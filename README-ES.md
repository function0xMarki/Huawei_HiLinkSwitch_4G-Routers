# HiLinkSwitch

<div align="center">

![macOS 12+](https://img.shields.io/badge/macOS-12%2B-blue?logo=apple)

![License: MIT](https://img.shields.io/badge/license-MIT-green)

</div>

[English](README.md) | **Español**

Usa un router o módem USB 4G Huawei HiLink (el E8372 y otros «Mobile WiFi»
parecidos) como conexión a Internet por cable en un macOS moderno, sin el
instalador de Huawei, que ya no funciona.

Un programa diminuto, que launchd lanza cada vez que enchufas el router, lo
pasa de su modo «CD-ROM» al modo red. A partir de ahí macOS lo usa con su
propio driver. Sin root, sin extensiones del kernel y sin nada de Huawei.

## El problema

Al enchufarlos, estos aparatos aparecen primero como un CD-ROM virtual (un
volumen llamado `MobileWiFi` o parecido) con el instalador de Huawei. En ese
modo no hay red. En Windows y en versiones antiguas de macOS, un servicio de
ese instalador cambiaba el aparato a modo red; en macOS actual el instalador
no funciona, así que el router se queda como CD-ROM y no hay Internet por
USB.

## Requisitos

- macOS 12 Monterey o posterior, en Intel o Apple Silicon.
- Las Xcode Command Line Tools, para compilar el programa. Si no las tienes:

  ```sh
  xcode-select --install
  ```

- Un aparato Huawei HiLink que en modo CD-ROM aparezca con el ID de
  fabricante `0x12d1` y el de producto `0x1f01`, `0x1f02`, `0x157d` o
  `0x158b`. Para comprobar el tuyo, enchúfalo y ejecuta:

  ```sh
  system_profiler SPUSBDataType 2> /dev/null | grep -A 3 -i huawei
  ```

  En modo CD-ROM muestra `Product ID: 0x1f01` (o uno de los otros) y
  `Vendor ID: 0x12d1`.

## Instalación

```sh
git clone https://github.com/function0xMarki/HiLinkSwitch.git
cd HiLinkSwitch
./install.sh
```

O descarga el ZIP desde GitHub (Code → Download ZIP), descomprímelo, abre el
Terminal en esa carpeta y ejecuta `sh install.sh`.

El script compila el programa y configura un LaunchAgent para tu usuario. No
pide contraseña y solo escribe en tu `~/Library`:

| Qué | Dónde |
| --- | --- |
| Programa | `~/Library/Application Support/HiLinkSwitch/hilink-switch` |
| LaunchAgent | `~/Library/LaunchAgents/local.hilink-switch.plist` |
| Registro | `~/Library/Logs/hilink-switch.log` |

macOS avisará de que «Se han añadido ítems en segundo plano»: es este
LaunchAgent. Para que el cambio de modo se haga solo, tiene que seguir
permitido en Ajustes del Sistema → General → Ítems de inicio («Ítems de
inicio y extensiones» en macOS 15 y posteriores), en «Permitir en segundo
plano».

## Uso

Enchufa el router. A los pocos segundos desaparece su volumen de CD-ROM y el
router vuelve como dispositivo de red: en Ajustes del Sistema → Red aparece
un servicio nuevo, `HUAWEI_MOBILE`, conectado y con una dirección como
`192.168.8.100` que le da el router.

El registro cuenta lo que ha pasado:

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

El código que aparece tras «switch request sent» no importa: el aparato
suele desconectarse antes de responder.

Cada vez que el router se desenchufa o se reinicia vuelve al modo CD-ROM, y
el cambio se repite solo. Para lanzarlo a mano:

```sh
~/Library/Application\ Support/HiLinkSwitch/hilink-switch
```

La web del propio router, con la señal, el consumo de datos y los ajustes,
suele estar en <http://192.168.8.1>.

## Problemas frecuentes

- **No pasa nada al enchufarlo.** Comprueba que el ítem en segundo plano
  está permitido en Ajustes del Sistema → General → Ítems de inicio; después
  lanza el programa a mano (ver arriba) y lee lo que dice. Si el Mac no ve
  el router en absoluto (compruébalo con el comando `system_profiler` de
  arriba), mira el punto siguiente.
- **El router desaparece una y otra vez, o el Mac deja de verlo.** Conéctalo
  directo al Mac o a un hub con alimentación propia. En 4G estos routers
  consumen bastante corriente, y un hub sin alimentación puede caerse con esa
  carga y llevarse el router con él.
- **«not switching: a volume is still mounted».** Hay un fichero abierto en
  la tarjeta de memoria del router. Ciérralo o expulsa la tarjeta en el
  Finder, y vuelve a enchufar el router. El programa nunca expulsa la
  tarjeta a la fuerza, para no perder datos.
- **Los portátiles con Apple Silicon preguntan «¿Permitir que se conecte el
  accesorio?».** Permítelo. Puede que lo pregunte una segunda vez tras el
  cambio, porque el router vuelve como otro dispositivo USB.
- **Conecta, pero va lento.** El programa solo cambia el modo USB; la red
  móvil es cosa del router. Mira en su web si ha bajado a 3G: en los ajustes
  de red móvil puedes fijar el modo de red en «Solo 4G».
- **Están conectados a la vez el WiFi y el router.** macOS usa el primero
  del orden de servicios: Ajustes del Sistema → Red → ⋯ → Establecer orden
  de servicios.

## Desinstalación

```sh
./uninstall.sh
```

Quita el LaunchAgent, el programa y el registro.

## Cómo funciona

Al desensamblar `mbbservice`, el servicio del instalador de Huawei para
macOS, se ve que en macOS posterior a 10.9 cambia el aparato en dos pasos:

1. Desmonta los volúmenes del aparato (el CD virtual y la tarjeta de
   memoria).
2. Le envía una petición USB de control de fabricante: `bmRequestType 0x40`,
   `bRequest 0xA1`, sin datos.

El aparato se desconecta y vuelve como interfaz de red CDC-ECM (en el E8372,
con el ID USB `12d1:14db`), que macOS maneja con su driver integrado
`AppleUserECM`, y el router reparte una dirección por DHCP.

`hilink-switch` hace solo esos dos pasos. launchd lo lanza mediante un
evento de IOKit cuando aparece un aparato con uno de los ID del modo CD-ROM.
Si el CD virtual está ocupado, se fuerza su desmontaje, porque es de solo
lectura y no hay nada que perder; la tarjeta de memoria nunca se fuerza.

## Por qué no el instalador de Huawei

Además de no funcionar en macOS actual, no merece la pena recuperarlo:

- `HiLink.app` es una aplicación Carbon compilada con el SDK de macOS 10.12
  que lanza un applet de AppleScript (con código PowerPC e i386) para
  instalar un paquete con el antiguo formato de *bundle*.
- Su script de instalación ejecuta como root `chmod a+w /usr`, cambia los
  permisos de `/etc/sudoers` y crea `/usr/local/FlashcardService` con
  permisos `777`.
- Su servicio corre como root y redirige con `>` la salida de comandos a
  `/usr/local/FlashcardService/cmd.txt`. Como cualquiera puede escribir en
  ese directorio, cualquier usuario local puede poner ahí un enlace
  simbólico y hacer que root sobrescriba un fichero del sistema.

Si alguna vez lo ejecutaste, busca y borra lo que dejó:

```sh
ls -ld /usr/local/FlashcardService /Library/StartupItems/MobileBrServ /Library/LaunchDaemons/com.huawei.mbbservice.plist
```

## Compatibilidad

- Probado con un **Huawei E8372** (firmware HiLink `21.333.03.00.00`) en
  macOS 15.8.1 Sequoia, en Intel.
- Se compila como binario universal para Intel y Apple Silicon, macOS 12 o
  posterior. Aún no se ha probado en Apple Silicon ni en macOS 26; se
  agradecen los informes.
- Otros aparatos Huawei HiLink que usen los mismos ID en modo CD-ROM
  deberían funcionar igual, pero no se han probado. Abre una *issue* con tu
  modelo y el resultado.
- No sirven los aparatos con firmware «stick» en vez de HiLink: se conectan
  con comandos AT o por marcación en lugar de funcionar como router.

## Seguridad y privacidad

- Corre como tu usuario, nunca como root, y no instala nada fuera de
  `~/Library`.
- No hace ninguna conexión de red: solo habla con el aparato USB y con el
  servicio de discos de macOS.
- Solo actúa sobre los volúmenes del propio aparato Huawei y nunca fuerza el
  desmontaje de un soporte en el que se pueda escribir.
- El registro solo guarda horas, ID USB y nombres de disco.

## Licencia

[MIT](LICENSE)
