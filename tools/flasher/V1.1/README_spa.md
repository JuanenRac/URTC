<p align="center">
  <img src="/images/URTC_LOGO_FLASHER.svg" alt="URTC Flasher Logo" width="100%">
</p>

# URTC Flasher (Windows / Linux)

**Versión:** 1.1 (la versión de esta herramienta - se muestra en el banner
de la ventana y en la barra de título, se controla por separado de la
versión del firmware de la placa URTC que escribe)

**Autor:** JuanenRac (Electro Hobby 3D) &lt;electrohobby3d@gmail.com&gt;

Licencia: **GPL-3.0**, la misma que el propio firmware de URTC — ver
`LICENSE` en la raíz del repositorio. Esto cubre `urtc_flasher.py` y
cualquier binario compilado a partir de él.

Una pequeña herramienta GUI multiplataforma para actualizar el firmware
de la placa URTC por bus CAN. Implementa exactamente el protocolo del
bootloader de `docs/CANBUS.TXT`: la verificación de HardwareID, la firma
HMAC-SHA256, el flujo de actualización con slot de respaldo de imagen
dorada, el progreso en vivo mediante los mensajes de latido del
bootloader, y una consulta de versión (identifica si la aplicación *o*
el bootloader de esta placa se anuncia por CAN) para que puedas ver qué
hay instalado actualmente antes de decidir qué flashear.

Dos formas de hablar con la placa, ambas usando el mismo protocolo por
debajo:

- **Serie / SLCAN** — funciona en Windows y Linux. Necesita un adaptador
  USB-CAN con firmware SLCAN, conectado como puerto serie virtual.
- **SocketCAN** — **solo Linux**, y solo se muestra en la interfaz de la
  herramienta en Linux. Habla directamente con una interfaz de red del
  kernel `can0`/`slcan0`. Si tu adaptador ya ejecuta firmware
  `gs_usb`/candleLight (la mayoría de las placas CANable lo hacen de
  fábrica), esta ruta **no necesita reflashear el adaptador en
  absoluto** — el propio driver de Linux lo gestiona de forma nativa.

**Estado:** el cálculo de CRC32 y HMAC-SHA256 en esta herramienta se ha
verificado byte a byte contra la propia implementación en C del
bootloader, y el empaquetado de tramas SocketCAN se verificó contra el
formato `struct can_frame` de Linux con una prueba de empaquetado/
desempaquetado de ida y vuelta. Lo que **no** se ha probado en ninguna
de las 2 plataformas es una placa real sobre hardware real — trata el
primer intento real de flasheo con la misma precaución que darías a
cualquier herramienta nueva que hable con un bootloader: ten JTAG a mano
como respaldo.

## 1. Consigue que tu adaptador hable CAN

Cuál de estos necesitas depende de tu plataforma y de qué transporte
vayas a usar:

**Linux, ruta SocketCAN (recomendada si tu adaptador la soporta):**
Nada que flashear en el adaptador mismo. Levanta la interfaz una vez por
arranque (o añádela a tu configuración de red para que persista):
```
sudo modprobe can vcan gs_usb   # gs_usb cubre la mayoria de placas de la familia CANable
sudo ip link set can0 type can bitrate 500000
sudo ip link set can0 up
```
Si tu adaptador se enumera con un nombre distinto de `can0`, revisa `ip
link show` (o `dmesg` justo después de conectarlo) para ver el nombre
real. Algunos adaptadores necesitan `slcand` en vez de un driver nativo
— si `ip link show` no muestra ninguna interfaz CAN en absoluto tras
conectarlo, este es probablemente tu caso; consulta la documentación de
tu adaptador para la invocación de `slcand`, que crea una interfaz
`slcan0` que luego levantas de la misma forma que arriba.

**Windows, o Linux por la ruta Serie/SLCAN:**
Una CANable Pro v2 viene de fábrica con firmware **candleLight**, que
habla con el host usando el protocolo `gs_usb` - el mismo que el driver
`gs_usb` de SocketCAN en Linux espera de forma nativa (ver arriba). Ese
protocolo **no** se presenta como puerto serie, que es lo que necesita
esta ruta. Para usar Serie/SLCAN en su lugar (obligatorio en Windows;
opcional en Linux):

1. Descarga firmware compatible con SLCAN para tu adaptador (busca
   "canable slcan firmware" — hay varios forks mantenidos; usa el que
   indique la propia documentación de tu adaptador).
2. Pon el adaptador en modo DFU/bootloader (normalmente un botón BOOT
   mantenido durante el encendido, o un jumper — revisa la documentación
   de tu adaptador).
3. Flashea el firmware SLCAN usando la herramienta de flasheo del
   fabricante de tu adaptador o `dfu-util`.
4. Reconecta — ahora debería enumerarse como puerto serie: un puerto COM
   en Windows, o al estilo `/dev/ttyACM0`/`/dev/ttyUSB0` en Linux.

Si tu adaptador ya ejecuta firmware SLCAN, salta directamente al paso 2
de abajo.

Una línea SLCAN recibida cuya longitud real no coincida con lo que
implica su propio DLC declarado se trata como malformada y se descarta,
en vez de analizarse desde sus primeros N caracteres hexadecimales sin
importar lo que sigue - vale la pena saberlo si estás depurando contra
un adaptador ruidoso o no estándar.

## 2. Instalar y ejecutar

