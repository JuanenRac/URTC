<p align="center">
  <img src="images/URTC_LOGO.svg" alt="URTC Logo" width="100%">
</p>

# 🚀 URTC — Universal Robot Tool Controller (v1.1)

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  🇪🇸 <b>Español</b> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a>
</p>


<p align="left">
  <img src="https://img.shields.io/badge/Licencia-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Hardware-CERN%20OHL--S-orange.svg" alt="CERN OHL-S">
  <img src="https://img.shields.io/badge/Lenguaje-C-00599C.svg" alt="C">
  <img src="https://img.shields.io/badge/Plataforma-STM32F303-003551.svg" alt="STM32">
  <img src="https://img.shields.io/badge/Bus-CAN-yellow.svg" alt="CAN">
</p>


> **⚠️ Aviso de seguridad:** esta placa controla un **diodo láser de grabado de 10W** y varias etapas de calentamiento (cartucho de soldador T12, hotend de impresora 3D). Construirla y usarla implica trabajar con equipo que puede causar **quemaduras, incendios o daño ocular** si se ensambla o se opera sin las medidas de seguridad adecuadas (gafas láser calificadas para la longitud de onda del diodo, protección térmica, un corte de energía accesible). Este es un proyecto hobbyista/maker compartido tal cual - constrúyelo y úsalo bajo tu propio riesgo, y no te saltes las prácticas básicas de seguridad solo porque el firmware tenga watchdogs.

