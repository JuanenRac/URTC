<p align="center">
  <img src="/images/URTC_LOGO_TESTER.svg" alt="URTC Tester Logo" width="100%">
</p>

# URTC Tester (Windows / Linux)

**Versión:** 1.1 · **Autor:** JuanenRac (Electro Hobby 3D) &lt;electrohobby3d@gmail.com&gt;

Licencia: **GPL-3.0**, la misma que el firmware URTC y la herramienta de
flasheo - ver `LICENSE` en la raíz del repositorio.

Un ejercitador en vivo de bus CAN para la placa URTC. Se conecta por el
mismo adaptador USB-CAN que usa el flasher, le pregunta a la placa para
cuál de sus 12 perfiles de herramienta está configurada actualmente
mediante jumpers, y muestra solo los controles y la telemetría propios
de esa herramienta - no una sola ventana intentando representar las 12 a
la vez. Todo lo que hace es un comando en tiempo de ejecución o una
lectura de telemetría contra la aplicación en ejecución; nunca toca la
flash, así que no hay nada aquí que pueda dejar la placa menos operativa
de lo que estaba al empezar.

## 1. Relación con el flasher

Esta herramienta y `tools/flasher/V1.1/` comparten la misma capa de
transporte (las clases SLCAN y SocketCAN son idénticas) ya que ambas en
última instancia solo necesitan meter y sacar tramas CAN del mismo tipo
de adaptador, pero hacen trabajos fundamentalmente distintos:

| | Flasher | Tester |
|---|---|---|
| Toca la flash | Sí (ese es todo el propósito) | Nunca |
| Habla con | El bootloader, mayormente | La aplicación en ejecución |
| Propósito | Actualizar firmware | Ejercitar/verificar el hardware real de un cabezal de herramienta |

Si no estás seguro de cuál necesitas: si la placa ya está ejecutando
firmware y quieres comprobar que una herramienta realmente funciona (el
calentador calienta, el motor gira, el LED se enciende), quieres esta.

## 2. Instalar y ejecutar

Mismo patrón que el flasher:

```
cd tools/tester
pip install -r requirements.txt
python urtc_tester.py          # Windows
python3 urtc_tester.py         # Linux
```

O compila un binario independiente: `build_exe.bat` en Windows,
`./build_exe.sh` en Linux. Ambos limpian `build/`/`dist/` primero y
empaquetan `assets/` (el banner y el icono) en el ejecutable - ver el
propio README del flasher para el razonamiento completo detrás de estos
scripts, ya que se aplica idénticamente aquí.

**Al arrancar**, el banner se muestra centrado en pantalla durante 5
segundos antes de que aparezca la ventana principal, en vez de vivir
dentro de la ventana misma - igual que el flasher, y por el mismo motivo
(mantiene la ventana en sí compacta). El icono de ventana/barra de
tareas es igualmente un diseño pequeño independiente, no el banner
reducido.

### Barra de menú

- **Archivo** - Guardar registros (el registro en pantalla como texto
  plano; para un paquete más completo que incluya diagnósticos del
  sistema, ver "Registros y paquetes de depuración" más abajo), y
  Salir.
- **Idioma** - cambia entre los 5 idiomas disponibles (ver "Idioma" más
  arriba para saber cómo funcionan las traducciones).
- **Ayuda** - Readme (abre este archivo en una ventana de solo lectura;
  recoge automáticamente una versión traducida en cuanto exista una para
  el idioma actual), GitHub de URTC (abre el repositorio del proyecto en
  tu navegador), Licencia (la licencia GPL-3.0 de esta herramienta,
  leída desde el propio archivo `LICENSE` del repositorio), y Acerca de
  (versión y autor).

### Estructura de archivos

Esta herramienta está organizada en módulos por responsabilidad,
puramente por legibilidad - no hay ninguna diferencia funcional entre
tenerlos como archivos separados o como uno grande. `tester_config.py`
contiene las constantes de configuración/idioma/protocolo,
`tester_transports.py` contiene SLCAN/SocketCAN, `tester_bus_monitor.py`
contiene el hilo de lectura CAN en segundo plano, y `TesterGUI` misma
está dividida entre `tester_gui_core.py` (conexión, detección, ciclo de
vida de la ventana, y la barra de menú) más 3 mixins que combina:
`tester_common_panels.py` (paneles de controles globales/F-RAM/
expansión/autoprueba/monitor de bus/trama personalizada),
`tester_panel_helpers.py` (utilidades compartidas que usa todo
constructor de panel de herramienta), y `tester_tool_panels.py` (los 8
constructores de paneles específicos de herramienta). `urtc_tester.py`
ahora es solo el punto de entrada - arranque sin CLI y la pantalla de
bienvenida.