**Windows:**
```
python -m pip install -r requirements.txt
python urtc_flasher.py
```
O compila un `.exe` independiente con `build_exe.bat` (ver ese archivo).

**Linux:**
```
python3 -m pip install -r requirements.txt
python3 urtc_flasher.py
```
O compila un binario independiente con `./build_exe.sh` (`chmod +x`
primero).

Ambos scripts pasan `--noconfirm` a PyInstaller, así que reconstruir
sobre un `dist/URTC_Flasher` ya existente lo reemplaza directamente en
vez de esperar un aviso de "¿reemplazar?" que es fácil pasar por alto en
la salida de un script.

### Barra de menú

- **Archivo** - Guardar registros (el registro en pantalla como texto
  plano; para un paquete más completo que incluya diagnósticos del
  sistema y el archivo de firmware seleccionado actualmente, ver
  "Diagnósticos" más abajo), y Salir.
- **Idioma** - cambia entre los 5 idiomas disponibles (ver "Idioma" más
  abajo para saber cómo funcionan las traducciones).
- **Ayuda** - Readme (abre este archivo en una ventana de solo lectura;
  recoge automáticamente una versión traducida en cuanto exista una para
  el idioma actual), GitHub de URTC (abre el repositorio del proyecto en
  tu navegador), Licencia (la licencia GPL-3.0 de esta herramienta,
  leída desde el propio archivo `LICENSE` del repositorio), y Acerca de
  (versión y autor).

**Al arrancar**, el banner se muestra centrado en pantalla durante 5
segundos antes de que aparezca la ventana principal - no forma parte de
la ventana principal en sí (por eso la ventana es bastante compacta para
todo lo que realmente hace). El icono de ventana/barra de tareas es un
diseño pequeño independiente (`assets/urtc_icon.png`/`.ico`), no el
banner reducido - la ilustración completa del banner no se sostiene bien
a 16-32px.

**Idioma**: inglés por defecto.
Se cambia mediante el menú **Idioma** (en la barra de menú en la parte
superior de la ventana) en vez de un desplegable en la ventana principal
- se guarda inmediatamente en `urtc_config.json` (el mismo archivo usado
para los overrides técnicos de hardware — la preferencia de idioma
simplemente vive junto a esos), y se aplica en el siguiente arranque.
Las traducciones viven en archivos de texto plano bajo `language/`
(`english.lng`, `spanish.lng`, `italian.lng`, `french.lng`,
`german.lng`) como pares simples `CLAVE=Valor`, uno por línea - las
líneas que empiezan con `#` y las líneas en blanco se ignoran, y un `\n`
literal dentro de un valor se convierte en un salto de línea real (usado
por el puñado de mensajes de diálogo multilínea). Editable directamente
si una traducción necesita corregirse, o como punto de partida para otro
idioma (añade `language/<nombre>.lng`, añade `("<nombre>", "Nombre
Nativo")` a `AVAILABLE_LANGUAGES` cerca del principio de
`flasher_config.py`, y pon `"language": "<nombre>"` en
`urtc_config.json`). Una clave que falte en un archivo de idioma cae de
vuelta a mostrar el nombre de esa misma clave en vez de fallar, y un
archivo de idioma ausente o ilegible (edición defectuosa, nombre de
archivo equivocado) cae de vuelta al inglés para toda la interfaz - de
cualquier forma la herramienta se mantiene usable mientras se resuelve
el desajuste.

Tkinter (el toolkit de la GUI) viene incluido con Python en Windows,
pero en distros de la familia Debian/Ubuntu es un paquete del sistema
operativo separado:
```
sudo apt install python3-tk
```
(Fedora: `sudo dnf install python3-tkinter`. Arch: `sudo pacman -S tk`.)
`build_exe.sh` comprueba esto por sí mismo y te avisa si falta en vez de
fallar a mitad de camino.

**Permisos de serie en Linux:** si estás usando la ruta Serie/SLCAN y la
conexión falla con "Permission denied", tu usuario necesita estar en el
grupo propietario de los dispositivos serie (`dialout` en
Debian/Ubuntu; varía en otras distros):
```
sudo usermod -a -G dialout $USER
```
Cierra sesión y vuelve a entrar (la pertenencia a grupos se lee al
iniciar sesión), luego inténtalo de nuevo. La herramienta detecta este
error específico y muestra esta misma solución en un diálogo, pero vale
la pena saberlo de antemano. SocketCAN no tiene este problema particular
— el acceso a una interfaz tipo `can0` no está controlado por el grupo
`dialout` — pero levantar la interfaz en primer lugar (paso 1 de arriba)
sí necesita `sudo`, ya que es un cambio de configuración de dispositivo
de red.

Usar `python -m pip`/`python3 -m pip` en vez de un `pip` a secas evita
un problema común en ambas plataformas: el propio script wrapper de
`pip` no siempre está en el PATH incluso justo después de una
instalación exitosa, mientras que `-m pip` encuentra el módulo instalado
directamente.

## 3. Dónde van los archivos de firmware

Esta herramienta espera una carpeta `firmware/` **dentro de
`tools/flasher/V1.1/`**, justo al lado de `urtc_flasher.py`:

```
tools/flasher/V1.1/
├── assets/
│   ├── URTC_LOGO_FLASHER.svg      <- fuente del banner (vectorial)
│   └── urtc_banner.png            <- se muestra en la parte superior de la ventana, renderizado desde el .svg de arriba
├── firmware/
│   ├── URTC_v1_0_F303CC.bin      <- pon los archivos .bin nuevos aqui
│   └── URTC_v1_0_F303CC_old.bin  <- tambien puedes conservar versiones anteriores
├── logs/                          <- se crea automaticamente, un archivo por sesion
├── urtc_config.json               <- opcional, no incluido por defecto (ver "Cambiar la clave HMAC" mas abajo)
├── urtc_flasher.py                <- punto de entrada: argumentos CLI, pantalla de bienvenida, configuracion de la ventana principal
├── flasher_config.py              <- E/S del archivo de configuracion, carga de idioma, constantes del protocolo
├── flasher_transports.py          <- SLCAN, SocketCAN, MockCAN
├── flasher_swd_tools.py           <- envoltorios de STM32CubeProgrammer / pyOCD
├── flasher_validation.py          <- validacion de archivos de firmware (.bin/.hex/.elf)
├── flasher_protocol.py            <- la propia maquina de estados de CAN OTA
├── flasher_gui.py                 <- la ventana principal (FlasherGUI) y su barra de menu
├── requirements.txt
├── build_exe.bat                  <- compilacion independiente para Windows
├── build_exe.sh                   <- compilacion independiente para Linux
└── README.md
```

Esta herramienta está organizada en los módulos de arriba por
responsabilidad, puramente por legibilidad - no hay ninguna diferencia
funcional entre tenerlos como archivos separados o como uno grande, y no
existe una forma monolítica que mantener sincronizada como sí ocurre con
el firmware (esto es una herramienta de PC, no algo que se flashea en la
placa, así que solo hay una forma).

`assets/urtc_banner.png` es opcional - si falta, la herramienta
simplemente arranca sin banner en vez de fallar. Se carga mediante el
soporte nativo de PNG de tkinter (Tk 8.6+, que incluye toda versión
actual de Python), no Pillow, así que no añade una dependencia nueva.
Tanto `build_exe.bat` como `build_exe.sh` ya empaquetan `assets/` en el
ejecutable independiente mediante el `--add-data` de PyInstaller, así
que funciona igual tanto si ejecutas desde el código fuente como desde
un binario compilado.

Esto es deliberado: mantener `firmware/` dentro de `tools/flasher/V1.1/`
en vez de en la raíz del repositorio significa que toda la carpeta
`tools/flasher/V1.1/` es autocontenida. Si solo quieres flashear una
placa — en un PC de taller, desde una memoria USB, donde sea — puedes
copiar `tools/flasher/V1.1/` por sí sola sin nada más del repositorio, y
sigue funcionando.

**Puedes tener más de un `.bin` ahí.** Cada archivo se revisa y se
lista - la herramienta no coge simplemente lo primero que encuentra. Al
arrancar (y cada vez que hagas clic en **Actualizar**), cada `.bin` en
`firmware/` se revisa contra la misma prueba de plausibilidad que aplica
el propio bootloader a una imagen nueva (sus primeros 4 bytes tienen que
parecer un puntero de pila inicial real para la RAM de este chip, y su
tamaño tiene que caber en el slot principal). Cada archivo aparece en la
lista con un ✓ o ✗ claro y el motivo:

| Archivo | Tamaño | Estado |
|---|---|---|
| URTC_v1_0_F303CC.bin | 30.9 KB | ✓ parece válido |
| URTC_v1_0_F303CC_old.bin | 30.4 KB | ✓ parece válido |
| notes.txt.bin | 0.1 KB | ✗ la primera palabra no parece un puntero de pila válido |

- **Exactamente un archivo pasa la revisión** → se selecciona por ti en
  el momento en que arranca la herramienta. Un archivo inválido que esté
  solo en la carpeta *no* se auto-selecciona solo porque nada más
  compite con él.
- **Más de un archivo válido** → no se auto-selecciona nada; elige el
  que quieras de la lista.
- **Seleccionas un archivo que parece inválido de todas formas** → la
  herramienta te pide confirmar primero. Esta revisión existe para
  detectar errores obvios (archivo equivocado, descarga truncada, un
  marcador de posición vacío) - no puede detectarlo todo (un archivo
  corrupto pero plausible, o uno firmado con la clave equivocada), que
  es para lo que sirve la propia revisión de CRC32/HMAC del bootloader
  durante la transferencia real.
- **No se encuentra nada, o quieres un archivo de otro sitio por
  completo** → usa el botón **Buscar .bin...**, que funciona sin
  importar dónde viva realmente el archivo (y ejecuta la misma revisión
  de validación de cualquier forma).

**`<nombredearchivo>.manifest.json` opcional, junto a un archivo de
firmware** (p. ej. `URTC_v1_0_F303CC.bin.manifest.json`), añade una
comprobación de cordura extra y no bloqueante: si está presente, su
campo `sha256` se compara contra el archivo real justo antes de
flashear, con `version`/`build_date` registrados junto a ello como
referencia.

```json
{"version": "1.1", "build_date": "2026-07-23", "sha256": "e5a4918c..."}
```

Un desajuste se registra como una advertencia clara, no como una parada
forzosa - esta es una comprobación de conveniencia para detectar
temprano un archivo obviamente equivocado o corrupto, no un sustituto de
la propia verificación HMAC del bootloader durante la transferencia
real, que sigue siendo la comprobación autoritativa de cualquier forma.