¡Hola a todos! Quería compartir un proyecto que he estado desarrollando llamado URTC (Universal Robot Tool Controller). Es una placa de control monolítica y altamente integrada, diseñada específicamente para expandir las capacidades de brazos robóticos y configuraciones de automatización, lo que la convierte en una pareja perfecta para plataformas como PAROL6 y Faze4 — dos brazos robóticos de código abierto diseñados y desarrollados por [Source-Robotics](https://source-robotics.com/) ([GitHub](https://github.com/Source-Robotics)).

**URTC es un proyecto independiente y no oficial.** No está desarrollado ni respaldado por Source-Robotics — es un controlador de cabezal de herramienta compatible, construido para funcionar bien con PAROL6 y Faze4, y la misma arquitectura basada en CAN está abierta para adaptarse también a otras plataformas de brazos robóticos.

Aquí está el desglose completo de qué es, qué hace, y el ecosistema de hardware que gestiona actualmente.

**Estado: 🚧 Proyecto en evolución activa — todavía sin Release.** URTC está en desarrollo continuo y activo en ambos frentes a la vez: firmware (nuevos perfiles de herramienta, el ecosistema de esclavos de expansión, cambios de protocolo) y hardware (el esquemático y la BOM todavía se están finalizando, todavía no existe ninguna placa poblada). Como ambos lados se mueven juntos, lo que hay en este repositorio en un momento dado es una instantánea de trabajo en curso, no un producto estable y versionado - los nombres de archivo, la estructura de carpetas, el número de herramientas, y la documentación pueden todos cambiar todavía mientras el diseño se asienta. Una vez que tanto el firmware como el hardware alcancen un estado genuinamente estable y verificado en hardware real, se etiquetará un **Release** propiamente dicho que agrupe todo (firmware, bootloader, herramientas de PC, archivos de diseño de hardware, y documentación) como una instantánea coherente y congelada. Hasta entonces, trata `main` como el objetivo activamente cambiante que es.

---

## ⚙️ ¿Qué es URTC?

URTC es una placa de control todo-en-uno y compacta, impulsada por un microcontrolador STM32 (STM32F303CCT6, LQFP48). Se comunica con el controlador principal del robot vía bus CAN, permitiendo la ejecución en tiempo real y de baja latencia de tareas complejas directamente en el cabezal de la herramienta o en el eje. Cuenta con una pantalla OLED integrada para diagnóstico instantáneo — splash de arranque animado, iconos animados por herramienta, telemetría en vivo en un panel de dos tonos — un LED RGB de estado de un solo píxel más un anillo de LED RGB direccionable para iluminación de cámara, un conector de expansión de 20 pines para placas adicionales, una F-RAM integrada que persiste los setpoints de la herramienta activa a través de un corte de energía, y etapas dedicadas de potencia analógica y de alta corriente.

## 🛠️ Arquitectura Escalable y Matriz de Herramientas

La fortaleza principal de URTC es su extrema versatilidad. En vez de cambiar la electrónica para cada trabajo distinto, la placa presenta una arquitectura de matriz escalable:

* **Esquema de identificación de 32 direcciones:** el hardware y el protocolo de comunicación están diseñados para identificar hasta 32 herramientas o efectores finales distintos directamente en el cabezal del robot, mediante una matriz de identificación de 5 bits por jumper de soldadura (ID0-ID4). De esas 32 lecturas, 31 se mapean directamente a un perfil de herramienta; la número 32 (los 5 jumpers instalados, `11111`) está reservada como dirección de "configuración libre" en su lugar - ver abajo.
* **25 perfiles automatizados plug-and-play:** el firmware maneja nativamente 25 perfiles de herramienta - la placa lee la identidad física del cabezal de herramienta y configura las etapas de potencia, sensores, y la lógica de conmutación de forma transparente sin necesitar un reflasheo completo. Quedan 6 direcciones más libres dentro del esquema existente para futuros perfiles de herramienta.
* **Configuración libre de herramienta:** la lectura de jumper reservada `11111` no elige una herramienta fija - le indica a la placa que busque qué herramienta usar en un registro de su propia F-RAM persistente, establecido de antemano por CAN (mediante `URTC Flasher`). Útil para una placa que necesita reprogramarse a una herramienta distinta sin volver a soldar jumpers físicamente. Ver `docs/EEPROM.TXT` sección 5 para el mecanismo completo.

## 🔌 Flexibilidad de Hardware y Soporte de Motores

Para manejar una variedad tan amplia de aplicaciones, el hardware de URTC está completamente equipado para controlar:

* **Motores paso a paso NEMA:** NEMA 8, 11, 14, y 17 funcionan directamente desde el TMC2209 integrado, lo mismo que NEMA 23 y 34 - hasta **2.0A** en cualquiera de ellos mediante la etapa de driver de la placa principal. Para NEMA 23/34 a su par nominal completo, un TMC5160 en el conector de expansión (ver abajo) soporta hasta **10A**, escalado en corriente por los MOSFETs externos/resistencia de sensado elegidos para esa placa - el límite de 2.0A integrado no aplica una vez que un motor se ha movido al driver de expansión.
* **Motores BLDC de 3 fases / motores gimbal** para movimiento de alta precisión.
* **Motores con sensores Hall y tacómetros** para control en lazo cerrado.
* **Entradas dedicadas** para sensores de proximidad óptica reflectivos como el TCRT5000, más una entrada genérica de fin de carrera/límite activa en bajo, compartida entre cuatro perfiles de herramienta.

## 🧩 Conector de Expansión

Un header de 20 pines, separado de los conectores específicos de herramienta, para placas adicionales que necesiten más de lo que un perfil de herramienta dado expone por sí solo - un eje paso a paso extra (TMC2209 o TMC5160), una segunda placa de sensores, ese tipo de cosas.

| Pines | Señal |
|---|---|
| 4 | 24V |
| 1 | 3.3V |
| 1 | 5V |
| 3 | GND |
| 2 | I2C por bit-banging (SCL/SDA) — su propio bus, separado del I2C2 por hardware del OLED/F-RAM |
| 3 | STEP/DIR/EN — universal para cualquiera de los 2 chips driver de abajo |
| 4 | SPI por bit-banging (CS/SCK/MISO/MOSI) — para la interfaz de configuración/diagnóstico de un TMC5160, o cualquier otro chip configurable por SPI |
| 1 | GPIO de propósito general (entrada de interrupción capaz de EXTI si un futuro complemento necesita una respuesta rápida de sensor, p. ej. un fin de carrera) |
| 1 | TMC5160 DIAG0 (línea de diagnóstico de stall/fallo, consultada vía `0x182`/`0x183`) |

20 pines en total.

**Dos buses I2C separados a propósito:** el OLED/F-RAM usan el único periférico I2C por hardware utilizable de este chip (I2C2, en PA9/PA10); el conector de expansión obtiene su propio bus I2C independiente por bit-banging (PB10/PB11 - los únicos otros pares de pines capaces de I2C de este chip ya estaban comprometidos con otras funciones, así que el bit-banging fue la forma de darle a este conector su propio bus sin un conflicto de hardware). Cualquier cosa colgada del header de expansión — un ADC/DAC I2C, un expansor de puertos, lo que sea que una placa adicional dada necesite — comparte este bus por bit-banging con cualquier otro dispositivo I2C del lado de expansión, pero no puede estirar el reloj (clock stretching) ni interferir de otro modo con el propio timing del OLED en su bus separado, I2C2 por hardware.

**Un TMC2209 o un TMC5160, no necesariamente ambos.** Ambos chips usan la misma interfaz STEP/DIR/EN para el movimiento real, así que esa parte es universal. Donde difieren es en configuración/diagnóstico: un TMC2209 usa su propio UART de un solo hilo para eso, mientras que un TMC5160 usa SPI — y dado que los 2 son mutuamente excluyentes en cualquier placa de expansión dada, los 4 pines SPI también sirven de forma natural como hogar para la única línea UART de un TMC2209, en vez de necesitar todavía otro pin dedicado que nadie usa al mismo tiempo que el bus SPI. El bus SPI por bit-banging habla exactamente el protocolo que un TMC5160 espera (SPI Modo 3, MSB primero, CS mantenido en bajo durante toda la transacción — ver `docs/CANBUS.TXT`, `0x180`/`0x181` para el comando genérico de paso-de-bytes que lo controla) en vez de que este firmware necesite conocer el layout de registros específico de ese chip. La línea DIAG0 de stall/fallo de un TMC5160 también está cableada (`0x182`/`0x183`) — reutiliza uno de los 2 pines GPIO de propósito general, que ya estaban reservados exactamente para este tipo de entrada rápida impulsada por interrupción.

El detalle completo pin por pin — qué pin del MCU respalda qué señal, y el razonamiento detrás de un par de restricciones de layout que tiene el paquete de 48 pines de este chip — vive en `docs/PINOUT_CONNECTORS.TXT` y `src/F303-master/README.md`.

### Las 6 variantes de placa de expansión

4 de las 6 variantes de placa de expansión llevan un driver de motor paso a paso — ya sea un TMC2209 (hasta 2A/bobina, MOSFETs de potencia integrados) o un TMC5160A (hasta 10A+/bobina, necesita 8 MOSFETs de potencia externos que el propio driver no incluye). Independientemente de esa elección de driver, una placa con driver es o bien **básica** (driver + conectores solamente, sin MCU — STEP/DIR/EN enrutado directamente desde la placa principal) o **avanzada** (añade un segundo microcontrolador, STM32F303CBT6, más 2 chips de sensor locales — un ADC de 16 bits ADS1115 y una cámara térmica de la familia MLX9064x — y generación local de PWM para herramientas cuyo timing necesita generarse justo en el cabezal de herramienta en vez de enrutarse por un cable). 2×2 combinaciones, más 2 placas básicas solo-sensor adicionales (ADS1115 o MLX9064x, cableadas directamente a la propia STM32F303CC de la placa principal, sin driver y sin MCU esclavo) para una herramienta que solo necesita uno de esos 2 chips y nada más de lo que también lleva una placa avanzada — 6 placas en total — ver `BOM/BOM_EXPANSION_*.TXT` (6 archivos), `docs/EXPANSION.TXT`, y `docs/PINOUT_SLAVE.txt`.

La propia STM32F303CBT6 de la variante avanzada habla con la placa principal a través del bus I2C por bit-banging ya existente del conector de expansión de arriba — placa principal como maestro, chip esclavo respondiendo como un verdadero esclavo I2C por hardware — y controla su propio segundo bus I2C, local únicamente, para los 2 chips de sensor. Tiene su propio bootloader y firmware de aplicación, actualizados de la misma forma que la placa principal (CAN-OTA desde `URTC Flasher`), solo que retransmitidos a través de ese enlace I2C en vez de alcanzar el chip esclavo directamente. Ver `src/F303-slave/README.md` y `src/F303-slave/boot/README.md` para el detalle técnico completo.

## 💾 Persistencia de Parámetros

Una F-RAM FM24CL64B integrada (64Kbit, I2C) mantiene una instantánea actualizada periódicamente de los setpoints de la herramienta activa y de los ajustes globales de LED/OLED, para que un corte de energía repentino no deje "qué estaba haciendo esta placa" tan desconocido como fue imprevista la propia pérdida de energía. Comparte el bus I2C2 por hardware del OLED en vez de tener uno propio — este MCU solo tiene un periférico I2C por hardware utilizable para este propósito, ya reclamado por el OLED (ver `src/F303-master/README.md` sección 6 para el razonamiento completo).

**El estado recuperado es consultable, nunca auto-aplicado a nada peligroso.** Al arrancar, lo que se guardó se vuelve legible por CAN (`0x190`/`0x191`) — pero un setpoint de calentador, la potencia del láser, o un comando de motor nunca se rearman silenciosamente por sí solos. Solo los ajustes seguros y pasivos (colores de LED, modo OLED) se restauran directamente. Reenviar deliberadamente un setpoint después de realmente revisar qué pasó se deja como decisión del controlador maestro, no algo que esta placa decida por sí misma en el instante en que vuelve la energía.

## 💼 Catálogo de Herramientas Automatizadas Nativamente (25 Perfiles de Firmware)

Mediante su lógica de conmutación dinámica, el firmware maneja nativamente los siguientes cabezales de herramienta:

1. **Estación de soldadura (T12):** control PID preciso de temperatura usando retroalimentación directa de ADC para manejar puntas de soldador T12 estándar, más un alimentador de hilo motorizado que bobina el hilo de soldadura hacia la unión (comparte `CONN_MOT` y su protocolo de motor paso a paso con las herramientas de movimiento simple de abajo - cede la propia entrada de fin de carrera genérica de esta herramienta para hacerle sitio). [Configuración de jumper/cableado →](images/TOOL_SOLDERING_IRON.png)
2. **Dispensador de pasta de soldadura SMT:** control de alimentación milimétrico para deposición precisa de pasta de soldadura en PCBs. [Configuración de jumper/cableado →](images/TOOL_PASTE_DISPENSER.png)
3. **Dispensador de pasta térmica / líquido:** gestión de fluidez para pastas de alta viscosidad o adhesivos líquidos. [Configuración de jumper/cableado →](images/TOOL_LIQUID_DISPENSER.png)
4. **Destornillador eléctrico inteligente:** control de rotación y parada basado en límites de torque o fines de carrera. [Configuración de jumper/cableado →](images/TOOL_SCREWDRIVER.png)
5. **Pinza de vacío / neumática:** control de bomba de vacío y lectura de nivel de presión para operaciones seguras de Pick-and-Place. [Configuración de jumper/cableado →](images/TOOL_VACUUM_PICKUP.png)
6. **Taladro (BL4260):** control de velocidad PWM, cambio de dirección, y frenado eléctrico dinámico con lecturas de RPM en tiempo real, en su propia línea dedicada de habilitación/freno, independiente de la habilitación del driver de herramientas paso a paso. Entrada de fin de carrera genérica disponible. [Configuración de jumper/cableado →](images/TOOL_DRILL.png)
7. **Pinza gimbal:** manipulación de alta sensibilidad usando motores gimbal brushless de 3 fases. [Configuración de jumper/cableado →](images/TOOL_GRIPPER_GIMBAL.png)
8. **Pinza NEMA:** fuerza de sujeción robusta controlada mediante un motor paso a paso de trabajo pesado. [Configuración de jumper/cableado →](images/TOOL_GRIPPER_NEMA.png)
9. **Sistema AOI (Inspección Óptica Automatizada):** control estroboscópico síncrono del arreglo de iluminación LED para captura de cámara de visión artificial. Entrada de fin de carrera genérica disponible. [Configuración de jumper/cableado →](images/TOOL_AOI_INSPECTION.png)
10. **Diodo láser de grabado (10W óptico):** modulación PWM de potencia del haz con un lazo de seguridad por hardware (watchdog CAN) que bloquea si se pierde la comunicación con el host. Entrada de fin de carrera genérica disponible. [Configuración de jumper/cableado →](images/TOOL_LASER_ENGRAVER.png)
11. **Hotend de impresión 3D:** control PID del cartucho calentador, lectura de termistor NTC, control de extrusor, y un ventilador dedicado de enfriamiento de capa controlado por PWM de 25kHz (4 hilos, retroalimentación de tacómetro, watchdog de comunicación propio) — todo integrado en un solo bloque. [Configuración de jumper/cableado →](images/TOOL_3D_PRINTER.png)
12. **Sonda de escáner 3D:** entrada de interrupción por hardware ultrarrápida (EXTI) con prioridad absoluta para digitalización de superficie en tiempo real y detección de impacto sin retraso. También cubre el palpado táctil de metrología - la misma ruta de hardware, una sonda física distinta en el mismo cabezal de herramienta. [Configuración de jumper/cableado →](images/TOOL_SCAN_PROBE.png)
13. **Cabezal Pick & Place SMT:** eje rotativo A para alineación correcta de pads, en la misma interfaz de motor paso a paso que los dispensadores de pasta/líquido y ambas pinzas de arriba. [Configuración de jumper/cableado →](images/TOOL_SMT_PICKPLACE.png)
14. **Electroimán de trabajo pesado:** control de recogida on/off para piezas ferromagnéticas, desde la salida del calentador T12 reutilizada como driver GPIO genérico. [Configuración de jumper/cableado →](images/TOOL_ELECTROMAGNET.png)
15. **Cabezal de soldadura por puntos:** pulsos de soldadura de precisión de milisegundos para tiras de níquel de packs de baterías, con un sensor de contacto de superficie que controla el pulso. [Configuración de jumper/cableado →](images/TOOL_SPOT_WELDER.png)
16. **Aerógrafo de recubrimiento conformal:** control de aspersión de recubrimiento protector para PCBs terminadas - la válvula de aspersión y su propio sensor viven en la propia placa principal del robot, fuera del alcance propio de esta placa. [Configuración de jumper/cableado →](images/TOOL_CONFORMAL_COATING.png)
17. **Pinza de vacío de gran formato:** arreglo de ventosas múltiples para placas FR4 sin poblar, en la misma interfaz de motor paso a paso que la herramienta #13 de arriba. [Configuración de jumper/cableado →](images/TOOL_VACUUM_GRIPPER_LG.png)
18. **Cabezal de pruebas funcionales:** pruebas de voltaje/continuidad con sonda voladora — lectura básica desde el ADC integrado, lectura avanzada mediante un ADC de 16 bits ADS1115 en una placa de expansión **avanzada**. [Configuración de jumper/cableado →](images/TOOL_FLYING_PROBE.png)
19. **Cabezal de curado UV:** driver de LED UV de alta potencia para curado instantáneo de pegamento/máscara. [Configuración de jumper/cableado →](images/TOOL_UV_CURING.png)
20. **Boquilla de retrabajo de aire caliente:** elemento calefactor, soplador de turbina, y retroalimentación de termopar para reflow de piezas SMD desalineadas - comparte el propio lazo de control térmico del soldador. [Configuración de jumper/cableado →](images/TOOL_HOTAIR_REWORK.png)
21. **Insertador neumático de presión (press-fit):** control de actuador lineal para presionar conectores en PCBs - el actuador y su propio sensor viven en la propia placa principal del robot, fuera del alcance propio de esta placa. [Configuración de jumper/cableado →](images/TOOL_PRESSFIT_INSERTER.png)
22. **Actuador de arnés de cables / engarzado (crimping):** mordaza de alto torque para pelar/engarzar terminales, impulsada por el **propio driver de una placa de expansión** en vez del de la placa principal. [Configuración de jumper/cableado →](images/TOOL_CRIMPING_ACTUATOR.png)
23. **Inspección avanzada de PCB:** imagen térmica (arreglo de la familia MLX9064x - los 3 miembros de la familia, MLX90640/MLX90641/MLX90642, soportados hoy, ya sea mediante el propio chip esclavo de una placa de expansión **avanzada** o una placa de expansión **básica** MLX9064x cableada directamente a la placa principal) para detectar cortocircuitos por firma de temperatura, junto con iluminación por anillo de LED. También cubre el depanelizado por micro-husillo - la misma ruta de hardware del taladro de arriba, un bit distinto para un trabajo distinto. [Configuración de jumper/cableado →](images/TOOL_THERMAL_INSPECTION.png)
24. **Válvula de jetting de pasta de soldadura:** dispensación de micro-gotas piezoeléctrica, precisión de pulso sub-milisegundo generada localmente en una placa de expansión **avanzada**. [Configuración de jumper/cableado →](images/TOOL_PASTE_JETTING.png)
25. **Soldador ultrasónico / sellador de empaques:** disparo de transductor de alta frecuencia para soldadura de carcasas plásticas. [Configuración de jumper/cableado →](images/TOOL_ULTRASONIC_WELDER.png)

*(Existen imágenes de configuración de herramienta para las herramientas 1-12; las imágenes para las herramientas 13-25 se irán completando a medida que la documentación de hardware se ponga al día - los nombres de archivo de arriba coinciden con la convención de nomenclatura ya en uso en `images/`.)*

## 🖥️ Interfaz OLED Local

Cada cabezal de herramienta muestra telemetría en vivo, específica de la herramienta, en un OLED de dos tonos de 128×64: un splash de arranque animado al encender, un indicador parpadeante de actividad CAN, una lectura "hero" en vivo en la franja superior (temperatura, RPM, potencia — lo que más importe para la herramienta activa), y un pequeño icono animado de cuatro fotogramas por perfil de herramienta.

### El módulo

Ambas variantes físicas de abajo son el mismo panel eléctricamente (controlado por SSD1306 o SSD1315 - la secuencia de inicialización del firmware está verificada como compatible con ambos, ver `OLED_Init()` en `firmware_oled_driver.c`; el SSD1315 es un controlador de reemplazo directo más nuevo que muchos módulos llevan hoy bajo el mismo listado/serigrafía "SSD1306"), **128×64**, y la misma división de dos tonos "amarillo/azul", donde el propio material de LED físico está dividido en 2 zonas de color fijo (esto no es seleccionable por software):

* **16 píxeles superiores (páginas 0-1): amarillo.** URTC usa esta franja para lo que sea más útil de ver de un vistazo sin leer con detalle - el indicador de actividad CAN, lecturas hero en vivo, o (en el splash de arranque / pantallas de herramienta inválida) texto de estado corto.
* **48 píxeles inferiores (páginas 2-7): azul.** Todo lo demás - iconos de herramienta, telemetría detallada, la cara animada de JuanenBOT en la pantalla de splash, la gran palabra ERROR parpadeante.

Ambos caen en el mismo bus I2C2 y en el mismo `OLED_Init()` — el firmware no puede saber cuál de los 2 está conectado, y no lo necesita. Son mutuamente excluyentes en una placa dada (ver la nota `CONN_OLED2` de `BOM/BOM.TXT` - el nombre que usa este documento para lo que el esquemático llama `LCD1`).

#### Opción A — montaje directo (`CONN_OLED2`, el footprint realmente poblado en la placa)

<img src="images/OLED_DIRECT_MOUNT.jpg" width="220">

Un panel desnudo sin PCB de breakout separado - solo el cristal y su cinta FPC de 30 pines, soldada directamente en el footprint `CONN_OLED2` (`FPC30`, WiseChip UG-2864, el nombre que usa este documento para lo que el esquemático llama `LCD1` — ver `BOM/BOM.TXT` y `URTC_NETLIST.TXT`). De los 30 pines, solo un subconjunto está realmente cableado — el resto es el bus de interfaz paralela del panel (`D2`–`D7`, `RW`, `E/!RD`), dejado sin conectar ya que la placa solo le habla por I2C:

| Pin(es) CONN_OLED2 | Net | Función |
|---|---|---|
| 1, 8, 29, 30 | GND / AGND | Tierra |
| 9 | VDD | Alimentación lógica (desde `+3V3B`, el riel exclusivo del OLED — ver BOM §1) |
| 28 | VCC | Alimentación del panel |
| 2–5 | C2P/C2N/C1P/C1N | Condensadores de bomba de carga — `C26`/`C27` en la BOM |
| 26 | IREF | Resistencia de ajuste de corriente de referencia |
| 27 | VCOMH | Desacoplo de voltaje común interno |
| 10, 12 | BS0, BS2 | Conectados a GND |
| 11 | BS1 | Conectado a `+3V3B` |
| 18 | D0/SCK | I2C2 SCL — PA9 |
| 19 | D1/DIN/SDA | I2C2 SDA — PA10 |

`BS0`/`BS1`/`BS2` son el propio strap de selección de interfaz del panel (GND/VCC/GND aquí), fijo en hardware en vez de expuesto al MCU — esto es lo que pone al controlador en modo I2C en primer lugar, en vez del modo paralelo 8080/6800 al que pertenecen los otros 22 pines FPC.

#### Opción B — módulo breakout (`CONN_OLED`, alternativa externa)

<img src="images/OLED_BREAKOUT_MODULE.jpg" width="220">

El mismo panel pre-montado en una pequeña placa portadora con un header de 4 pines - útil si prefieres cablear un módulo comercial en vez de conseguir el panel FPC desnudo. Cableado directamente a `CONN_OLED` sin necesidad de cruzar nada — el propio orden de pines del módulo (`GND · VDD · SCK · SDA`) coincide exactamente con el pinout de `CONN_OLED`, pin por pin:

| Pin del módulo OLED | Pin CONN_OLED | Señal |
|---|---|---|
| GND | 1 | Tierra |
| VDD | 2 | +3.3V (alimentación lógica de la pantalla) |
| SCK | 3 | SCL — PA9, I2C2 por hardware |
| SDA | 4 | SDA — PA10, I2C2 por hardware |

### Splash de arranque

<img src="ani/splash_boot.gif" width="480">


### Iconos de herramienta (uno por perfil, animación de 4 fotogramas)

<table>
<tr>
<td align="center"><img src="ani/00_soldering_iron.gif" width="80"><br>Soldador T12</td>
<td align="center"><img src="ani/01_paste_dispenser.gif" width="80"><br>Dispensador de Pasta</td>
<td align="center"><img src="ani/02_liquid_dispenser.gif" width="80"><br>Dispensador de Líquido</td>
<td align="center"><img src="ani/03_screwdriver.gif" width="80"><br>Destornillador</td>
</tr>
<tr>
<td align="center"><img src="ani/04_vacuum_pickup.gif" width="80"><br>Pinza de Vacío</td>
<td align="center"><img src="ani/05_drill.gif" width="80"><br>Taladro (BL4260)</td>
<td align="center"><img src="ani/06_gripper_gimbal.gif" width="80"><br>Pinza Gimbal</td>
<td align="center"><img src="ani/07_gripper_nema.gif" width="80"><br>Pinza NEMA</td>
</tr>
<tr>
<td align="center"><img src="ani/08_aoi_inspection.gif" width="80"><br>Inspección AOI</td>
<td align="center"><img src="ani/09_laser_engraver.gif" width="80"><br>Grabador Láser</td>
<td align="center"><img src="ani/10_3d_printer.gif" width="80"><br>Hotend de Impresora 3D</td>
<td align="center"><img src="ani/11_scan_probe.gif" width="80"><br>Sonda de Escáner 3D</td>
</tr>
<tr>
<td align="center"><img src="ani/12_smt_pickplace.gif" width="80"><br>Pick & Place SMT</td>
<td align="center"><img src="ani/13_electromagnet.gif" width="80"><br>Electroimán</td>
<td align="center"><img src="ani/14_spot_welder.gif" width="80"><br>Soldador por Puntos</td>
<td align="center"><img src="ani/15_conformal_coating.gif" width="80"><br>Recubrimiento Conformal</td>
</tr>
<tr>
<td align="center"><img src="ani/16_vacuum_gripper_lg.gif" width="80"><br>Pinza de Vacío (LG)</td>
<td align="center"><img src="ani/17_flying_probe.gif" width="80"><br>Sonda Voladora</td>
<td align="center"><img src="ani/18_uv_curing.gif" width="80"><br>Curado UV</td>
<td align="center"><img src="ani/19_hotair_rework.gif" width="80"><br>Retrabajo de Aire Caliente</td>
</tr>
<tr>
<td align="center"><img src="ani/20_pressfit_inserter.gif" width="80"><br>Insertador Press-Fit</td>
<td align="center"><img src="ani/21_crimping_actuator.gif" width="80"><br>Actuador de Engarzado</td>
<td align="center"><img src="ani/22_thermal_inspection.gif" width="80"><br>Inspección Térmica</td>
<td align="center"><img src="ani/23_paste_jetting.gif" width="80"><br>Jetting de Pasta</td>
</tr>
<tr>
<td align="center"><img src="ani/24_ultrasonic_welder.gif" width="80"><br>Soldador Ultrasónico</td>
</tr>
</table>


### Advertencia de ID de herramienta inválida

Si los jumpers de ID no coinciden con ninguno de los 25 perfiles asignados, la placa bloquea todos los actuadores y parpadea esto en su lugar:

<img src="ani/error_warning.gif" width="480">

Todos los GIFs fuente de las animaciones viven en [`/ani`](ani/).

## 🔴🟢🔵 LED de Estado Digital

Separado del OLED y del anillo de iluminación de 8 píxeles, `CONN_LED1` lleva un único LED RGB direccionable (familia WS2812B, controlado por SPI/DMA) dedicado al estado de un vistazo.

**Automático por defecto, anulable por el host bajo demanda.** El firmware colorea este LED por sí mismo, con prioridad de 3 vías:

* 🔴 **Rojo** — hay un fallo de hardware activo (`system_error_flag`). Siempre gana, sin importar qué más esté pasando.
* 🔵 **Azul** — la placa está funcionando activamente: llegó una trama CAN (cualquier ID) en los últimos 1.5 segundos.
* 🟢 **Verde** — inactiva, esperando comandos: sin tráfico CAN en más de 1.5 segundos.

El maestro todavía puede anular esto en cualquier momento enviando el ID CAN `0x100` (DLC 8) con la intensidad de rojo, verde, y azul como los primeros 3 bytes (0-255 cada uno — color completo de 24 bits, no solo los 3 automáticos). Un color enviado por el host se mantiene 10 segundos antes de volver al esquema automático — suficiente para realmente verse, lo bastante corto para que la placa no se quede atascada mostrando un color personalizado obsoleto si el host deja de actualizarlo. Enviar `0x100` de nuevo (ya sea el mismo color o uno nuevo) renueva esa ventana de 10 segundos, así que un host que quiera mantener control personalizado solo necesita seguir enviándolo periódicamente. Un fallo de hardware siempre interrumpe una anulación activa — el rojo tiene prioridad sobre cualquier color que el host hubiera establecido.

Ver `docs/CANBUS.TXT` (ID `0x100`) para el layout exacto de bytes, que también comparte este mismo mensaje con el control del anillo de LED y del modo nocturno del OLED.

## 📸 Fotos

![URTC v1.0](images/URTC_BOARD.png)

*(Trabajo en progreso — más ángulos y una placa poblada llegarán pronto.)*

## 🔧 Compilación y Flasheo

La flash de URTC está dividida en 2 piezas independientes, para que la placa pueda reflashearse por el mismo cordón umbilical CAN que ya usa para todo lo demás — sin necesitar nunca más acceso físico al header JTAG/SWD después de la configuración inicial.

### Layout de memoria flash (256K total, modelo de imagen dorada / actualización A-B)

```
0x08000000 ┌─────────────────────────────────┐
           │  Bootloader (30K)                 │  Siempre se ejecuta primero en cada
           │                                   │  arranque. Escucha brevemente por
           │                                   │  CAN, y luego o salta a la app o
           │                                   │  espera una actualización. Controla
           │                                   │  el OLED directamente durante una
           │                                   │  actualización (ver abajo).
0x08007800 ├─────────────────────────────────┤
           │  Página de metadatos (2K)         │  Describe lo que sea que haya en el
           │                                   │  slot principal en este momento:
           │                                   │  HardwareID, versión, tamaño, CRC32,
           │                                   │  y una firma HMAC-SHA256. El
           │                                   │  bootloader lo comprueba todo antes
           │                                   │  de saltar a la app.
0x08008000 ├─────────────────────────────────┤
           │  Slot principal (112K)            │  Este es el firmware de aplicación /
           │                                   │  URTC_V1.1_F303CC.* — el firmware
           │                                   │  real que se ejecuta día a día,
           │                                   │  descrito en el resto de este
           │                                   │  README. Nunca se toca en una
           │                                   │  actualización hasta que una imagen
           │                                   │  verificada y buena conocida está
           │                                   │  lista para reemplazarlo.
0x08024000 ├─────────────────────────────────┤
           │  Slot de respaldo / staging (112K)│  Solo almacenamiento en bruto,
           │                                   │  nunca ejecutado directamente. Cada
           │                                   │  actualización CAN escribe aquí
           │                                   │  primero.
0x08040000 └─────────────────────────────────┘
```

**Por qué un slot de respaldo.** Una actualización CAN nunca se escribe en el slot que está corriendo actualmente. Va al respaldo primero, se verifica completamente ahí — tamaño, CRC32, y una firma HMAC-SHA256 que prueba que realmente vino del propio proceso de build de este proyecto, no solo que llegó intacta — y solo entonces se copia al slot principal. Una pérdida de energía en cualquier punto antes de que empiece esa copia deja el firmware actualmente en ejecución completamente intacto, así que no hay ninguna ventana en la que una descarga interrumpida pueda inutilizar la placa. Si la pérdida de energía ocurre *durante* la propia copia, el bootloader se da cuenta en el siguiente arranque (el respaldo, nunca tocado durante la copia, sigue completamente intacto) y simplemente retoma la copia desde ahí hasta que tiene éxito.

### 0. Compilar desde el código fuente (opcional — `firmware/` ya viene con binarios precompilados)

Dos formas de ir desde el código fuente de este repositorio hasta los 4 binarios de arriba:

- **Automatizada:** `build_firmware.sh` (Linux) o `build_firmware.bat` (Windows), en la raíz del repositorio. Cualquiera de los 2 instala la toolchain ARM GNU si falta, descarga el commit fijado de ST HAL/CMSIS, y compila, enlaza, y aplica `objcopy` a los 4 binarios (app + bootloader de la placa principal, app + bootloader del esclavo de expansión) directamente en `firmware/`, y luego regenera `firmware/firmware_manifest.json`. Ejecútalo sin argumentos para una build completa, `--clean` para vaciar primero la caché local `build/`, o `master`/`slave` para compilar solo el par propio de un chip. `build_firmware.sh` corre de principio a fin contra el árbol de código fuente real de este proyecto; `build_firmware.bat` refleja esa misma lógica para Windows — si alguna vez discrepan, confía en la lógica del script `.sh` como referencia.
- **Manual:** cada comando que cualquiera de los 2 scripts ejecuta, más el razonamiento detrás de cada elección de toolchain/HAL, está detallado paso a paso en `docs/COMPILE_STM32F303.TXT` — útil en un sistema operativo distinto, con una fuente HAL/CMSIS distinta, o simplemente para ver exactamente qué automatizan los scripts.

Después de cualquier cambio en el código fuente del firmware (o antes de confiar en un incremento de versión), ejecuta **`check_version_consistency.sh`** desde la raíz del repositorio: lee las constantes de versión de las Pistas A/E (firmware de la placa principal, aplicación del esclavo de expansión) como fuente de verdad y comprueba cada ubicación que documenta `VERSION_CHECKLIST.txt` para esa etiqueta de versión, reportando cualquier discrepancia — solo reporta, no arregla nada por sí mismo. `VERSION_CHECKLIST.txt` es la referencia completa para las 5 pistas de versión independientes que lleva este proyecto (firmware principal, hardware/PCB, bootloader principal, aplicación del esclavo de expansión, bootloader del esclavo de expansión) y exactamente qué hay que tocar al incrementar cualquiera de ellas.

### 1. Configuración inicial — requiere JTAG/SWD (una vez)

El bootloader solo puede entrar al chip mediante programación física — no hay forma de flashear por CAN una placa que todavía no tiene un bootloader instalado. Este es un paso único:

1. Abre el proyecto en **STM32CubeIDE** (construido y probado contra el target STM32F303CC), o usa **STM32CubeProgrammer** directamente con los binarios compilados de abajo.
2. Flashea **ambas** imágenes por SWD (ST-Link) mediante el header `STM_JTAG` integrado — cada archivo `.hex` tiene su dirección de destino incrustada, así que la mayoría de las herramientas (incluyendo STM32CubeProgrammer) pueden cargar ambas en la misma sesión:
   * `URTC_BOOTLOADER.hex` → `0x08000000`
   * `URTC_V1.1_F303CC.hex` → `0x08008000`
3. Establece la identidad de la herramienta mediante los jumpers de soldadura de ID antes de encender - la placa los lee una vez al arrancar, como siempre. Cinco jumpers (ID0-ID4), cubriendo el espacio completo de 32 direcciones (31 direcciones de herramienta directas, más la dirección reservada `11111` de configuración libre - ver la sección de Matriz de Herramientas de arriba).
4. Enciende. El bootloader escucha durante ~600ms, no ve nada, y salta directamente a la aplicación — desde aquí en adelante, todo se comporta exactamente como se describe en el resto de este README.

**El header JTAG nunca se quita ni se deshabilita.** Siempre está ahí como respaldo — si una actualización CAN alguna vez sale mal, o simplemente lo prefieres, puedes reflashear cualquiera de las 2 imágenes por SWD en cualquier momento.

**Dos pulsadores integrados, BOOT y RESET**, también están ahí para recuperación — RESET es un reset de hardware ordinario (`NRST`), y BOOT pone `BOOT0` en alto, que es una decisión a nivel de chip tomada *antes* de que nada de este repositorio corra en absoluto: normalmente (no mantenido) el chip arranca desde la flash hacia el propio bootloader de este proyecto como se describió arriba; mantenido durante el reset, arranca hacia el propio bootloader de System Memory de fábrica de ST en su lugar (recuperación por USB DFU/UART, completamente separado de cualquier cosa de aquí). Ver `src/F303-master/README.md` sección 4a para el detalle técnico completo.

### 2. Actualizaciones posteriores — por bus CAN

Una vez que el bootloader está en su lugar, actualizar la aplicación ya no necesita acceso físico a la placa en absoluto — solo envía la build de firmware nueva por la misma línea CAN umbilical que ya lleva comandos al cabezal de herramienta.

**La secuencia de actualización:**

1. **Disparo.** El maestro envía `0x7F0` (DLC 4, payload `B0 07 1D 5A`) a la *aplicación en ejecución*. Corta la energía de forma segura a cada actuador en línea — motores, calentadores, láser — y resetea el chip. Este requisito de payload mágico significa que una trama corrupta o malformada no puede disparar accidentalmente un reset hacia el modo de actualización.
2. **Inicio.** Tras el reset, el bootloader está escuchando. El maestro envía `0x7F1` (DLC 8, tamaño total de firmware big-endian + HardwareID big-endian). Una imagen construida para un hardware distinto se rechaza justo aquí, antes de que se toque un solo byte de flash. El bootloader borra exactamente tantas páginas del slot de respaldo como necesite la imagen nueva y responde con una trama de estado (`0x7F5`).
3. **Firma.** El maestro envía la firma HMAC-SHA256 esperada como 4 tramas `0x7F7` (8 bytes cada una, en orden) — calculada sobre la imagen de firmware con una clave compartida entre el bootloader y cualquier herramienta que firme la build.
4. **Datos.** El maestro transmite el archivo `.bin` como una secuencia de tramas `0x7F2` (hasta 8 bytes de datos de firmware en bruto cada una), enviadas una tras otra — CAN garantiza que las tramas llegan en el orden en que se enviaron en un solo bus, así que no hace falta un número de secuencia por trama. El bootloader acumula los bytes entrantes en una página de 2KB en RAM y la escribe en el slot de *respaldo* una vez llena, releyendo cada media palabra y comparándola contra lo que se suponía que se escribiera antes de dar la página por terminada, y enviando un reconocimiento `0x7F3` (con el índice de página) tras cada escritura verificada. Una implementación razonable del maestro espera el ACK de cada página antes de enviar los datos de la siguiente, para evitar desbordar el búfer de recepción del bootloader.
5. **Fin y verificación.** Una vez que se ha enviado cada byte, el maestro envía `0x7F4` (DLC 8, CRC32 big-endian + versión mayor/menor). El bootloader comprueba el tamaño del slot de respaldo, calcula su CRC32 y HMAC-SHA256 y compara ambos contra lo que declaró el maestro. Solo si todo coincide procede a copiar el respaldo al slot principal, página por página, con la misma verificación de relectura de arriba. Una vez que esa copia está completa y confirmada, guarda los metadatos nuevos y resetea hacia la aplicación actualizada. Ante cualquier discrepancia — tamaño, CRC32, HMAC, o HardwareID — el slot principal nunca se toca en absoluto, y el bootloader simplemente vuelve a escuchar para un intento nuevo.

**Tramas de estado (`0x7F5`, DLC 1):** `0x01` escuchando, `0x02` borrando, `0x03` recibiendo, `0x06` verificando, `0x07` copiando respaldo a principal, `0x04` verificado OK (a punto de saltar), `0x05` verificación fallida, `0xFF` error.

**Latido (`0x7F6`, DLC 2, cada ~1s mientras escucha o actualiza):** byte de estado + porcentaje de progreso (0-100, o `0xFF` donde un porcentaje no aplica). Permite al maestro distinguir "el nodo está vivo pero todavía no ha empezado a escuchar" de "el nodo está completamente sin respuesta" - útil para puesta en marcha automatizada y para detectar un bootloader atascado sin esperar a un timeout.

**Progreso en pantalla.** El bootloader controla el OLED directamente durante una actualización — nadie tiene que adivinar si algo está pasando. Muestra "UPDATING" más una barra de progreso en vivo y un porcentaje mientras se escriben o copian páginas, "FLASH OK" durante un instante antes de resetear hacia el firmware nuevo, y "ERROR" si falla la escritura de una página, la transferencia se estanca más de 10 segundos, o la verificación vuelve con una discrepancia.

**⚠️ Prueba esto en banco antes de confiar en ello en campo.** El protocolo de arriba compila y enlaza limpio y la lógica se ha razonado cuidadosamente, pero un bootloader es exactamente el tipo de firmware donde "compila correctamente" está muy lejos de "confiable en hardware" — el timing real de programación de flash, el comportamiento de CAN a través de una transferencia de varios miles de tramas, y el traspaso bootloader-a-aplicación necesitan todos verificarse en una placa real (idealmente con JTAG a mano como respaldo) antes de confiar en esto para una actualización desatendida con actuadores reales conectados.

### Herramientas de PC

Dos herramientas GUI independientes y multiplataforma (Windows/Linux)
dan soporte a esta placa - **URTC Flasher** (actualizaciones CAN-OTA y
de chip completo por SWD/JTAG, tanto para esta placa como, en una
variante de expansión Avanzada, para su propio chip esclavo de
expansión) y **URTC Tester** (un ejercitador en vivo de bus CAN que
muestra el perfil de herramienta que esté jumpereado en cada momento).
Ambas solían vivir dentro de este repositorio bajo `tools/`; cada una es
ahora su propio proyecto independiente, con su propio README, licencia,
y traducciones:

- [URTC Flasher](https://github.com/JuanenRac/URTC-FLASHER)
- [URTC Tester](https://github.com/JuanenRac/URTC-TESTER)

También existe una alternativa basada en web que cubre terreno similar
(monitorización en vivo, análisis CAN, flasheo OTA, inspección térmica)
sin instalar nada localmente: [URTC Web Studio](https://github.com/JuanenRac/URTC-WEB-STUDIO).

## 📋 Registro de Cambios

El firmware y el bootloader se versionan y publican de forma
independiente - flashear un bootloader nuevo no implica una versión de
aplicación nueva y viceversa, así que cada uno tiene su propio historial
en su propio archivo en vez de un número de versión combinado que
implicaría que siempre se mueven juntos:

- Firmware (`src/F303-master/`): [`src/F303-master/CHANGELOG.md`](src/F303-master/CHANGELOG.md)
- Bootloader (`src/F303-master/boot/`): [`src/F303-master/boot/CHANGELOG.md`](src/F303-master/boot/CHANGELOG.md)
- Aplicación del esclavo de expansión (`src/F303-slave/`, STM32F303CBT6): [`src/F303-slave/CHANGELOG.md`](src/F303-slave/CHANGELOG.md)
- Bootloader del esclavo de expansión (`src/F303-slave/boot/`): [`src/F303-slave/boot/CHANGELOG.md`](src/F303-slave/boot/CHANGELOG.md)

**Política de versionado:** los 4 componentes (2 firmwares de aplicación, 2 bootloaders - `FIRMWARE_VERSION_MAJOR`/`MINOR`/`PATCH` y `BOOTLOADER_VERSION_MAJOR`/`MINOR`/`PATCH`) son **incrementales** - cada compilación real sube el `PATCH` propio de ese componente en 1 automáticamente (`bump_version.py` en la raíz del repositorio, invocado por `build_firmware.sh`/`.bat` justo antes de compilar cada componente), con acarreo hacia `MINOR` (y luego `MAJOR`) cuando `PATCH` superaría 9, la misma regla en base 10 de "cuentakilómetros" que usa un odómetro real - p. ej. `1.1.7` → `1.1.8` → `1.1.9` → `1.2.0`, nunca `1.1.10`. Cada bootloader mantiene además su propia copia del `FIRMWARE_VERSION_*` de la aplicación correspondiente, sincronizada automáticamente por el mismo bump. Ver [`CHANGELOG.md`](CHANGELOG.md) en la raíz del repositorio para el estado actual de los 4 componentes de un vistazo, y [`VERSION_CHECKLIST.txt`](VERSION_CHECKLIST.txt) para la mecánica completa por pista.

## 🔍 Estado Actual

**Firmware (`src/F303-master/`):** completo en funcionalidad para los 25 perfiles de herramienta — control PID térmico, telemetría por herramienta, watchdogs de comunicación, detección de stall/fallo, y el propio diagnóstico en vivo del OLED, junto con un par de consulta de herramienta activa (`0x110`/`0x111`), un paso-a-través SPI genérico (`0x180`/`0x181`) para el conector de expansión, una F-RAM integrada que persiste los setpoints a través de un corte de energía (`0x190`/`0x191`), el mecanismo de configuración libre de herramienta con jumper `11111` (`0x1A2`/`0x1A3`), reporte de tipo de periférico + número de serie de dispositivo (`0x1A4`/`0x1A5`) para distinguir múltiples placas por lo demás idénticas en un bus compartido, y un puente CAN-a-I2C (`0x210`-`0x221`) que alcanza el chip esclavo de expansión en placas de expansión avanzadas. Versionado independientemente del bootloader (ver el Registro de Cambios abajo).

**Bootloader (`src/F303-master/boot/`):** sistema de actualización A/B de imagen dorada completo en funcionalidad — actualizaciones OTA firmadas con HMAC-SHA256 por CAN, un slot de respaldo que garantiza que una actualización fallida nunca inutiliza la placa, y su propio reporte de versión (`0x7FA`) independiente de la aplicación. Compila y enlaza limpio; ver el aviso de prueba en banco de arriba antes de confiar en él desatendido con actuadores reales conectados.

**Herramientas de PC:** tanto [URTC Flasher](https://github.com/JuanenRac/URTC-FLASHER) (actualizaciones CAN OTA + programación de chip completo por SWD/JTAG) como [URTC Tester](https://github.com/JuanenRac/URTC-TESTER) (ejercitador en vivo de control/telemetría por herramienta) están completas en funcionalidad para lo que se propusieron hacer, cada una ahora su propio proyecto independiente con su propio README que cubre la configuración y cada control en detalle.

**Hardware:** el esquemático y la BOM todavía se están finalizando; todavía no existe ninguna placa poblada para validar nada de lo de arriba contra silicio real. Todo lo de arriba compila, enlaza, y se ha razonado con cuidado, pero "compila correctamente" y "verificado en hardware" son 2 afirmaciones distintas — ver el aviso de seguridad al principio de este README, y trata una primera puesta en marcha con la precaución que cualquier placa nueva merece.

Si alguien de la comunidad está trabajando en efectores finales personalizados, cambiadores de herramienta inteligentes, o integración avanzada de herramientas para PAROL6, Faze4, o cualquier otra plataforma de brazo robótico, ¡me encantaría charlar, intercambiar ideas, o profundizar en los comandos CAN!

## 📂 Estructura del Repositorio

```
/
├── 3D/
│   ├── RACK/                    Rack de montaje de la placa, 2 variantes (x1, x3) - cada una
│   │                            en .stl/.3mf/.amf/.scad
│   ├── REVOLVER/                Placeholder - vacío, contenido todavía no iniciado
│   └── TOOLS/
│       └── PAROL6/              Piezas imprimibles en 3D por herramienta para el brazo robótico
│                                PAROL6 - una subcarpeta por herramienta (0.Universal parts, luego
│                                1-12 según la numeración del Catálogo de Herramientas de arriba),
│                                cada una en .stl/.3mf/.amf/.scad donde está poblada; varias
│                                (4, 6-12) siguen siendo placeholders vacíos
├── ani/                          27 GIFs: una animación de 4 fotogramas por perfil de herramienta
│                                 (00-24, coincidiendo con el propio ID numérico de cada
│                                 herramienta), el splash de arranque (splash_boot.gif), y la
│                                 advertencia de ID inválido (error_warning.gif) - todos
│                                 decodificados directamente desde el propio código fuente del
│                                 firmware de este proyecto (las propias tablas
│                                 ToolIcons[]/SplashFace[]/ErrorText[] de firmware_render.c), no
│                                 dibujados a mano por separado, así que siempre coinciden con lo
│                                 que el OLED real realmente muestra
├── BOM/
│   ├── BOM.TXT                  Lista de materiales completa de la placa PCB
│   ├── BOM_EXPANSION_BASIC_TMC2209.TXT     Placa de expansión, básica + TMC2209
│   ├── BOM_EXPANSION_BASIC_TMC5160A.TXT    Placa de expansión, básica + TMC5160A
│   ├── BOM_EXPANSION_ADVANCED_TMC2209.TXT  Placa de expansión, avanzada + TMC2209
│   ├── BOM_EXPANSION_ADVANCED_TMC5160A.TXT Placa de expansión, avanzada + TMC5160A
│   ├── BOM_EXPANSION_BASIC_ADS1115.TXT     Placa de expansión, básica + ADS1115 (solo sensor, sin driver/MCU)
│   └── BOM_EXPANSION_BASIC_MLX9064X.TXT    Placa de expansión, básica + MLX9064x (solo sensor, sin driver/MCU)
├── docs/
│   ├── CANBUS.TXT               Referencia del protocolo de bus CAN (todos los IDs de comando/telemetría)
│   ├── ECOVIA.TXT               Matriz de identificación de herramienta y lógica de mutación de pines
│   ├── TOOLS.TXT                Catálogo de alto nivel de las 25 herramientas - qué hace cada una y
│   │                            qué periféricos usa, sin detalle a nivel de pin
│   ├── PINOUT.TXT               Pinout completo del MCU, bloque por bloque
│   ├── PINOUT_CONNECTORS.TXT    Pinouts físicos de los conectores (CONN_DRILL, CONN_SEN, etc.)
│   ├── EXPANSION.TXT            Conector CONN_EXPANSION y las variantes de placa adicional
│   ├── PINOUT_SLAVE.txt         Pinout completo del chip esclavo de expansión (solo variantes avanzadas)
│   ├── EEPROM.TXT               Mapa completo de registros de la F-RAM (cada ajuste persistido, offsets de byte)
│   ├── COMPILE_STM32F303.TXT    Guía de compilación desde cero para los 4 binarios de firmware -
│   │                            toolchain, configuración de ST HAL/CMSIS, comandos exactos de
│   │                            compilación/enlazado; build_firmware.sh/.bat en la raíz del
│   │                            repositorio automatizan este mismo proceso de principio a fin
│   ├── datasheet/               2 datasheets de componentes no cubiertos ya bajo
│   │                            PCB/datasheet/ (CFM_40.pdf, EFB0424VHD-CP0.pdf)
│   └── tool_image_generator/    Toolkit que genera images/TOOL_*.png (ver abajo) -
│                                render_engine.py + tool_data.py + generate_all.py, y
│                                PROCEDURE.TXT explicando cómo añadir la imagen propia de una
│                                herramienta nueva o regenerar una existente
├── src/
│   ├── F303-master/
│   │   ├── STM32F303CC_main.c    Punto de entrada - definiciones globales y main()
│   │   ├── firmware_*.c/.h       ~85 archivos más, uno por subsistema (OLED, LEDs, manejadores
│   │   │                         CAN por herramienta, inicialización, persistencia, etc.), incluyendo
│   │   │                         firmware_ads1115.c (driver directo de ADS1115, placa
│   │   │                         Basic+ADS1115) - ver el propio README.md de esta carpeta para
│   │   │                         la tabla completa archivo por archivo
│   │   ├── melexis_mlx90640/     Biblioteca oficial propia de Melexis para MLX90640 (Apache-2.0,
│   │   │                         C plano) más el propio driver de conexión directa de esta placa
│   │   │                         encima de ella, para la placa de expansión Basic+MLX9064x
│   │   ├── melexis_mlx90641/     Misma idea, biblioteca MLX90641 (Apache-2.0, C++ - ver el
│   │   │                         propio README.md de esta carpeta, sección 8a, para por qué esta
│   │   │                         biblioteca es C++ en un proyecto por lo demás todo-C)
│   │   ├── melexis_mlx90642/     Misma idea, biblioteca MLX90642 (Apache-2.0, C plano) - ver la
│   │   │                         sección 8a para por qué el propio driver de este sensor es
│   │   │                         genuinamente más simple que el de los otros 2
│   │   ├── STM32F303CCTx_APP.ld  Script de enlazado para la aplicación (slot principal de 112K en 0x08008000)
│   │   ├── README.md             Referencia técnica: plataforma de hardware, el sistema de
│   │   │                         selección de herramienta por jumper de ID, el cableado de
│   │   │                         periféricos por herramienta - ver CANBUS.TXT para el protocolo
│   │   │                         a nivel de cable cuyo por qué explica esto
│   │   └── boot/
│   │       ├── bootloader_main.c  Punto de entrada del bootloader
│   │       ├── bootloader_*.c/.h  9 archivos más (tipos/constantes compartidos, criptografía,
│   │       │                      flash/metadatos, OLED, protocolo CAN)
│   │       ├── STM32F303CCTx_BOOTLOADER.ld  Script de enlazado para el bootloader (región de 30K en 0x08000000)
│   │       └── README.md          Mismo rol de referencia técnica que el de la aplicación, para el bootloader
│   └── F303-slave/               Chip acompañante (STM32F303CBT6) solo en las 2 variantes de placa
│       │                         de expansión AVANZADA - ver la sección Conector de Expansión de
│       │                         arriba. Par propio de bootloader/aplicación, protocolo de
│       │                         actualización propio basado en I2C (no CAN), versionado propio
│       │                         independiente.
│       ├── slave_main.c          Punto de entrada
│       ├── slave_*.c/.h          7 archivos más (tipos/constantes compartidos, protocolo de
│       │                         enlace I2C, bus de sensor local, PWM local)
│       ├── STM32F303CBTx_SLAVEAPP.ld  Script de enlazado (slot principal de 54K en 0x08005000)
│       ├── README.md             Referencia técnica: por qué existe este chip, el propio bus de
│       │                         sensores ADS1115/MLX9064x local, PWM local, el protocolo de
│       │                         enlace I2C hacia la placa principal
│       ├── melexis_mlx90640/     Biblioteca oficial propia de Melexis para MLX90640 (Apache-2.0,
│       │                         C plano, sin modificar, con su propio archivo de licencia) -
│       │                         mantenida como su propia unidad de compilación separada,
│       │                         deliberadamente nunca fusionada en el propio código fuente de
│       │                         este proyecto, ya que Apache-2.0 exige que el propio aviso de
│       │                         copyright de ese código se mantenga intacto
│       ├── melexis_mlx90641/     Biblioteca oficial propia de Melexis para MLX90641 (Apache-2.0,
│       │                         C++ - una biblioteca genuinamente separada de la propia de
│       │                         MLX90640, no una variante de ella - ver el propio README.md de
│       │                         esta carpeta, sección 3, para por qué es C++ y cómo lo maneja
│       │                         la build)
│       ├── melexis_mlx90642/     Biblioteca oficial propia de Melexis para MLX90642 (Apache-2.0,
│       │                         C plano) - interfaz de transporte genuinamente más simple que la
│       │                         propia de los otros 2 sensores, ver README.md sección 3 para
│       │                         saber por qué
│       └── boot/
│           ├── slaveboot_main.c   Punto de entrada del bootloader
│           ├── slaveboot_*.c/.h   7 archivos más (criptografía, flash/metadatos, protocolo)
│           ├── STM32F303CBTx_SLAVEBOOT.ld  Script de enlazado (región de 18K en 0x08000000)
│           └── README.md          Mismo rol de referencia técnica que el de la aplicación
├── firmware/
│   ├── URTC_BOOTLOADER.bin       Bootloader compilado, flashear en 0x08000000
│   ├── URTC_BOOTLOADER.elf       Bootloader compilado, flashear en 0x08000000
│   ├── URTC_BOOTLOADER.hex       Bootloader compilado, flashear en 0x08000000 (dirección incrustada)
│   ├── URTC_V1.1_F303CC.bin      Bin de aplicación compilado, flashear en 0x08008000
│   ├── URTC_V1.1_F303CC.elf      Elf de aplicación compilado, flashear en 0x08008000
│   ├── URTC_V1.1_F303CC.hex      HEX de aplicación compilado, flashear en 0x08008000 (dirección incrustada)
│   ├── URTC_SLAVE_BOOTLOADER.{bin,elf,hex}  Bootloader propio del esclavo de expansión, flashear en 0x08000000
│   │                             en el STM32F303CBT6 (solo placas de expansión avanzadas)
│   ├── URTC_SLAVE_APP.{bin,elf,hex}  Aplicación propia del esclavo de expansión, flashear en 0x08005000
│   └── firmware_manifest.json    Índice legible por máquina de los 4 componentes de arriba - versión,
│                                 dirección de flash, y el tamaño/CRC32 propio de cada archivo, para
│                                 que una herramienta externa compruebe qué hay aquí y qué es más
│                                 nuevo que lo que tenga actualmente. Regenerado automáticamente por
│                                 generate_manifest.py (llamado por el propio último paso de
│                                 build_firmware.sh/.bat) - nunca editado a mano.
├── images/
│   ├── OLED_DIRECT_MOUNT.jpg     LCD1/CONN_OLED2 - panel FPC desnudo de 30 pines, opción de montaje directo
│   ├── OLED_BREAKOUT_MODULE.jpg  CONN_OLED - módulo breakout I2C externo, opción alternativa
│   ├── URTC_LOGO.svg             Logo general del proyecto, incrustado al principio de este README
│   ├── URTC_BOARD.png           Foto de la placa
│   ├── URTC_SCHEMATIC.png       Esquemático de la placa
│   ├── URTC_PCB_TOP.png         Capa TOP de la placa (cuando se añada)
│   ├── URTC_PCB_BOTTOM.png      Capa BOTTOM de la placa (cuando se añada)
│   └── TOOL_*.png               Diagrama de referencia de jumper/cableado por herramienta, uno por perfil
│                                (los 25 presentes - ver el propio enlace de cada herramienta en el Catálogo de Herramientas de arriba)
├── PCB/
│   ├── URTC_V1.0.sch            Esquemático Eagle (cuando se añada)
│   ├── URTC_V1.0.brd            Layout de placa Eagle (cuando se añada)
│   ├── URTC_V1.0_JLCPCB.ZIP     Gerbers, archivos bom y cpl (cuando se añadan)
│   ├── URTC_BOM.TXT             BOM en bruto exportada de Eagle (exportación de referencia - ver
│   │                            BOM/BOM.TXT para la propia versión curada y organizada de este proyecto)
│   ├── datasheet/               Datasheets de todas las piezas usadas en la placa
│   └── *_PARLIST/PINLIST/NETLIST.TXT   Netlists exportadas de Eagle (referencia para el mapeo de pines)
├── VERSION_CHECKLIST.txt        Checklist mecánico para incrementar correctamente cualquiera de los
│                                propios 4 números de versión independientes de este proyecto
├── check_version_consistency.sh  Comprobaciones automatizadas de consistencia de versión/archivo - ejecutar antes
│                                de confiar en las propias afirmaciones de VERSION_CHECKLIST.txt
├── build_firmware.sh            Instala la toolchain, descarga el propio HAL/CMSIS de ST, y
│                                compila los 4 binarios de firmware de principio a fin (Linux)
├── build_firmware.bat           Igual, para Windows - ver docs/COMPILE_STM32F303.TXT para
│                                el proceso manual completo que automatiza cualquiera de los 2 scripts
├── generate_manifest.py         Regenera firmware/firmware_manifest.json - llamado
│                                automáticamente como último paso de una ejecución completa de
│                                build_firmware.sh/.bat, o de forma independiente en cualquier
│                                momento en que el manifiesto necesite ponerse al día sin una
│                                recompilación completa
├── LICENSE
├── README.md                    Este archivo
└── README_spa.md / README_ita.md / README_fra.md / README_deu.md  <- traducciones
```

Los archivos de diseño de hardware (esquemático/placa/netlists de Eagle) se añadirán a medida que el layout se estabilice.

## 🔗 Proyectos Relacionados

Este proyecto es parte de un ecosistema de robótica más grande del mismo autor (JuanenRac / Electro Hobby 3D). Vale la pena conocerlo, ya que una solicitud podría en realidad tratarse de uno de estos en vez de este repositorio:

**Plataforma HYDRA-UMC** — la célula de micro-fábrica multi-robot
- **[HYDRA-UMC](https://github.com/JuanenRac/HYDRA-UMC)** — la propia placa madre: host Raspberry Pi CM5 + coprocesador de tiempo real STM32H745 de doble núcleo, orquestando hasta 8 brazos robóticos distribuidos por CAN-OTA/SPI-OTA. Hardware + firmware propios, GPL-3.0/CERN-OHL-S v2/CC BY-SA 4.0.
- **[HYDRA-UMC STUDIO](https://github.com/JuanenRac/HYDRA-UMC-STUDIO)** — panel de control basado en web para HYDRA-UMC: visualización 3D multi-robot, grabación de cinemática/trayectoria, flasheo y pruebas CAN-OTA para toda la plataforma. React + Vite + Three.js.
- **[HYDRA-UMC-SERVER](https://github.com/JuanenRac/HYDRA-UMC-SERVER)** — el backend headless (Node/Express/WebSocket) que antes venía empaquetado dentro del propio proceso de HYDRA-UMC-STUDIO. Contiene la API REST/WS de control de robots, la persistencia de settings.json, la autenticación JWT y el descubrimiento mDNS. HYDRA-UMC-STUDIO es ahora un cliente frontend estático puro que se comunica con él por red.
- **[HYDRA-UMC-ANDROID-CONTROL](https://github.com/JuanenRac/HYDRA-UMC-ANDROID-CONTROL)** — app de control Android para HYDRA-UMC por Wi-Fi/Bluetooth. App real y funcional - conjunto completo de funciones de control remoto, autenticación JWT, almacenamiento cifrado de credenciales.
- **[HYDRA-UMC-IOS-CONTROL](https://github.com/JuanenRac/HYDRA-UMC-IOS-CONTROL)** — app de control iOS/iPadOS para HYDRA-UMC por Wi-Fi, construida en Flutter (multiplataforma, verificable en Windows sin necesitar un Mac; el empaquetado final `.ipa` todavía necesita Xcode). App real y funcional - mismo conjunto de funciones que la app de Android.
- **[HYDRA-UMC-SUITE](https://github.com/JuanenRac/HYDRA-UMC-SUITE)** — centro de comando de enjambre de escritorio (Python/PySide6): descubrimiento de red multi-controlador, sincronización bidireccional en vivo, viewport 3D real de robots, espacio de trabajo acoplable estilo Photoshop. Real y funcional, no un placeholder.
- **[HYDRA-UMC-EDITOR-URDF](https://github.com/JuanenRac/HYDRA-UMC-EDITOR-URDF)** — creador/editor gráfico de URDF de escritorio (Python/PySide6) para el propio catálogo de modelos de este proyecto: extrae archivos fuente desde GitHub o una carpeta local, valida la viabilidad de los DOF, edita color/escala/cinemática con una vista previa 3D en vivo, y sube el resultado final a un servidor STUDIO en ejecución. Real y funcional, no un placeholder.
- **[HYDRA-UMC-DSI](https://github.com/JuanenRac/HYDRA-UMC-DSI)** — UI táctil nativa en Flutter para la propia pantalla táctil DSI de 5"/7" de HYDRA-UMC (1280×720, misma resolución en ambos tamaños) en la Compute Module 5, controlando este mismo servidor directamente desde la placa. Scaffold real y funcional con las 6 pantallas del catálogo (dashboard, control manual, cámara, vista 3D simplificada, métricas de sistema, login) conectadas al servidor en vivo; el build real del target Linux aún no se ha ejecutado en hardware real (entorno de trabajo solo Windows hasta ahora - ver el README propio de ese proyecto).

**Plataforma URTC** — el controlador de cabezal de herramienta que lleva cada brazo robótico HYDRA-UMC
- **URTC** *(este repositorio)* — Universal Robot Tool Controller: controlador de cabezal de herramienta por bus CAN basado en STM32F303, 25 perfiles de herramienta totalmente implementados, actualización de firmware CAN-OTA.
- **[URTC Flasher](https://github.com/JuanenRac/URTC-FLASHER)** — herramienta de escritorio de flasheo CAN-OTA + chip completo por SWD/JTAG para placas URTC (Windows/Linux).
- **[URTC Tester](https://github.com/JuanenRac/URTC-TESTER)** — herramienta de diagnóstico en vivo de bus CAN de escritorio para placas URTC, un panel por perfil de herramienta (Windows/Linux).
- **[URTC Web Studio](https://github.com/JuanenRac/URTC-WEB-STUDIO)** — alternativa basada en navegador a las 2 herramientas de escritorio de arriba (Web Serial API + SLCAN), sin necesidad de instalación local.

## 👤 Autor

**JuanenRac** (Electro Hobby 3D)
📧 electrohobby3d@gmail.com
📺 [youtube.com/@electrohobby3d](https://youtube.com/@electrohobby3d)

## 📜 Licencia y Avisos de Copyright

URTC es (c) 2026 JuanenRac (Electro Hobby 3D). Este aviso debe incluirse en cualquier distribución de este proyecto o trabajos derivados.

Dado que este proyecto consiste en varios tipos distintos de contenido, las partes individuales están disponibles bajo licencias distintas - cada una adecuada a lo que realmente cubre, en vez de forzar una sola licencia a encajar en todo:

1. El **firmware** ubicado en `./firmware` (aplicación y bootloader CAN por igual) está disponible bajo la **GNU General Public License v3.0 (GPL-3.0)**. Texto completo en https://www.gnu.org/licenses/gpl-3.0.html.

2. Los **diseños de hardware** (archivos de esquemático/placa de Eagle, gerbers, y las piezas imprimibles en 3D bajo `./PCB` y `./3D`) están disponibles bajo la **CERN Open Hardware Licence v2 - Strongly Reciprocal (CERN-OHL-S v2)**. Texto completo en https://cern-ohl.web.cern.ch/.

3. La **documentación** (este README y sus propias traducciones - `README_spa.md`, `README_ita.md`, `README_fra.md`, `README_deu.md` - más los archivos de referencia bajo `./docs`) está disponible bajo **Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)**. Texto completo en https://creativecommons.org/licenses/by-sa/4.0/.

Si construyes sobre este proyecto, ten en cuenta la separación de licencias: los cambios de código al firmware deberían mantenerse GPL-3.0, las modificaciones de hardware deberían mantenerse CERN-OHL-S, y los derivados de documentación deberían mantenerse CC BY-SA - cada uno con atribución de vuelta a este proyecto.

Este repositorio cubre solo el propio firmware y hardware de la placa URTC - las herramientas de PC (URTC Flasher, URTC Tester) que solían vivir aquí son ahora proyectos independientes con su propia licencia, ver "Herramientas de PC" arriba.