**Idioma**: inglés por defecto. Se cambia mediante el menú **Idioma**
(en la barra de menú en la parte superior de la ventana) en vez de un
desplegable en la ventana principal - cambia la interfaz (etiquetas,
botones, diálogos, y mensajes de registro) a cualquiera de los 5 idiomas
disponibles, se guarda inmediatamente en `config.json` junto a esta
herramienta, y se aplica en el siguiente arranque. Las traducciones
viven en archivos de texto plano bajo `language/` (`english.lng`,
`spanish.lng`, `italian.lng`, `french.lng`, `german.lng`) como pares
simples `CLAVE=Valor`, uno por línea - las líneas que empiezan con `#` y
las líneas en blanco se ignoran, y un `\n` literal dentro de un valor se
convierte en un salto de línea real (usado por el puñado de mensajes de
diálogo multilínea). Editable directamente si una traducción necesita
corregirse, o como punto de partida para otro idioma (añade
`language/<nombre>.lng`, añade `("<nombre>", "Nombre Nativo")` a
`AVAILABLE_LANGUAGES` cerca del principio de `tester_config.py`, y pon
`"language": "<nombre>"` en `config.json`). Una clave que falte en un
archivo de idioma cae de vuelta a mostrar el nombre de esa misma clave
en vez de fallar, y un archivo de idioma ausente o ilegible (edición
defectuosa, nombre de archivo equivocado) cae de vuelta al inglés para
toda la interfaz - de cualquier forma la herramienta se mantiene usable
mientras se resuelve el desajuste.

**Configuración de SLCAN/SocketCAN en Linux** (reflasheo del adaptador,
permisos de serie, levantar la interfaz con `ip link`) es exactamente
igual que la sección 1 del flasher - ver
`tools/flasher/V1.1/README.md` secciones 1 y 2 en vez de duplicarlo
aquí.

## 3. Cómo funciona

La ventana está distribuida en 3 columnas: izquierda y centro contienen
las secciones siempre visibles de abajo (1-4, luego 6), la derecha
contiene el panel por herramienta de la sección 5, que es la única
parte de la ventana que realmente cambia según lo que se detecte. Dividir
las secciones siempre visibles en 2 columnas en vez de apilarlas todas
en una mantiene la ventana sin crecer tanto en altura como para no caber
en una pantalla ordinaria, a medida que se fueron añadiendo más de estas
secciones con el tiempo. El propio panel de la impresora 3D (el más
alto de los 12) va un paso más allá y divide sus propios controles en 2
subcolumnas internamente, por el mismo motivo.

**Conectar** (sección 1, idéntica al flasher): elige Serie/SLCAN o
SocketCAN, el puerto/interfaz, opcionalmente autodetecta el bitrate,
luego Conectar.

**La detección ocurre automáticamente al conectar** (o haz clic en
**Detectar** para repetirla): la herramienta envía `0x110` (consulta
herramienta activa) y `0x7F8` (consulta versión), y usa la respuesta
para:
- Mostrar cuál de los 12 perfiles de herramienta está activo, y el
  estado general de la placa (cualquier error declarado, fallo de bus
  CAN, todavía en la pantalla de arranque).
- Mostrar el HardwareID y la versión de firmware que reporta, marcando
  un desajuste si no coincide con el propio `THIS_HARDWARE_ID` de este
  proyecto.
- Construir el panel de **Controles de Herramienta** a la derecha para
  esa herramienta específica - y solo esa herramienta. Cambiar qué
  herramienta está jumpeada y detectar de nuevo desmonta el panel viejo
  y construye el nuevo desde cero.

**Controles Globales** (sección 2, siempre visible sin importar qué
herramienta esté activa): el override de color del LED de estado, el
color y encendido/apagado del anillo LED, y el modo de pantalla OLED
(`0x100`) - estos aplican a toda herramienta, así que no se mueven al
panel dinámico. En el modo Inspección AOI específicamente, el
encendido/apagado del anillo aquí se ignora en favor del propio control
de estrobo de esa herramienta (según `docs/CANBUS.TXT`) - el color sigue
aplicando de cualquier forma.

**Placa de Expansión** (sección 3, siempre visible): el propio bus SPI
de `CONN_EXPANSION` y la línea DIAG0 - nada más vive en este conector
hoy -