Añadir una compilación nueva más tarde: solo suéltala en `firmware/` y
haz clic en **Actualizar** - no hace falta reiniciar.

## 4. Comprobar qué hay instalado actualmente

Si estás en Linux y SocketCAN está disponible, verás una opción de
**Transporte** en la parte superior - elige Serie/SLCAN o SocketCAN
antes de conectar. En Windows esta fila no aparece en absoluto;
Serie/SLCAN es la única opción.

Haz clic en **Conectar**, y la herramienta le pregunta automáticamente a
la placa qué está ejecutando actualmente (CAN ID `0x7F8` → `0x7F9` - ver
`docs/CANBUS.TXT`). Esto funciona tanto si la placa está ejecutando su
aplicación normalmente *como* si está en el bootloader, así que no
necesitas provocar un reinicio solo para averiguarlo. Haz clic en
**Consultar** en cualquier momento después para volver a comprobar (útil
justo después de que termine un flasheo, para confirmar que la versión
nueva realmente se aplicó).

**Cuando el propio bootloader responde** (la placa está en el
bootloader, sin ejecutar su aplicación), también informa de su propia
versión - algo separado de la versión de la aplicación instalada,
controlada mediante su propio `BOOTLOADER_VERSION_MAJOR/MINOR/PATCH` en
`BOOTLOADER.C` y enviada como una segunda trama (`0x7FA`) justo junto a
`0x7F9`. La aplicación en ejecución nunca envía esto - no tiene forma de
saber la versión de un bootloader actualmente flasheado salvo
preguntándole al propio bootloader, así que esto solo aparece cuando la
placa realmente está ahí sentada (justo después de `0x7F0`, o en un
arranque nuevo antes de saltar a la aplicación).

Lo que verás:

- **`v1.1 (application, HardwareID 0x0303CC01)`** - caso normal,
  aplicación en ejecución, todo coincide.
- **`Bootloader running, no valid firmware currently installed,
  bootloader v1.1.1`** - la placa está atascada en el bootloader sin
  nada a lo que saltar (chip en blanco, o cada comprobación en el slot
  principal falló). Esta es exactamente la situación para la que existe
  esta herramienta - flashéalo. La versión de bootloader mostrada aquí
  es la del propio bootloader, sin relación con la versión de aplicación
  que haya fallado sus comprobaciones.
- **`⚠ HardwareID mismatch!`** mostrado en rojo - algo respondió, pero
  su HardwareID no coincide con lo que espera esta herramienta. No
  flashees sin entender por qué primero; el bootloader rechazaría la
  actualización de todas formas, pero un desajuste aquí también puede
  significar que apuntas a la placa equivocada por completo.
- **Sin respuesta** (rojo) - placa no responde, bitrate equivocado, o no
  está realmente conectada. Revisa la conexión física y, en la ruta
  SocketCAN, que la interfaz realmente esté activa (`ip link show`).

**Placa de expansión:** un desplegable separado y un par
Consultar/Guardar, justo debajo de la comprobación de versión. Lee y
establece cuál de las 5 configuraciones posibles de `CONN_EXPANSION`
(ninguna, o una de las 4 variantes planificadas - ver `EXPANSION.TXT`)
está instalada físicamente, por CAN (`0x1A0`/`0x1A1`). No hay forma
eléctrica de que la placa detecte esto por sí misma, así que hay que
decírselo - esto vive aquí (no solo en `URTC Tester`) ya que es un paso
de configuración de hardware de una sola vez que se hace de forma más
natural junto con una actualización de firmware. **Guardar** pide
confirmación primero, ya que esto persiste entre ciclos de encendido
hasta que se cambie explícitamente de nuevo.

## 5. Flasheando

1. **Conectar**: elige Serie/SLCAN o SocketCAN (solo Linux), luego el
   puerto/interfaz, luego haz clic en Conectar. Para Serie/SLCAN esto
   abre el canal CAN a 500 kbit/s (la velocidad de bus fija de URTC);
   para SocketCAN se espera que la interfaz ya esté a esa velocidad
   (paso 1 de arriba) - esta herramienta no la establece. De cualquier
   forma, la versión actual se consulta automáticamente - ver sección 4
   de arriba.
2. **Seleccionar firmware**: elige de la lista detectada, o Buscar - ver
   sección 3 de arriba para saber exactamente cómo funcionan la
   detección y la validación.
3. **Flashear**:
   - Deja marcado "La placa está ejecutando actualmente la aplicación"
     si la placa está encendida y funcionando con normalidad - la
     herramienta envía primero el disparador de payload mágico `0x7F0`,
     que apaga de forma segura cada actuador antes de reiniciar hacia el
     bootloader.
   - Desmárcalo si la placa ya está en el bootloader (justo después de
     un flasheo JTAG nuevo, o si la comprobación de versión de arriba
     mostró "no valid firmware currently installed").
   - Haz clic en **Flashear Firmware** y confirma. El registro muestra
     cada paso del protocolo; la barra de progreso sigue el progreso de
     escritura página a página durante la transferencia, y luego el
     progreso de copia durante la copia final de respaldo a principal.

Si la verificación falla en cualquier punto (desajuste de CRC32, HMAC, o
HardwareID), el slot principal del bootloader nunca se toca - la placa
sigue ejecutando el firmware que ya tenía. Siempre es seguro simplemente
volver a intentarlo.

