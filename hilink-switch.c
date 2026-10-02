/*
 * hilink-switch: switches Huawei HiLink modems and routers from "CD-ROM"
 * mode (12d1:1f01 and similar) to CDC-ECM network mode, which macOS drives
 * with its own AppleUserECM driver.
 *
 * It does the same as the mbbservice daemon from Huawei's installer on
 * macOS > 10.9: it unmounts the device's volumes (virtual CD and memory
 * card) and sends it a vendor USB control request: bmRequestType 0x40,
 * bRequest 0xA1, no data. The device disconnects and comes back as a
 * network interface.
 *
 * Usage:
 *   hilink-switch            find the device and switch it
 *   hilink-switch --launchd  the same, started by launchd when plugged in
 *
 * Build:
 *   clang -O2 -Wall -o hilink-switch hilink-switch.c \
 *         -framework IOKit -framework CoreFoundation -framework DiskArbitration
 */

#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/usb/IOUSBLib.h>
#include <dispatch/dispatch.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xpc/xpc.h>

#define HUAWEI_VID 0x12d1

/* CD-ROM mode PIDs watched by mbbservice (ArConfig.dat, [PRODUCT_ID]) */
static const int64_t cdrom_pids[] = { 0x1f01, 0x1f02, 0x157d, 0x158b };

/* What mbbservice's activateDevice() sends */
#define SWITCH_REQTYPE 0x40 /* vendor, host to device, recipient device */
#define SWITCH_REQUEST 0xA1

#define MAX_DISKS 8