**F-RAM de Persistencia** (sección 4, también siempre visible, pero
deliberadamente separada de Placa de Expansión de arriba): la FM24CL64B
comparte el propio bus I2C2 hardware con el OLED - un componente central
de la placa, no algo cableado a `CONN_EXPANSION` en absoluto. Agrupar
las 2 juntas implicaría una conexión entre ellas que no es real - el
propio conector de expansión no tiene F-RAM, ni EEPROM, nada no volátil
en él.
- **Paso directo SPI**: escribe bytes hex separados por espacios (1-7 de
  ellos, p. ej. `01 02 03`), pulsa Enviar, y ve exactamente qué volvió
  por MISO durante esa misma transferencia (`0x180`/`0x181`) - un
  transporte de bytes en bruto, no consciente de los registros TMC5160,
  igual que el propio enfoque del firmware. Útil para ejercitar el bus
  en sí antes de que valga la pena construir un panel dedicado para el
  protocolo de registros de una placa de expansión específica.
- **Nivel DIAG0**: **Consultar DIAG0** lee el estado actual de la línea
  de diagnóstico de stall/fallo de un TMC5160 (`0x182`/`0x183`) - HIGH
  (inactivo) o LOW (activado). Una lectura sondeada simple, no un valor
  en vivo/empujado - pulsa el botón de nuevo para actualizarlo.
- **F-RAM de Persistencia**: **Consultar Estado** lee de vuelta lo que
  la placa guardó por última vez antes de una pérdida de energía
  (`0x190`/`0x191`) - qué herramienta era, el setpoint, si había un
  error crítico activo en ese momento. **Borrar F-RAM...** la borra
  (`0x192`, con un diálogo de confirmación primero - esto no se puede
  deshacer).
- **Tipo de placa de expansión**: **Consultar** muestra cuál de las 5
  configuraciones posibles de `CONN_EXPANSION` está establecida
  actualmente (`0x1A1` - ver `EXPANSION.TXT`). Solo lectura aquí -
  establécela desde la propia sección CAN OTA de `URTC Flasher` en su
  lugar, ya que es un paso de configuración de hardware de una sola vez,
  no algo para cambiar casualmente desde una herramienta de diagnóstico
  en vivo.
- **Configuración libre de herramienta**: **Consultar** muestra la
  lectura cruda de los jumpers ID (0-31) junto a lo que dice
  actualmente el registro `free_tool_selection` de la F-RAM (`0x1A3` -
  ver `EEPROM.TXT` sección 5) - solo realmente consultado por una placa
  cuyos jumpers lean 0x1F/11111b. Solo lectura aquí, mismo razonamiento
  que el tipo de placa de expansión de arriba - `URTC Flasher` es la
  única herramienta que lo escribe.
- **Tipo de periférico y número de serie**: **Consultar** muestra el
  tipo de periférico fijo (siempre URTC/0x03) junto al número de serie
  del dispositivo establecido actualmente (`0x1A5` - ver `EEPROM.TXT`
  sección 6), una etiqueta asignada por el usuario para distinguir
  varias placas por lo demás idénticas en el mismo bus CAN. Solo lectura
  aquí también - `URTC Flasher` escribe el número de serie, esta
  herramienta solo lo lee de vuelta.

**Trama CAN Personalizada** (sección 6, también siempre visible): una
entrada de ID en bruto + bytes hex con envío de una vez y periódico -
para un comando que aún no tiene su propio control aquí, o para probar
algo no (o aún no) documentado en `docs/CANBUS.TXT`. Sin validación más
allá del rango de ID y DLC≤8; lo que sea que esto envíe es exactamente
lo que va al bus. La misma sección también abre el **Monitor de Bus en
Bruto** (ver abajo).

**Ejecutar Autoprueba** (junto a Detectar): ejecuta un pequeño conjunto
de comprobaciones de comunicación seguras y en reposo para la
herramienta que esté actualmente detectada - confirma que tanto la
consulta de herramienta activa como la de versión responden, luego
(para herramientas con telemetría) envía un setpoint/velocidad/potencia
seguro de 0 y comprueba que llega la telemetría esperada.
Deliberadamente nunca envía nada que realmente caliente, dispare, o
gire a potencia significativa - esto verifica que el ida y vuelta de
comunicación funciona, no que un actuador responda físicamente, ya que
confirmar eso necesita a un humano observando de todas formas. Pide
confirmación antes de enviar nada. Las herramientas sin telemetría
(movimiento simple) o que son puramente dirigidas por eventos (sonda de
escaneo) reciben una nota solo informativa en vez de un aprobado/fallido
real.

**Gráficos de temperatura en vivo**: tanto los paneles del soldador
como de la boquilla de la impresora 3D muestran un pequeño gráfico de
línea deslizante junto a su lectura de temperatura en vivo - un widget
Canvas de Tkinter simple, no una dependencia nueva (matplotlib/
pyqtgraph romperían la política de cero dependencias de esta herramienta
más allá de pyserial). Escala de eje Y fija (0 hasta el techo de
setpoint de esa herramienta) en vez de auto-escalado, así que la
tendencia es fácil de leer de un vistazo en vez de que la escala se
desplace por debajo.