## 6. Programar el chip completo por SWD/JTAG (avanzado)

La sección "4. Program complete chip via SWD/JTAG" en la herramienta
hace un flasheo de puesta en marcha completo - borra todo el chip por
completo, luego escribe desde cero tanto la imagen del bootloader
(`0x08000000`) como la de la aplicación (`0x08008000`). Esto es un
**tipo de operación distinto** de las secciones 1-5 de arriba:

|  | Actualización CAN OTA (secciones 1-5) | SWD/JTAG chip completo (sección 6) |
|---|---|---|
| Auto-reparable si se interrumpe | Sí - el slot de respaldo de imagen dorada garantiza que el firmware en ejecución sobrevive | No - no ejecutará nada hasta que se reprograme |
| Recuperable | Automáticamente, sin acción necesaria | Sí - solo reconecta y flashea de nuevo por SWD; el puerto de depuración no depende del contenido de la flash. Solo un bloqueo verdaderamente permanente (option byte RDP2) impediría esto, y nada en esta herramienta establece option bytes |
| Toca el bootloader | Nunca | Sí, por diseño |
| Necesita | Un adaptador USB-CAN | Una sonda SWD/JTAG (ST-Link o similar) |
| Uso típico | Actualizaciones de firmware rutinarias | Primera puesta en marcha en un chip en blanco, o recuperar una placa inutilizada |

**Requiere uno de** (la herramienta autodetecta cuál está disponible y
solo habilita las que encuentra):
- **pyOCD** - `pip install pyocd`. Libre, de código abierto, sin
  instalación separada más allá del paquete pip.