static void logmsg(const char *fmt, ...)
{
    char ts[32] = "";
    time_t now = time(NULL);
    struct tm tm;
    if (localtime_r(&now, &tm))
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(stderr, "%s hilink-switch: ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* Numeric registry property, or -1 if missing. 64 bits so that location
   IDs from 0x80000000 up are not truncated. */
static int64_t int_property(io_service_t svc, CFStringRef key)
{
    int64_t value = -1;
    CFTypeRef ref = IORegistryEntryCreateCFProperty(svc, key, kCFAllocatorDefault, 0);
    if (ref) {
        if (CFGetTypeID(ref) == CFNumberGetTypeID())
            CFNumberGetValue(ref, kCFNumberSInt64Type, &value);
        CFRelease(ref);
    }
    return value;
}

/* Connected USB devices. IOKit does not accept idVendor alone as a
   matching key (it goes with idProduct), so the vendor is filtered later. */
static kern_return_t usb_devices(io_iterator_t *it)
{
    return IOServiceGetMatchingServices(kIOMainPortDefault,
                                        IOServiceMatching(kIOUSBHostDeviceClassName), it);
}

static bool is_cdrom_pid(int64_t pid)
{
    for (size_t i = 0; i < sizeof cdrom_pids / sizeof cdrom_pids[0]; i++)
        if (cdrom_pids[i] == pid)
            return true;
    return false;
}

static bool is_huawei_cdrom(io_service_t dev)
{
    return int_property(dev, CFSTR(kUSBVendorID)) == HUAWEI_VID
        && is_cdrom_pid(int_property(dev, CFSTR(kUSBProductID)));
}

/* Whole disks (diskN) under the USB device: virtual CD and memory card.
   Keeps the registry objects, not their BSD names: a diskN number freed by
   an unplugged device can be given to another disk. */
static int find_disks(io_service_t dev, io_object_t media[], int max)
{
    io_iterator_t it;
    int n = 0;
    if (IORegistryEntryCreateIterator(dev, kIOServicePlane, kIORegistryIterateRecursively, &it) != KERN_SUCCESS)
        return 0;
    io_object_t obj;
    while ((obj = IOIteratorNext(it))) {
        if (n < max && IOObjectConformsTo(obj, kIOMediaClass)) {
            CFTypeRef whole = IORegistryEntryCreateCFProperty(obj, CFSTR(kIOMediaWholeKey), kCFAllocatorDefault, 0);
            if (whole == kCFBooleanTrue) {
                media[n++] = obj; /* the caller releases it */
                obj = IO_OBJECT_NULL;
            }
            if (whole)
                CFRelease(whole);
        }
        if (obj)
            IOObjectRelease(obj);
    }
    IOObjectRelease(it);
    return n;
}

struct unmount_result {
    bool done;
    bool ok;
};

static void unmount_cb(DADiskRef disk, DADissenterRef dissenter, void *ctx)
{
    struct unmount_result *r = ctx;
    r->done = true;
    r->ok = (dissenter == NULL);
    if (dissenter) {
        CFStringRef why = DADissenterGetStatusString(dissenter);
        char buf[256] = "";
        if (why)
            CFStringGetCString(why, buf, sizeof buf, kCFStringEncodingUTF8);
        const char *bsd = DADiskGetBSDName(disk);
        logmsg("could not unmount %s: 0x%x %s", bsd ? bsd : "?",
               DADissenterGetStatus(dissenter), buf);
    }
}

/* Whether the DiskArbitration disk is still this registry object */
static bool same_media(DADiskRef disk, io_object_t media)
{
    io_service_t current = DADiskCopyIOMedia(disk);
    bool same = current && IOObjectIsEqualTo(current, media);
    if (current)
        IOObjectRelease(current);
    return same;
}

static bool is_read_only(DADiskRef disk)
{
    bool ro = false;
    CFDictionaryRef desc = DADiskCopyDescription(disk);
    if (desc) {
        ro = CFDictionaryGetValue(desc, kDADiskDescriptionMediaWritableKey) == kCFBooleanFalse;
        CFRelease(desc);
    }
    return ro;
}

static bool try_unmount(DADiskRef disk, DADiskUnmountOptions options)
{
    struct unmount_result r = { false, false };
    DADiskUnmount(disk, kDADiskUnmountOptionWhole | options, unmount_cb, &r);
    for (int i = 0; i < 60 && !r.done; i++)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, true);
    return r.ok;
}

/* Unmounts every volume of a disk; true if nothing is left mounted.
   If it is busy, unmounting is only forced on a read-only disk (the
   virtual CD), where nothing can be lost; the memory card never is. */
static bool unmount_disk(DASessionRef session, io_object_t media)
{
    DADiskRef disk = DADiskCreateFromIOMedia(kCFAllocatorDefault, session, media);
    if (!disk)
        return true; /* already gone */
    const char *name = DADiskGetBSDName(disk);
    char bsd[32];
    snprintf(bsd, sizeof bsd, "%s", name ? name : "?");

    bool ok = false, gone = false;
    for (int attempt = 1; attempt <= 3 && !ok; attempt++) {
        if (!same_media(disk, media)) {
            gone = true;
            break;
        }
        ok = try_unmount(disk, kDADiskUnmountOptionDefault);
        if (!ok)
            sleep(1);
    }
    if (!ok && !gone && same_media(disk, media) && is_read_only(disk)) {
        logmsg("%s is read-only: forcing the unmount", bsd);
        ok = try_unmount(disk, kDADiskUnmountOptionForce);
    }
    CFRelease(disk);
    if (gone)
        logmsg("%s is gone", bsd);
    else if (ok)
        logmsg("unmounted %s", bsd);
    return ok || gone;
}

/* Sends the mode switch request. Returns the IOKit result code. */
static IOReturn send_switch(io_service_t dev)
{
    IOCFPlugInInterface **plugin = NULL;
    IOUSBDeviceInterface **usb = NULL;
    SInt32 score;
    IOReturn kr = IOCreatePlugInInterfaceForService(dev, kIOUSBDeviceUserClientTypeID,
                                                    kIOCFPlugInInterfaceID, &plugin, &score);
    if (kr != kIOReturnSuccess || !plugin)
        return kr ? kr : kIOReturnError;
    HRESULT hr = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                                           (LPVOID *)&usb);
    IODestroyPlugInInterface(plugin);
    if (hr || !usb)
        return kIOReturnError;

    /* mbbservice opens it first; if someone else holds it, send anyway */
    IOReturn open_kr = (*usb)->USBDeviceOpen(usb);
    if (open_kr != kIOReturnSuccess)
        logmsg("USBDeviceOpen: 0x%x, sending without opening", open_kr);

    IOUSBDevRequest req = {
        .bmRequestType = SWITCH_REQTYPE,
        .bRequest = SWITCH_REQUEST,
        .wValue = 0,
        .wIndex = 0,
        .wLength = 0,
        .pData = NULL,
    };
    kr = (*usb)->DeviceRequest(usb, &req);

    if (open_kr == kIOReturnSuccess)
        (*usb)->USBDeviceClose(usb);
    (*usb)->Release(usb);
    return kr;
}