**Monitor de Bus en Bruto** (abierto desde la sección Trama CAN
Personalizada): una ventana separada mostrando cada trama vista,
cualquier ID, independiente del panel de herramienta activo - una tabla
que se desplaza en vivo (Tiempo/ID/DLC/Datos/Δt), Pausar/Limpiar, y una
lectura aproximada de carga de bus/tasa de tramas (actualizada una vez
por segundo; la cifra de carga no modela la sobrecarga de bit-stuffing,
así que trátala como una cifra diagnóstica aproximada, no una medición
certificada). **Exportar .trc...**/**Exportar .asc...** guardan la
tabla mostrada actualmente como un archivo de traza simplificado al
estilo PEAK PCAN-View / Vector CANalyzer respectivamente - lo bastante
cercano como para ser legible por la mayoría de herramientas que esperan
esos formatos, no garantizado byte-idéntico a lo que producen las
aplicaciones reales. Si `urtc_custom_ids.json` existe junto a este
script (opcional, no incluido por defecto - `{"0x199": "My Sensor"}`),
la columna ID muestra ese nombre amigable junto al ID hex en bruto -
útil para cualquiera que pruebe el propio tráfico de una placa de
expansión personalizada sin necesitar modificar el código fuente de
esta herramienta.

## 4. Cobertura de herramientas

Cada uno de los 12 perfiles tiene su propio panel, construido
directamente a partir de `docs/CANBUS.TXT`:

| Herramienta | Controles | Telemetría en vivo |
|---|---|---|
| Soldador | Temperatura de setpoint, encendido/apagado | Temperatura real, endstop |
| Dispensador de Pasta/Líquido, Destornillador, ambos Grippers | Dirección + cuenta de pasos (movimiento de una vez) | ninguna (0x120 compartido, sin telemetría para ninguna de estas 5) |
| Recogida por Vacío | ninguno | Lectura analógica, pieza detectada |
| Taladro | Velocidad + dirección | RPM real, endstop |
| Inspección AOI | Modo de anillo (apagado/estrobo/continuo) + período de estrobo | Endstop |
| Grabador Láser | Potencia + armado/seguro del interlock | Endstop |
| Impresora 3D | Setpoint de boquilla, dirección/pasos del extrusor, potencia del ventilador de capa, potencia del ventilador de hotend | Temperatura de hotend, RPM del ventilador de capa, RPM del ventilador de hotend |
| Sonda de Escaneo | ninguno | Cuenta de eventos de impacto + marca de tiempo (`0x095` de máxima prioridad) |

**Los watchdogs de comunicación se manejan por ti.** El soldador, el
láser, y la boquilla de la impresora 3D tienen cada uno un watchdog de
250ms en el firmware; el ventilador de capa tiene uno de 1000ms. Marcar
la casilla "Activo" correspondiente no solo envía el comando una vez -
lo reenvía automáticamente (150ms para las herramientas con watchdog de
250ms, 400ms para el ventilador de capa) mientras la casilla siga
marcada, de la misma forma en que un controlador maestro real tiene que
hacerlo. Desmarcarla envía una única trama de cero/apagado y para. El
ventilador de hotend no tiene watchdog (un detector de estancamiento en
su lugar - ver `docs/CANBUS.TXT`), así que es un envío simple de una
vez.

## 5. Registros y paquetes de depuración

Igual que el flasher: un registro de sesión con marca de tiempo se
escribe automáticamente en `tools/tester/V1.1/logs/` (seguro de borrar),
y **Exportar Paquete de Depuración** guarda un `.zip` con el registro
actual en pantalla más diagnósticos básicos del sistema (SO, versión de
Python, transporte/puerto/bitrate actual, herramienta detectada) para
entregar a quien esté depurando un problema de cabezal de herramienta.

## 6. Limitaciones conocidas

- **No probado contra hardware real.** Cada pieza aquí - la capa de
  transporte, el manejo de ID CAN/disposición de bytes, la temporización
  de keepalive del watchdog - se comprobó de forma aislada (tramas
  simuladas, un subproceso real para la temporización donde aplica)
  pero el entorno que construyó esto no tiene acceso USB. Trata una
  primera sesión real con la misma precaución que pide el propio README
  del flasher.
- **Un panel de herramienta a la vez, por diseño**, no una limitación
  actual a eliminar más adelante - ver la introducción de arriba para
  el motivo.
- **Los colores globales de LED son un override directo**, no una
  lectura en vivo - no hay telemetría de qué están mostrando realmente
  en este momento los LEDs de estado/anillo, solo lo que se ordenó por
  última vez.