- **STM32CubeProgrammer** - la herramienta oficial de ST, instalada por
  separado desde [st.com](https://www.st.com). Si ya la tienes para otro
  trabajo con STM32, no necesitas instalar nada extra aquí.

Ambas se ejecutan como subprocesos de línea de comandos, no se importan
como bibliotecas de Python - verás el comando exacto registrado antes de
que se ejecute.

**Formatos de archivo:** `.bin` (necesita la dirección fija que esta
herramienta ya conoce - no la introduces tú) o `.hex` (lleva su propia
dirección, se usa tal cual). Mezclar está bien - bootloader como `.hex`
y aplicación como `.bin`, o viceversa, ambos funcionan. Ambos selectores
de archivo validan el archivo elegido (tamaño plausible para el slot de
destino, y - donde el formato permite comprobarlo con confianza - un
puntero de pila inicial plausible) antes de dejarte continuar, de la
misma forma que ya lo hacía el selector de firmware de la ruta CAN.

**La conexión se comprueba antes de que se ejecute nada destructivo**,
exigiendo **evidencia positiva** de una sonda/objetivo real en vez de
solo la ausencia de un error - el propio código de salida de
STM32CubeProgrammer no es una señal fiable de éxito/fallo por sí solo,
así que una comprobación de conexión dedicada (`pyocd list --probes`, o
un `-c port=SWD` de solo conexión para STM32CubeProgrammer) se ejecuta
antes de que el paso de borrado masivo lo haga jamás. La salida de cada
comando posterior también se examina en busca de texto de fallo conocido
como una segunda capa, por si el código de salida de una herramienta por
sí solo no fuera fiable en alguna otra situación tampoco.

**El simulacro (dry run) está activado por defecto.** La primera vez,
déjalo marcado y pulsa "Flash Complete Chip" - imprime los comandos
exactos en el registro sin tocar la placa. Léelos, confirma que las
rutas y direcciones parecen correctas, *luego* desmarca el simulacro y
hazlo de verdad.

**"Back up entire flash before erasing"** lee toda la región de flash de
256KB a un archivo `.bin` primero, mediante el propio comando de
lectura-de-memoria-a-archivo de la misma herramienta (`-r` para
STM32CubeProgrammer, `commander savemem` para pyOCD) - un seguro real
aquí, ya que a diferencia de una actualización CAN OTA (que el slot de
respaldo de imagen dorada ya protege), un borrado completo de chip no
tiene otro deshacer. Desactivado por defecto ya que añade 10-30s y no es
necesario en un chip nuevo/en blanco; vale la pena marcarlo antes de
sobrescribir una placa que ya está ejecutando algo. Si la lectura
realmente no produce un archivo, el borrado se rechaza en vez de
proceder sin el respaldo que pediste.

**Estado de las pruebas:** la lógica de comprobación de conexión de
arriba se verificó contra salida real de STM32CubeProgrammer (tanto un
registro genuino de conexión exitosa como un fallo documentado de "No
target connected", ambos obtenidos del propio foro comunitario de ST) y
contra el escenario exacto de falso-éxito que encontró un usuario real.
La secuencia completa de borrado/programación/verificación contra un
ST-Link real y un STM32F303CC real todavía no se ha ejercitado de
principio a fin - el entorno que escribió esto no tiene acceso USB. Trata
un primer intento real completo con la precaución apropiada - una placa
de repuesto/prueba primero si tienes una, y ten en mente un plan de
respaldo (la propia herramienta de flasheo de STM32CubeIDE, o
`st-flash`) por si algo de tu versión específica de pyOCD o sonda no
coincide con lo que se asume aquí.

## 7. Modo CLI (sin interfaz gráfica)

Para pipelines de CI, bancos de pruebas, o scripting de línea de
producción donde no hay pantalla:

```
python3 urtc_flasher.py --cli --port /dev/ttyACM0 --file firmware.bin
```

```
usage: urtc_flasher.py --cli [-h] [--transport {serial,socketcan}] --port PORT
                             --file FILE [--no-trigger] [--force]
```

Códigos de salida: `0` éxito, `1` error de protocolo/conexión, `2`
argumentos incorrectos o un archivo de firmware que falla la validación
(pasa `--force` para flashearlo de todas formas), `130` cancelado con
Ctrl+C. Solo cubre la ruta de actualización CAN OTA (secciones 1-3) - la
ruta completa SWD/JTAG del chip es deliberadamente solo-GUI por ahora,
dado lo mucho más que está en juego si una ejecución con script obtiene
una combinación equivocada de archivo/objetivo sin que nadie esté
observando.

**`--transport mock`** ejecuta toda la secuencia de actualización contra
un bootloader simulado, en memoria, en vez de una placa real - sin
adaptador, sin puerto, nada físico involucrado:

```
python3 urtc_flasher.py --cli --transport mock --file firmware.bin --no-trigger
```

Útil para probar la lógica propia de esta herramienta (comportamiento de
reintentos, manejo de timeouts, códigos de salida) en un pipeline de CI
o antes de tocar hardware real - no algo que hable con una placa real.
`--mock-fail 0x03` (o cualquier otro valor `VERIFY_FAIL_REASON_*` de
`docs/CANBUS.TXT`) hace que la actualización simulada falle la
verificación en vez de tener éxito, para probar la ruta de fallo de la
misma manera.

## 8. Fiabilidad durante una actualización CAN, y registros de sesión

Si el ACK de una página no llega dentro de la ventana normal de 3s
durante una actualización CAN, la herramienta reintenta la *espera* (no
un reenvío de los datos de la página) hasta dos veces más con una breve
espera creciente antes de rendirse, recuperándose de un ACK que se
retrasó o se perdió en un bus ruidoso sin que los datos subyacentes se
hayan perdido. Deliberadamente no reenvía los datos de la página en un
timeout - si los datos originales realmente llegaron bien y solo se
perdió el ACK, reenviar haría que el bootloader leyera esos bytes como
el inicio de la *siguiente* página, desincronizando la transferencia.
Cada reintento también comprueba el propio latido del bootloader
(enviado aproximadamente una vez por segundo) contra lo que implicaría
haber recibido la página actual por completo - cuando son consistentes,
el registro lo dice, lo cual es evidencia real de que los datos pasaron
y solo se perdió el ACK, no solo una espera más larga y una esperanza.

Cada sesión también escribe un archivo de registro con marca de tiempo
en `tools/flasher/V1.1/logs/` (`urtc_flasher_YYYYMMDD_HHMMSS.log`),
independiente del registro en pantalla - útil para entregarle una traza
completa a quien escribió el firmware si algo sale mal en el campo. Esta
carpeta se crea automáticamente y es segura de borrar; nada vuelve a
leer registros antiguos.

## 9. Diagnósticos — actividad de bus, bitrate, y paquetes de depuración

**Selector de bitrate + autodetección** (solo Serie/SLCAN): el bus de
URTC está fijo a 500 kbit/s, que sigue siendo el valor por defecto -
esto es para un adaptador mal configurado o para depurar una placa no
estándar. **Autodetección** prueba cada bitrate SLCAN estándar por turno
contra una consulta de versión y se detiene en el primero que obtiene
una respuesta real; no conectado todavía cuando haces clic en él. El
bitrate de SocketCAN se establece a nivel del sistema operativo
(`ip link`), así que este control está deshabilitado para ese
transporte - no hay nada aquí para que lo intente.

**Actividad de bus** ("Comprobar (2s)", junto a Consultar): cuenta
tramas de protocolo reales realmente vistas durante una ventana fija de
2 segundos en el transporte que esté conectado. Esto deliberadamente
**no** es lo mismo que un porcentaje real de carga de bus CAN o los
propios contadores de error del controlador (REC/TEC) - esos necesitan
una consulta netlink (SocketCAN) o extensiones específicas del adaptador
(SLCAN) que esta herramienta no tiene una forma estándar y sin
dependencias de obtener. Lo que sí da: una señal genuina, directamente
medida, de "hay algo hablando en este bus, y aproximadamente con qué
frecuencia", en cualquiera de los 2 transportes. Para SocketCAN en
concreto, también muestra el delta de 2 segundos de las propias
estadísticas de interfaz de Linux
(`/sys/class/net/<iface>/statistics/`) - contadores básicos de
rx/tx/error/drop que expone toda interfaz, leídos como archivos planos,
sin dependencia extra. Conectar por SocketCAN también lee
`/sys/class/net/<iface>/carrier` - un archivo plano 0/1 que expone toda
interfaz de Linux. Cuando un controlador CAN entra en bus-off, el driver
del kernel llama a `netif_carrier_off()`, así que "sin portadora" aquí
es evidencia real de un bus-off o un enlace igualmente muerto, registrado
como advertencia con el comando exacto de recuperación (`sudo ip link
set <iface> down && sudo ip link set <iface> up type can bitrate 500000
restart-ms 100`). Esta herramienta no ejecuta ese comando por sí misma -
limpiar un bus-off real necesita bajar y volver a subir la interfaz a
nivel del kernel, lo cual necesita root y cuenta como cambiar la
configuración de red del sistema, no algo para hacer silenciosamente en
tu nombre.

**Exportar paquete de depuración** (arriba del registro): guarda un
`.zip` con el registro actual en pantalla, diagnósticos básicos del
sistema (SO, versión de Python, qué herramientas se encontraron,
transporte/puerto/bitrate actual), y el archivo de firmware CAN
seleccionado actualmente - útil para entregar un panorama completo a
quien escribió el firmware si algo sale mal en el campo, en vez de
copiar el registro
por mano.

## 10. SWD/JTAG — formatos de archivo, verificación de slot, y selección de sonda

**Formatos de archivo**: los selectores de bootloader/aplicación de la
sección SWD aceptan `.bin`, `.hex`, y `.elf`/`.axf`. ELF/AXF se analiza
con una pequeña cantidad de desempaquetado de estructuras escrito a mano
(solo cabecera ELF + cabeceras de programa - sin símbolos, sin cabeceras
de sección), deliberadamente sin usar `pyelftools`: este proyecto se
mantiene con cero dependencias fuera de la biblioteca estándar, y el
análisis ELF completo es más de lo que necesita esta comprobación de
plausibilidad específica. Verificado contra el propio
`BOOTLOADER.elf`/`APP.elf` compilado real de este proyecto - ambos
validan correctamente en sus direcciones de carga reales
(`0x08000000`/`0x08008000`), no solo archivos de prueba sintéticos. Solo
ARM little-endian de 32 bits, que es todo lo que un objetivo Cortex-M es
alguna vez. El tamaño declarado de un archivo `.hex` es el recuento real
de bytes ocupados, no el rango de direcciones desde su registro más bajo
hasta el más alto - así que un archivo disperso (un pequeño bloque de
firmware real más un bloque distante y separado de option bytes o datos
de calibración, que algunos toolchains de STM32 agrupan en una sola
exportación) valida sobre su contenido real en vez de sobre el hueco
entre ellos. Una imagen de firmware en bruto bajo una extensión que no
sea `.bin` (`.img`, `.rom`, o sin extensión en absoluto - seleccionable
mediante la opción "Todos los archivos" del selector de archivos) obtiene
su dirección base de en qué slot la estés cargando, igual que lo haría
un `.bin`.

**Verificación de slot de bootloader/aplicación**: los selectores de
archivo verifican que cada imagen está pensada para el slot en el que se
está poniendo, no solo que parece firmware válido de *algún* tipo. Una
imagen de bootloader y una de aplicación tienen un puntero de pila
igualmente plausible - mismo chip, misma RAM - así que esa comprobación
por sí sola no puede distinguirlas si una termina en el slot de la otra.
Lo que sí puede: el **manejador de reset** de una imagen enlazada es una
dirección absoluta y real fijada en tiempo de enlazado, y solo apunta
dentro de la región para la que realmente se enlazó. Verificado contra
el propio `BOOTLOADER.bin`/`APP.bin` compilado real de este proyecto:
sus manejadores de reset son `0x080030F1` y `0x0800C725`
respectivamente, cada uno correctamente dentro del rango de direcciones
de su propio slot y fuera del de la otra - así que poner cualquiera de
las 2 en el slot equivocado se detecta y se bloquea, no se acepta en
silencio. Se aplica la misma lógica a `.hex`/`.elf`, comprobados contra
su propia dirección de carga embebida en su lugar.

**Comprobar Option Bytes** (sección 4, solo STM32CubeProgrammer -
pyOCD no expone esto de la misma forma por CLI): un volcado de solo
lectura `-ob displ`, sin borrado/escritura. Marca el nivel RDP con el
mismo cuidado que toma toda esta herramienta en torno al riesgo de SWD:
- **RDP0** - sin protección, normal para una placa de desarrollo.
- **RDP1** - reversible mediante el Read Unprotect de CubeProgrammer,
  pero eso borra el chip por completo como parte de quitarlo - no algo
  que esta herramienta haga por ti automáticamente.
- **RDP2** - el único bloqueo genuinamente **permanente** de todo este
  proyecto. A diferencia de cualquier otro riesgo documentado arriba
  (todos recuperables por SWD), RDP2 deshabilita el puerto de depuración
  para siempre por diseño del propio ST. Esta comprobación existe para
  detectarlo antes de una operación de chip completo, no después.

**Selección de sonda** (sección 4): si hay más de un ST-Link/sonda
conectado a la vez, todo comando exige elegir uno explícitamente del
desplegable de Sonda - no hay "el que sea que el sistema operativo
enumere primero". Con exactamente una sonda conectada, se auto-selecciona;
con cero o varias, pulsa Actualizar y elige. Esto se aplica tanto al
flasheo de chip completo como a la comprobación de option bytes, ya que
ambos son lo bastante cercanos a destructivos como para que adivinar la
placa equivocada sea un riesgo real en un banco con varios dispositivos.

**Las escrituras de pyOCD se verifican con una relectura explícita**,
no solo se confía en el código de salida. El propio comando `flash` de
pyOCD se salta reescribir páginas que ya coinciden (una optimización de
velocidad, no un informe de verificación), así que esta herramienta
añade un paso `commander compare` contra ambas imágenes tras escribir -
una comprobación genuina byte a byte, igual que ya hace el flag `-v` de
STM32CubeProgrammer. Solo para `.bin`: `compare` comprueba el contenido
de la flash contra los bytes en bruto del archivo, lo cual no
coincidiría correctamente con la propia codificación de un archivo
`.hex`/`.elf` incluso tras un flasheo exitoso, así que esos 2 formatos
se saltan este paso específico y confían en la propia verificación
interna en tiempo de escritura de pyOCD en su lugar.

## 11. Telemetría de transferencia y detalle de fallo de verificación

**Telemetría de transferencia**: el registro muestra los KB/s efectivos
y el tiempo transcurrido por página durante una actualización CAN, más
una línea de resumen al final (tiempo total, KB/s promedio, cuántos
reintentos de ACK de página ocurrieron). Puramente informativo - no
cambia el comportamiento de flasheo, solo hace más fácil distinguir de
un vistazo "esto simplemente va lento" de "algo realmente va mal".

**Motivos específicos de fallo de verificación**: si la verificación
falla durante una actualización CAN, `BOOTLOADER.C` envía un byte de
motivo junto al estado `0x05` (verificación fallida) - transferencia
incompleta, desajuste de CRC32, desajuste de HMAC, o desajuste de
HardwareID, en vez de que todo fallo parezca idéntico. Ver
`docs/CANBUS.TXT` para el formato exacto de trama (`0x7F5`, DLC 2 para
este estado específico). Esta herramienta y el bootloader coinciden en
este formato de trama, así que flashea ambos juntos si estás
construyendo un bootloader personalizado con una versión distinta del
protocolo.

## 12. Borrado opcional de la F-RAM antes de flashear

La sección 3 tiene una casilla, **"También borrar la F-RAM de
persistencia antes de flashear"** - desactivada por defecto. Si está
marcada, envía el comando de borrado con payload mágico (`0x192` - ver
`docs/CANBUS.TXT`) a la F-RAM de persistencia FM24CL64B de la placa
antes de que empiece la secuencia de actualización, borrando cualquier
estado de parámetros de herramienta que tuviera guardado.

**No es necesario para una actualización normal.** Un desajuste de
versión en el propio formato del registro guardado ya se detecta y se
ignora de forma segura en el siguiente arranque (ver la sección de
persistencia de parámetros de `src/F303-master/V1.1/README.md`) - esta
casilla existe para una limpieza genuinamente completa, no porque
saltársela vaya a dejar algo roto.

**Solo funciona mientras la aplicación está en ejecución** - el propio
bootloader no maneja `0x192` en absoluto, solo lo hace `STM32F303CC.C`.
Esta casilla se salta en silencio (con una línea de registro explicando
por qué) si la casilla "la placa está ejecutando actualmente la
aplicación" de arriba no está marcada, ya que en ese caso se asume que
la placa ya está en el bootloader.

**Una confirmación ausente no detiene el flasheo.** Si la propia trama
de confirmación del comando de borrado no vuelve dentro de 2 segundos,
esto se registra como una advertencia y la actualización de firmware
real procede de todas formas - borrar es un paso secundario y opcional
junto al propósito real de esta herramienta, no algo que deba abortar
una actualización por lo demás exitosa por la ausencia de su propia
trama de confirmación. Comprueba el estado de la F-RAM por separado (el
propio botón Consultar Estado de `URTC Tester`) si eso te importa.

## Cambiar la clave HMAC / el HardwareID

La clave de firma compartida vive en 2 lugares que siempre deben
coincidir: el array `HMAC_KEY` de `BOOTLOADER.C`, y la constante
`HMAC_KEY` de esta herramienta cerca del principio de
`urtc_flasher.py`. Si cambias una, cambia la otra y reconstruye/reflashea
el bootloader antes de intentar firmar nada con la clave nueva - una
imagen firmada con una clave que el bootloader no tiene siempre fallará
la verificación, de forma segura, dejando el slot principal intacto.

**O sustituye cualquiera de esto sin tocar el script:** un
`urtc_config.json` opcional junto a `firmware/` puede establecer la
clave de firma, el HardwareID, y los valores del mapa de memoria - útil
para una revisión de placa distinta, una clave rotada, o (para los
campos del mapa de memoria) adaptar esta herramienta a una variante de
chip o esquema de particiones distinto, sin necesitar una versión de
script nueva por despliegue:
```json
{
  "hardware_id": "0x0303CC01",
  "hmac_key_hex": "555254432D4859445241...",
  "app_max_size": 114688,
  "bootloader_max_size": 32768,
  "flash_page_size": 2048,
  "bootloader_flash_addr": "0x08000000",
  "app_flash_addr": "0x08008000"
}
```
Todo campo es opcional - solo sustituye lo que realmente esté
cambiando. Un archivo ausente vuelve en silencio a los valores por
defecto compilados; un archivo presente pero roto registra una
advertencia y también vuelve a esos valores, en vez de hacer fallar la
herramienta por una errata. Se registra qué fuente está activa al
arrancar, así que siempre es visible qué valores usó realmente una
sesión dada. `hardware_id` acepta tanto una cadena JSON (`"0x0303CC01"`)
como un número JSON plano (`50580689`) - lo que sea más natural según
cómo se genere el archivo. `app_max_size`, `bootloader_max_size`,
`flash_page_size`, `bootloader_flash_addr`, y `app_flash_addr` también
se pueden sustituir aquí, junto a la clave de firma y el HardwareID de
arriba - útil si esta herramienta alguna vez se adapta a una variante de
chip o esquema de particiones distinto.