/* Is a device in CD-ROM mode still connected at this USB location? */
static bool still_present(int64_t location)
{
    io_iterator_t it;
    if (usb_devices(&it) != KERN_SUCCESS)
        return false;
    bool found = false;
    io_service_t dev;
    while ((dev = IOIteratorNext(it))) {
        if (is_huawei_cdrom(dev) && int_property(dev, CFSTR(kUSBDevicePropertyLocationID)) == location)
            found = true;
        IOObjectRelease(dev);
    }
    IOObjectRelease(it);
    return found;
}

static int switch_device(DASessionRef session, io_service_t dev)
{
    int64_t pid = int_property(dev, CFSTR(kUSBProductID));
    int64_t location = int_property(dev, CFSTR(kUSBDevicePropertyLocationID));
    logmsg("found %04x:%04llx at 0x%08llx", HUAWEI_VID, (long long)pid, (long long)location);

    /* Let the system finish setting up the device and mounting its volumes */
    mach_timespec_t quiet = { 10, 0 };
    IOServiceWaitQuiet(dev, &quiet);
    sleep(2);

    io_object_t media[MAX_DISKS];
    int ndisks = find_disks(dev, media, MAX_DISKS);
    bool unmounted = true;
    for (int i = 0; i < ndisks; i++) {
        if (unmounted && !unmount_disk(session, media[i]))
            unmounted = false;
        IOObjectRelease(media[i]);
    }
    if (!unmounted) {
        logmsg("not switching: a volume is still mounted; eject it and plug the device in again");
        return 1;
    }

    IOReturn kr = send_switch(dev);
    logmsg("switch request sent (0x%x)", kr);

    /* The device disconnects as soon as it gets it, sometimes without answering */
    for (int i = 0; i < 20; i++) {
        if (!still_present(location)) {
            logmsg("switched: the device comes back in network mode");
            return 0;
        }
        usleep(500000);
    }
    logmsg("the device is still in CD-ROM mode after the request");
    return 1;
}

static int switch_all(void)
{
    io_iterator_t it;
    if (usb_devices(&it) != KERN_SUCCESS) {
        logmsg("could not query the IOKit registry");
        return 1;
    }
    DASessionRef session = DASessionCreate(kCFAllocatorDefault);
    if (!session) {
        logmsg("could not open a DiskArbitration session");
        IOObjectRelease(it);
        return 1;
    }
    DASessionScheduleWithRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    int found = 0, failed = 0;
    io_service_t dev;
    while ((dev = IOIteratorNext(it))) {
        if (is_huawei_cdrom(dev)) {
            found++;
            failed += switch_device(session, dev);
        }
        IOObjectRelease(dev);
    }
    IOObjectRelease(it);
    DASessionUnscheduleFromRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFRelease(session);

    if (!found)
        logmsg("no Huawei HiLink device in CD-ROM mode");
    return failed ? 1 : 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "--launchd") == 0) {
        /* launchd starts us for an IOKit event: it must be consumed, or
           launchd would keep starting us again */
        dispatch_queue_t q = dispatch_queue_create("hilink-switch.events", NULL);
        xpc_set_event_stream_handler("com.apple.iokit.matching", q, ^(xpc_object_t ev) {
            (void)ev;
        });
        sleep(1);
    } else if (argc > 1) {
        fprintf(stderr, "usage: %s [--launchd]\n", argv[0]);
        return 2;
    }
    return switch_all();
}
