# HiLinkSwitch

<div align="center">

![macOS 12+](https://img.shields.io/badge/macOS-12%2B-blue?logo=apple)

![License: MIT](https://img.shields.io/badge/license-MIT-green)

</div>

**English** | [Español](README-ES.md)

Use a Huawei HiLink 4G USB router or modem (E8372 and similar "Mobile WiFi"
sticks) as a wired internet connection on modern macOS, without Huawei's
installer, which no longer works.

A tiny program, run by launchd every time you plug the router in, switches
it from its "CD-ROM" mode to network mode. macOS then uses it with its own
built-in driver. No root, no kernel extension, nothing from Huawei.

## The problem

When plugged in, these devices first show up as a virtual CD-ROM (a volume
called `MobileWiFi` or similar) holding Huawei's installer. In that mode
there is no network. On Windows and older macOS, a daemon from that
installer switched the device to network mode; on current macOS the
installer does not work, so the router stays a CD-ROM and you get no
internet over USB.

## Requirements

- macOS 12 Monterey or later, Intel or Apple Silicon.
- Xcode Command Line Tools, to build the program. If you do not have them:

  ```sh
  xcode-select --install
  ```

- A Huawei HiLink device that shows up in CD-ROM mode with vendor ID
  `0x12d1` and product ID `0x1f01`, `0x1f02`, `0x157d` or `0x158b`. To check
  yours, plug it in and run:

  ```sh
  system_profiler SPUSBDataType 2> /dev/null | grep -A 3 -i huawei
  ```

  In CD-ROM mode it shows `Product ID: 0x1f01` (or one of the others) and
  `Vendor ID: 0x12d1`.

## Install

```sh
git clone https://github.com/function0xMarki/HiLinkSwitch.git
cd HiLinkSwitch
./install.sh
```

Or download the ZIP from GitHub (Code → Download ZIP), unzip it, open
Terminal in that folder and run `sh install.sh`.

The script builds the program and sets up a LaunchAgent for your user. It
needs no password and only writes to your `~/Library`:

| What | Where |
| --- | --- |
| Program | `~/Library/Application Support/HiLinkSwitch/hilink-switch` |
| LaunchAgent | `~/Library/LaunchAgents/local.hilink-switch.plist` |
| Log | `~/Library/Logs/hilink-switch.log` |

macOS will notify you that a background item was added: it is this
LaunchAgent. It must stay allowed in System Settings → General → Login Items
(Login Items & Extensions on macOS 15 and later), under "Allow in the
Background", for the switch to happen on its own.

## Use

Plug the router in. After a few seconds its CD-ROM volume disappears and
the router comes back as a network device: System Settings → Network shows
a new `HUAWEI_MOBILE` service, connected, with an address like
`192.168.8.100` given by the router.

The log shows what happened:

```sh
cat ~/Library/Logs/hilink-switch.log
```

```
2026-10-02 23:19:10 hilink-switch: found 12d1:1f01 at 0x14200000
2026-10-02 23:19:12 hilink-switch: unmounted disk3
2026-10-02 23:19:14 hilink-switch: unmounted disk2
2026-10-02 23:19:14 hilink-switch: switch request sent (0xe000404f)
2026-10-02 23:19:14 hilink-switch: switched: the device comes back in network mode
```

The code after "switch request sent" does not matter: the device usually
disconnects before answering.

Every time the router is unplugged or restarts it goes back to CD-ROM mode,
and the switch happens again by itself. To run it by hand:

```sh
~/Library/Application\ Support/HiLinkSwitch/hilink-switch
```

The router's own web page, with signal, data usage and settings, is usually
at <http://192.168.8.1>.

## Troubleshooting

- **Nothing happens when plugging it in.** Check that the background item
  is allowed in System Settings → General → Login Items, then run the
  program by hand (see above) and read what it says. If the Mac does not
  see the router at all (check with the `system_profiler` command above),
  see the next point.
- **The router keeps disappearing, or the Mac stops seeing it.** Plug it
  straight into the Mac, or into a hub with its own power supply. On 4G these
  routers draw a lot of current, and a bus-powered hub can drop out under
  that load, taking the router with it.
- **"not switching: a volume is still mounted".** A file on the router's
  memory card is open. Close it or eject the card in Finder, then plug the
  router in again. The program never forces the card out, to avoid losing
  data.
- **Apple Silicon laptops ask to allow the accessory.** Allow it. You may be
  asked a second time after the switch, because the router comes back as a
  different USB device.
- **Connected, but slow.** The program only changes the USB mode; the mobile
  network is up to the router. Check on its web page whether it fell back to
  3G: in the mobile network settings you can set the network mode to
  "4G only".
- **Both Wi-Fi and the router are connected.** macOS uses the first one in
  the service order: System Settings → Network → ⋯ → Set Service Order.

## Uninstall

```sh
./uninstall.sh
```

It removes the LaunchAgent, the program and the log.

## How it works

Disassembling `mbbservice`, the daemon in Huawei's macOS installer, shows
that on macOS after 10.9 it switches the device in two steps:

1. Unmount the device's volumes (the virtual CD and the memory card).
2. Send it a vendor USB control request: `bmRequestType 0x40`,
   `bRequest 0xA1`, no data.

The device disconnects and comes back as a CDC-ECM network interface (for
the E8372, USB ID `12d1:14db`), which macOS drives with its built-in
`AppleUserECM` driver, and the router hands out an address over DHCP.

`hilink-switch` does only those two steps. launchd starts it through an
IOKit matching event when a device with one of the CD-ROM mode IDs appears.
If the virtual CD is busy, its unmount is forced, since it is read-only and
nothing can be lost; the memory card is never forced.

## Why not Huawei's installer

Besides not working on current macOS, it is not worth reviving:

- `HiLink.app` is a Carbon app built with the macOS 10.12 SDK that starts an
  AppleScript applet (with PowerPC and i386 code) to install an old
  bundle-format package.
- Its install script runs `chmod a+w /usr` as root, changes the permissions
  of `/etc/sudoers` and creates `/usr/local/FlashcardService` with mode
  `777`.
- Its daemon runs as root and redirects command output with `>` to
  `/usr/local/FlashcardService/cmd.txt`. Since anyone can write to that
  directory, any local user can place a symbolic link there and make root
  overwrite a system file.

If you ever ran it, check for and remove what it left behind:

```sh
ls -ld /usr/local/FlashcardService /Library/StartupItems/MobileBrServ /Library/LaunchDaemons/com.huawei.mbbservice.plist
```

## Compatibility

- Tested with a **Huawei E8372** (HiLink firmware `21.333.03.00.00`) on
  macOS 15.8.1 Sequoia, Intel.
- Built as a universal binary for Intel and Apple Silicon, macOS 12 or later.
  Not yet tested on Apple Silicon or on macOS 26; reports are welcome.
- Other Huawei HiLink devices that use the same CD-ROM mode IDs should work
  the same way, but have not been tested. Please open an issue with your
  model and result.
- Devices with "stick" firmware instead of HiLink are not supported: they
  connect through AT commands or dial-up instead of acting as a router.

## Security and privacy

- Runs as your user, never as root, and installs nothing outside `~/Library`.
- Makes no network connections: it only talks to the USB device and to
  macOS's disk arbitration service.
- Acts only on the volumes of the Huawei device itself, and never forces the
  unmount of writable media.
- The log holds only times, USB IDs and disk names.

## License

[MIT](LICENSE)
