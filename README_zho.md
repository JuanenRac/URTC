<p align="center">
  <img src="images/URTC_BANNER.svg" alt="URTC banner" width="100%">
</p>

# 🚀 URTC — 通用机器人工具控制器（v0.3）

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  🇨🇳 <b>简体中文</b> |
  <a href="README_jpn.md">🇯🇵 日本語</a> |
  <a href="README_por.md">🇵🇹 Português</a>
</p>


<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Hardware-CERN%20OHL--S-orange.svg" alt="CERN OHL-S">
  <img src="https://img.shields.io/badge/Language-C-00599C.svg" alt="C">
  <img src="https://img.shields.io/badge/Platform-STM32F303-003551.svg" alt="STM32">
  <img src="https://img.shields.io/badge/Bus-CAN-yellow.svg" alt="CAN">
</p>


> **⚠️ 安全提示（设计阶段，尚无板卡运行过此固件）：** 本板驱动一个**10W 雕刻激光二极管**以及多个加热级（T12 电烙铁头、3D 打印机热端）。制作和使用它意味着要操作在缺乏适当安全措施（针对该激光二极管波长的护目镜、热防护、可及的电源切断装置）时可能造成**烧伤、火灾或眼部伤害**的设备。这是一个按原样分享的爱好者/创客项目——自行承担构建和使用的风险，不要仅仅因为固件带有看门狗机制就跳过基本的安全规范（这些是软件看门狗，从未在真实板卡上运行过）。

大家好！我想分享一个我一直在开发的项目，叫做 URTC（通用机器人工具控制器）。它是一块单体式、高度集成的控制板，专为扩展机械臂和自动化装置的能力而设计，非常适合像 PAROL6 和 Faze4 这样的平台——这两款开源机械臂由 [Source-Robotics](https://source-robotics.com/)（[GitHub](https://github.com/Source-Robotics)）设计开发。

**URTC 是一个独立的、非官方的项目。** 它不是由 Source-Robotics 开发或认可的——它是一款兼容的工具头控制器，设计用来与 PAROL6 和 Faze4 良好协作，同样基于 CAN 的架构也可以开放用于适配其他机械臂平台。

以下是关于它是什么、能做什么，以及它目前所管理的硬件生态系统的完整分解。

**状态：🚧 积极演进中的项目——已有发布版本，但没有任何东西在真实板卡上运行过。** 已有带标签的发布版本（v0.3.x），每个都包含由本源码构建的固件镜像，但不存在已组装的板卡，因此固件从未在真实板卡上运行过。 URTC 正同时在两条战线上持续、积极地开发：固件（新的工具配置文件、扩展从属生态系统、协议变更）和硬件（原理图和 BOM 仍在最终确定中，尚不存在已实际组装的板卡）。由于两侧始终同步推进，本仓库中在任何给定时刻的内容都是正在进行中工作的快照，而非一个稳定的、有版本号的产品——文件名、文件夹结构、工具数量和文档在设计稳定下来之前都仍可能变化。一旦固件和硬件都真正达到稳定、在真实硬件上经过验证的状态，就会打上一个正式的 **Release** 标签，将所有内容（固件、引导程序、PC 工具、硬件设计文件和文档）作为一个连贯的、冻结的快照捆绑在一起。在那之前，请将 `main` 视为它本身——一个正在积极移动的目标。

---

> **安全与安防现状:**本生态系统不是经认证的安全系统,目前也不存在已组装的 URTC 电路板。引导程序使用 CRC32 和共享密钥的 HMAC-SHA256 校验固件映像,而非非对称签名(计划中);已发布的应用映像使用公开的开发密钥签名(`signing_key_kind: dev`),因此任何人都可以生成引导程序接受的映像;尚未向任何真实电路板发送过任何内容。在依赖此处任何说法来保障人身安全之前,请先阅读[安全基线](https://github.com/JuanenRac/URTC-DOCS/blob/main/docs/SECURITY_BASELINE.md)和[威胁模型](https://github.com/JuanenRac/URTC-DOCS/blob/main/docs/THREAT_MODEL.md)。

## 📋 速览

| | |
|---|---|
| 版本 | `0.4.1` |
| 成熟度 | 可用 |
| 技术栈 | STM32F303 / bxCAN / 25 tool profiles |
| 目标 | 微控制器板（固件）；在电脑上构建 |
| 真实硬件 | 尚未：此版本没有在真实硬件上运行的记录 |

## ⚙️ 什么是 URTC？

URTC 是一块由 STM32 微控制器（STM32F303CCT6，LQFP48）驱动的一体化、紧凑型控制板。它通过 CAN 总线与主机器人控制器通信，旨在让工具头或轴端以低延迟执行复杂任务（延迟从未在真实板卡上测量过，也不保证硬实时）。它配备了一块板载 OLED 显示屏用于即时诊断——动画开机画面、每个工具的动画图标、双色面板上的实时遥测——一个单像素 RGB 状态 LED，加上一圈可寻址 RGB LED 环用于摄像头照明，一个 20 针扩展接口用于附加板，一块板载 F-RAM 在断电时保留当前工具的设定值，以及专用的模拟和大电流电源级。

## 🛠️ 可扩展架构与工具矩阵

URTC 的核心优势在于其极强的多功能性。板卡采用可扩展矩阵架构，而不是为每一项不同的工作更换电子设备：

* **32 地址识别方案：** 硬件和通信协议设计为通过一个 5 位焊接跳线 ID 矩阵（ID0-ID4），直接在机器人头部识别多达 32 种不同的工具或末端执行器。这 32 种读数中，31 种直接映射到一个工具配置文件；第 32 种（全部 5 个跳线均安装，`11111`）则保留作为“自由配置”地址——见下文。
* **25 个即插即用自动化配置文件：** 固件原生支持 25 种工具配置文件——板卡读取工具头的物理身份，无需完整重新刷写即可无缝配置电源级、传感器和逻辑切换。在现有方案内还有 6 个地址保留给未来的工具配置文件。
* **自由工具配置：** 保留的 `11111` 跳线读数不会选定一个固定的工具——它会告诉板卡改为从其自身持久化的 F-RAM 中的一个寄存器查找应使用哪个工具，该寄存器通过 CAN（经由 `URTC Flasher`）提前设置。适用于需要重新编程为不同工具、而无需物理重新焊接跳线的板卡。完整机制见 `docs/EEPROM.TXT` 第 5 节。

## 🔌 硬件灵活性与电机支持

为应对如此广泛的应用场景，URTC 硬件完全具备控制以下设备的能力：

* **NEMA 步进电机：** NEMA 8、11、14 和 17 直接由板载 TMC2209 驱动，NEMA 23 和 34 也同样如此——通过主板自身的驱动级，任意一款均可达 **2.0A**。若 NEMA 23/34 需要达到其额定的全部扭矩，扩展接口上的 TMC5160（见下文）支持高达 **10A**，其电流大小取决于该板所选用的外部 MOSFET/检测电阻——一旦电机移至扩展驱动器，板载的 2.0A 限制便不再适用。
* **三相无刷 / 云台电机**，用于高精度运动。
* **带霍尔传感器和转速计的电机**，用于闭环控制。
* 面向反射式光电接近传感器（如 TCRT5000）的**专用输入**，加上一个通用的低电平有效限位/限位开关输入，在四种工具配置文件间共享。

## 🧩 扩展接口

一个独立于各工具专用接口的 20 针接口，供需要超出单一工具配置文件所暴露内容的附加板使用——例如一个额外的步进轴（TMC2209 或 TMC5160）、第二块传感器板等等。

| 引脚数 | 信号 |
|---|---|
| 4 | 24V |
| 1 | 3.3V |
| 1 | 5V |
| 3 | GND |
| 2 | 位模拟 I2C（SCL/SDA）——它自己的总线，独立于 OLED/F-RAM 的硬件 I2C2 |
| 3 | STEP/DIR/EN——对下方两种驱动芯片通用 |
| 4 | 位模拟 SPI（CS/SCK/MISO/MOSI）——用于 TMC5160 的配置/诊断接口，或任何其他可通过 SPI 配置的芯片 |
| 1 | 通用 GPIO（若未来某个附加板需要快速的传感器响应，例如限位开关，则可作为支持 EXTI 的中断输入） |
| 1 | TMC5160 DIAG0（失速/故障诊断线，通过 `0x182`/`0x183` 轮询） |

共计 20 针。

**刻意设置两条独立的 I2C 总线：** OLED/F-RAM 使用该芯片唯一可用的硬件 I2C 外设（I2C2，位于 PA9/PA10）；扩展接口则获得自己独立的位模拟 I2C 总线（PB10/PB11——该芯片其余唯一支持 I2C 的引脚对已经用于其他功能，因此位模拟是在没有硬件冲突的情况下让此接口拥有自己总线的方式）。挂载在扩展接口上的任何设备——一个 I2C ADC/DAC、一个端口扩展器，或某个附加板所需的任何设备——都与其他任何扩展侧 I2C 设备共享这条位模拟总线，但不能拉伸时钟或以其他方式干扰 OLED 自身在其独立的硬件 I2C2 总线上的时序。

**要么是 TMC2209，要么是 TMC5160，不必两者兼有。** 两颗芯片对于实际运动都使用相同的 STEP/DIR/EN 接口，因此这部分是通用的。它们的区别在于配置/诊断方式：TMC2209 使用自己的单线 UART，而 TMC5160 使用 SPI——由于两者在任意一块扩展板上是互斥的，那 4 根 SPI 引脚也自然充当了 TMC2209 单条 UART 线的归宿，而不需要另一个专用引脚（该引脚永远不会与 SPI 总线同时使用）。这条位模拟 SPI 总线所使用的正是 TMC5160 所期望的确切协议（SPI 模式 3，MSB 在前，整个事务期间 CS 保持低电平——驱动它的通用字节透传指令见 `docs/CANBUS.TXT` 中的 `0x180`/`0x181`），而无需本固件了解该芯片具体的寄存器布局。TMC5160 的 DIAG0 失速/故障线也已接好（`0x182`/`0x183`）——它复用了两个通用 GPIO 引脚之一，而这两个引脚本来就是为这类快速的中断驱动输入预留的。

完整的逐引脚细节——哪个 MCU 引脚对应哪个信号，以及该芯片 48 针封装背后几处布局约束的原因——见 `docs/PINOUT_CONNECTORS.TXT` 和 `src/F303-master/README.md`。

### 6 种扩展板变体

6 种扩展板变体中有 4 种带有步进驱动器——要么是 TMC2209（每相高达 2A，集成功率 MOSFET），要么是 TMC5160A（每相高达 10A+，需要驱动器本身不包含的 8 颗外部功率 MOSFET）。与驱动器选择无关，带驱动器的板要么是**基础型**（仅驱动器 + 接口，无 MCU——STEP/DIR/EN 直接从主板路由过来），要么是**高级型**（增加第二颗微控制器 STM32F303CBT6，加上 2 颗本地传感器芯片——一个 16 位 ADS1115 ADC 和一个 MLX9064x 系列热成像相机——以及针对时序需要在工具头本地生成而非通过线缆路由的工具的本地 PWM 生成）。2×2 种组合，再加上 2 种仅带传感器的基础板（ADS1115 或 MLX9064x，直接接线到主板自身的 STM32F303CC，没有驱动器也没有从属 MCU），供只需要这两颗芯片之一、且不需要高级板所携带的其他任何内容的工具使用——共计 6 块板——见 `BOM/BOM_EXPANSION_*.TXT`（6 个文件）、`docs/EXPANSION.TXT` 和 `docs/PINOUT_SLAVE.txt`。

高级变体自身的 STM32F303CBT6 通过上述扩展接口既有的位模拟 I2C 总线与主板通信——主板作为主机，从属芯片作为真正的硬件 I2C 从机应答——并驱动其自身第二条、仅供本地使用的 I2C 总线以连接那 2 颗传感器芯片。它拥有自己的引导程序和应用固件，更新方式与主板相同（来自 `URTC Flasher` 的 CAN-OTA），只是经由那条 I2C 链路中继，而非直接触达从属芯片。完整技术细节见 `src/F303-slave/README.md` 和 `src/F303-slave/boot/README.md`。

## 💾 参数持久化

一块板载的 FM24CL64B F-RAM（64Kbit，I2C）保存着当前工具设定值以及全局 LED/OLED 设置的定期更新快照，因此突然的断电不会让“这块板当时在做什么”像断电本身一样无从得知。它共享 OLED 的硬件 I2C2 总线，而不是拥有自己的一条——本 MCU 为此目的只有一个可用的硬件 I2C 外设，已经被 OLED 占用了（完整推理见 `src/F303-master/README.md` 第 6 节）。

**恢复的状态可查询，但绝不会自动应用于任何有危险性的内容。** 开机时，任何已保存的内容都可通过 CAN 读取（`0x190`/`0x191`）——但加热器设定值、激光功率或电机指令绝不会在自身悄然重新启用。只有安全的、被动性的设置（LED 颜色、OLED 模式）会被直接恢复。是否在真正审查了发生了什么之后再刻意重新发送一个设定值，留给主控制器自行决定，而不是由本板在电源恢复的那一刻自行决定。

## 💼 原生自动化工具目录（25 种固件配置文件）

通过其动态切换逻辑，固件原生管理以下工具头：

1. **焊接工作站（T12）：** 使用直接 ADC 反馈实现带滞回的开关式温度控制（不是 PID），处理标准 T12 烙铁头，加上一个将焊丝送入焊点的电动送丝器（与下方纯运动类工具共享 `CONN_MOT` 及其步进协议——为腾出空间，牺牲了本工具自身的通用限位输入）。[跳线/接线配置 →](images/TOOL_SOLDERING_IRON.png)
2. **SMT 焊膏点胶器：** 用于 PCB 上精确焊膏沉积的毫米级送料控制。[跳线/接线配置 →](images/TOOL_PASTE_DISPENSER.png)
3. **导热膏/液体点胶器：** 针对高粘度膏体或液态胶粘剂的流动性管理。[跳线/接线配置 →](images/TOOL_LIQUID_DISPENSER.png)
4. **智能电动螺丝刀：** 通过步进接口进行旋转和停止控制。计划中：基于扭矩限制的停止（目前还没有扭矩检测）。[跳线/接线配置 →](images/TOOL_SCREWDRIVER.png)
5. **真空/气动夹爪：** 真空泵控制和压力等级读取，用于安全的拾取放置操作。[跳线/接线配置 →](images/TOOL_VACUUM_PICKUP.png)
6. **钻头（BL4260）：** PWM 转速控制、方向切换，以及带在线转速读数的动态电子制动，配有独立于步进工具驱动器使能线的专用使能/制动线。提供通用限位输入。[跳线/接线配置 →](images/TOOL_DRILL.png)
7. **云台夹爪：** 使用三相无刷云台电机的高灵敏操控。[跳线/接线配置 →](images/TOOL_GRIPPER_GIMBAL.png)
8. **NEMA 夹爪：** 通过重型步进电机控制的稳固夹紧力。[跳线/接线配置 →](images/TOOL_GRIPPER_NEMA.png)
9. **AOI（自动光学检测）系统：** 面向机器视觉相机拍摄的 LED 照明阵列的同步频闪控制。提供通用限位输入。[跳线/接线配置 →](images/TOOL_AOI_INSPECTION.png)
10. **雕刻激光二极管（10W 光学）：** 带软件 CAN 看门狗的 PWM 光束功率调制，主机通信丢失时锁定激光输出（仅完成构建，从未在真实板卡上运行，也不能代替独立的硬件联锁）。提供通用限位输入。[跳线/接线配置 →](images/TOOL_LASER_ENGRAVER.png)
11. **3D 打印热端：** 加热管的带滞回开关式控制、NTC 热敏电阻读数、挤出机控制，以及一个专用的 25kHz PWM 控制层冷却风扇（4 线，转速计反馈，自身的通信看门狗）——全部集成于一个模块中。[跳线/接线配置 →](images/TOOL_3D_PRINTER.png)
12. **3D 扫描探针：** 超快的硬件中断输入（EXTI），具有绝对优先级，用于无延迟的实时表面数字化和碰撞感应。也涵盖计量接触式探测——同一硬件路径，同一工具头上的不同物理探针。[跳线/接线配置 →](images/TOOL_SCAN_PROBE.png)
13. **SMT 拾取放置头：** 旋转 A 轴用于正确的焊盘对齐，与上方膏体/液体点胶器及两款夹爪共用同一步进接口。[跳线/接线配置 →](images/TOOL_SMT_PICKPLACE.png)
14. **重型电磁铁：** 针对铁磁性零件的通/断吸附控制，复用 T12 加热器输出作为通用 GPIO 驱动器。[跳线/接线配置 →](images/TOOL_ELECTROMAGNET.png)
15. **点焊头：** 针对电池组镍片的毫秒级精确焊接脉冲，由表面接触传感器控制脉冲的门控。[跳线/接线配置 →](images/TOOL_SPOT_WELDER.png)
16. **三防涂层喷枪：** 针对成品 PCB 的保护涂层喷涂控制——喷阀及其自身传感器位于机器人自身主板上，不在本板范围内。[跳线/接线配置 →](images/TOOL_CONFORMAL_COATING.png)
17. **大幅面真空夹爪：** 用于未装配 FR4 板的多吸盘阵列，与上方工具 #13 共用同一步进接口。[跳线/接线配置 →](images/TOOL_VACUUM_GRIPPER_LG.png)
18. **功能测试头：** 飞针电压/导通测试——基础读数来自板载 ADC，高级读数则通过**高级**扩展板上的 16 位 ADS1115 ADC。[跳线/接线配置 →](images/TOOL_FLYING_PROBE.png)
19. **UV 固化头：** 用于即时胶水/掩膜固化的大功率 UV LED 驱动器。[跳线/接线配置 →](images/TOOL_UV_CURING.png)
20. **热风返修喷嘴：** 加热元件、涡轮鼓风机和热电偶反馈，用于对偏位的贴片元件进行回流焊——共享电烙铁自身的热控回路。[跳线/接线配置 →](images/TOOL_HOTAIR_REWORK.png)
21. **气动压装插入器：** 用于将连接器压入 PCB 的线性执行器控制——执行器及其自身传感器位于机器人自身主板上，不在本板范围内。[跳线/接线配置 →](images/TOOL_PRESSFIT_INSERTER.png)
22. **线束/压接执行器：** 用于剥线/压接端子的高扭矩钳口，由**扩展板自身的驱动器**驱动，而非主板的驱动器。固件中没有扭矩或力检测。[跳线/接线配置 →](images/TOOL_CRIMPING_ACTUATOR.png)
23. **PCB 高级检测：** 热成像（MLX9064x 系列阵列——目前支持全部 3 个系列成员，MLX90640/MLX90641/MLX90642，可通过**高级**扩展板自身的从属芯片，或直接接线到主板的**基础型** MLX9064x 扩展板）用于根据温度特征发现短路，配合环形 LED 照明。也涵盖微型主轴分板——同一钻头硬件路径，用于不同工作的不同环节。[跳线/接线配置 →](images/TOOL_THERMAL_INSPECTION.png)
24. **焊膏喷射阀：** 压电微滴分配，亚毫秒级脉冲精度，在**高级**扩展板上本地生成。[跳线/接线配置 →](images/TOOL_PASTE_JETTING.png)
25. **超声波焊接/包装封口机：** 用于塑料外壳焊接的高频换能器触发。[跳线/接线配置 →](images/TOOL_ULTRASONIC_WELDER.png)

*（工具 1-12 已有配置图；工具 13-25 的图像将随硬件文档跟进而逐步补充——上方文件名与 `images/` 中已使用的命名惯例一致。）*

## 🖥️ 本地 OLED 界面

每个工具头都会在一块 128×64 双色 OLED 上显示实时的、针对该工具的遥测数据：开机时的动画开机画面、闪烁的 CAN 活动指示器、顶部条带上的实时“主要”读数（温度、转速、功率——对当前工具而言最重要的内容），以及每个工具配置文件对应的一个小型四帧动画图标。

### 模组

下方两种物理变体在电气上是同一块面板（由 SSD1306 或 SSD1315 驱动——固件的初始化序列已验证与两者兼容，见 `firmware_oled_driver.c` 中的 `OLED_Init()`；SSD1315 是一款更新的、可直接替代的控制器，如今许多模组在同一“SSD1306”标注/丝印下出货的其实是它），均为 **128×64**，且都是相同的双色“黄/蓝”分区，其中物理 LED 材料本身被分成两个固定颜色区域（这不是软件可选的）：

* **顶部 16 像素（第 0-1 页）：黄色。** URTC 用这一条带显示最便于一瞥即知、无需仔细阅读的内容——CAN 活动指示器、实时主要读数，或（在开机画面/无效工具界面上）简短的状态文字。
* **底部 48 像素（第 2-7 页）：蓝色。** 其余的一切——工具图标、详细遥测、开机画面上的动画 JuanenBOT 表情、大号闪烁的 ERROR 字样。

两者都接在同一条 I2C2 总线上，使用同一个 `OLED_Init()`——固件无法分辨究竟连接了哪一种，也不需要分辨。在同一块板上两者互斥（见 `BOM/BOM.TXT` 中 `CONN_OLED2` 的注释——本文档中对应原理图所称的 `LCD1`）。

#### 选项 A —— 直接安装（`CONN_OLED2`，板上实际焊接的封装）

<img src="images/OLED_DIRECT_MOUNT.jpg" width="220">

一块没有独立分线 PCB 的裸面板——仅有玻璃基板及其 30 针 FPC 排线，直接焊入 `CONN_OLED2` 封装（`FPC30`，WiseChip UG-2864，本文档中对应原理图所称的 `LCD1`——见 `BOM/BOM.TXT` 和 `URTC_NETLIST.TXT`）。这 30 针中只有一部分实际接线——其余是该面板的并行接口总线（`D2`–`D7`、`RW`、`E/!RD`），由于本板始终仅通过 I2C 与其通信，故留空未接：

| CONN_OLED2 引脚 | 网络 | 功能 |
|---|---|---|
| 1, 8, 29, 30 | GND / AGND | 地 |
| 9 | VDD | 逻辑电源（来自 `+3V3B`，专供 OLED 使用的电源轨——见 BOM §1） |
| 28 | VCC | 面板电源 |
| 2–5 | C2P/C2N/C1P/C1N | 电荷泵电容——BOM 中的 `C26`/`C27` |
| 26 | IREF | 参考电流设定电阻 |
| 27 | VCOMH | 内部共模电压去耦 |
| 10, 12 | BS0, BS2 | 接地 |
| 11 | BS1 | 接 `+3V3B` |
| 18 | D0/SCK | I2C2 SCL——PA9 |
| 19 | D1/DIN/SDA | I2C2 SDA——PA10 |

`BS0`/`BS1`/`BS2` 是该面板自身的接口选择跳接（此处为 GND/VCC/GND），在硬件上固定，而非暴露给 MCU——正是这一设置首先将控制器置于 I2C 模式，而不是其余 22 个 FPC 引脚所属的 8080/6800 并行模式。

#### 选项 B —— 分线模组（`CONN_OLED`，外部替代方案）

<img src="images/OLED_BREAKOUT_MODULE.jpg" width="220">

同一块面板预先安装在一块带 4 针接口的小型载板上——如果你更愿意接一个现成模组而不是自行采购裸 FPC 面板，这会很有用。直接接到 `CONN_OLED`，无需交叉连接——该模组自身的引脚顺序（`GND · VDD · SCK · SDA`）与 `CONN_OLED` 的引脚定义逐针精确匹配：

| OLED 模组引脚 | CONN_OLED 引脚 | 信号 |
|---|---|---|
| GND | 1 | 地 |
| VDD | 2 | +3.3V（显示逻辑电源） |
| SCK | 3 | SCL——PA9，硬件 I2C2 |
| SDA | 4 | SDA——PA10，硬件 I2C2 |

### 开机画面

<img src="ani/splash_boot.gif" width="480">


### 工具图标（每个配置文件一个，4 帧动画）

<table>
<tr>
<td align="center"><img src="ani/00_soldering_iron.gif" width="80"><br>T12 电烙铁</td>
<td align="center"><img src="ani/01_paste_dispenser.gif" width="80"><br>焊膏点胶器</td>
<td align="center"><img src="ani/02_liquid_dispenser.gif" width="80"><br>液体点胶器</td>
<td align="center"><img src="ani/03_screwdriver.gif" width="80"><br>螺丝刀</td>
</tr>
<tr>
<td align="center"><img src="ani/04_vacuum_pickup.gif" width="80"><br>真空拾取</td>
<td align="center"><img src="ani/05_drill.gif" width="80"><br>钻头（BL4260）</td>
<td align="center"><img src="ani/06_gripper_gimbal.gif" width="80"><br>云台夹爪</td>
<td align="center"><img src="ani/07_gripper_nema.gif" width="80"><br>NEMA 夹爪</td>
</tr>
<tr>
<td align="center"><img src="ani/08_aoi_inspection.gif" width="80"><br>AOI 检测</td>
<td align="center"><img src="ani/09_laser_engraver.gif" width="80"><br>激光雕刻机</td>
<td align="center"><img src="ani/10_3d_printer.gif" width="80"><br>3D 打印热端</td>
<td align="center"><img src="ani/11_scan_probe.gif" width="80"><br>3D 扫描探针</td>
</tr>
<tr>
<td align="center"><img src="ani/12_smt_pickplace.gif" width="80"><br>SMT 拾取放置</td>
<td align="center"><img src="ani/13_electromagnet.gif" width="80"><br>电磁铁</td>
<td align="center"><img src="ani/14_spot_welder.gif" width="80"><br>点焊头</td>
<td align="center"><img src="ani/15_conformal_coating.gif" width="80"><br>三防涂层</td>
</tr>
<tr>
<td align="center"><img src="ani/16_vacuum_gripper_lg.gif" width="80"><br>大幅面真空夹爪</td>
<td align="center"><img src="ani/17_flying_probe.gif" width="80"><br>飞针测试</td>
<td align="center"><img src="ani/18_uv_curing.gif" width="80"><br>UV 固化</td>
<td align="center"><img src="ani/19_hotair_rework.gif" width="80"><br>热风返修</td>
</tr>
<tr>
<td align="center"><img src="ani/20_pressfit_inserter.gif" width="80"><br>压装插入器</td>
<td align="center"><img src="ani/21_crimping_actuator.gif" width="80"><br>压接执行器</td>
<td align="center"><img src="ani/22_thermal_inspection.gif" width="80"><br>热成像检测</td>
<td align="center"><img src="ani/23_paste_jetting.gif" width="80"><br>焊膏喷射</td>
</tr>
<tr>
<td align="center"><img src="ani/24_ultrasonic_welder.gif" width="80"><br>超声波焊接</td>
</tr>
</table>


### 无效工具 ID 警告

如果 ID 跳线不匹配 25 个已分配配置文件中的任何一个，板卡会阻断所有执行器并改为闪烁以下画面：

<img src="ani/error_warning.gif" width="480">

所有动画源 GIF 位于 [`/ani`](ani/) 中。

## 🔴🟢🔵 数字状态 LED

与 OLED 及 8 像素照明环分开，`CONN_LED1` 携带一颗独立的可寻址 RGB LED（WS2812B 系列，SPI/DMA 驱动），专用于一目了然的状态显示。

**默认自动，可按需由主机覆盖。** 固件按三级优先级自行为该 LED 着色：

* 🔴 **红色** —— 一个硬件故障处于激活状态（`system_error_flag`）。无论其他情况如何，始终优先。
* 🔵 **蓝色** —— 板卡正在积极运作：过去 1.5 秒内收到过一个 CAN 帧（任意 ID）。
* 🟢 **绿色** —— 空闲，等待指令：超过 1.5 秒没有 CAN 流量。

主机随时仍可通过发送 CAN ID `0x100`（DLC 8）来覆盖此逻辑，前三个字节分别为红、绿、蓝的强度（每个 0-255——完整的 24 位色彩，而不仅是那三种自动颜色）。主机发送的颜色会保持 10 秒，之后回落到自动方案——足够长以便真正被看到，也足够短以确保如果主机停止更新它，板卡不会卡在显示一个陈旧的自定义颜色上。再次发送 `0x100`（无论是相同颜色还是新颜色）都会刷新这个 10 秒窗口，因此想要保持自定义控制的主机只需定期持续发送即可。硬件故障始终会中断一个正在生效的覆盖——红色优先于主机所设置的任何颜色。

确切的字节布局见 `docs/CANBUS.TXT`（ID `0x100`），该消息也与环形 LED 及 OLED 夜间模式控制共用。

## 📸 照片

![URTC v1.0](images/URTC_BOARD.png)

*（进行中——更多角度和已组装板卡即将上线。）*

## 🔧 构建与刷写

URTC 的闪存被拆分为两个独立部分，因此板卡可以通过它已经用于其他一切事务的同一条 CAN 脐带线重新刷写——初次设置之后，再也不需要物理接触 JTAG/SWD 接口。

### 闪存布局（总计 256K，黄金镜像/A-B 更新模型）

```
0x08000000 ┌─────────────────────────────────┐
           │  引导程序（30K）                  │  每次启动时始终首先运行。
           │                                   │  在 CAN 上短暂监听，然后要么
           │                                   │  跳转到应用程序，要么等待
           │                                   │  更新。更新期间直接驱动
           │                                   │  OLED（见下文）。
0x08007800 ├─────────────────────────────────┤
           │  元数据页（2K）                   │  描述主槽当前的内容：
           │                                   │  硬件 ID、版本、大小、CRC32，
           │                                   │  以及一个 HMAC-SHA256 签名。
           │                                   │  引导程序在跳转到应用程序
           │                                   │  之前会检查全部这些内容。
0x08008000 ├─────────────────────────────────┤
           │  主槽（112K）                     │  这是应用固件 /
           │                                   │  URTC_MAIN_FIRMWARE_v<version>.* ——
           │                                   │  日常运行的实际固件，
           │                                   │  本 README 其他部分描述的
           │                                   │  正是它。在一个经过验证的、
           │                                   │  已知良好的镜像准备好替换
           │                                   │  它之前，绝不会被更新触碰。
0x08024000 ├─────────────────────────────────┤
           │  备份/暂存槽（112K）               │  仅作原始存储，从不
           │                                   │  被直接执行。每次 CAN 更新
           │                                   │  都会先写入这里。
0x08040000 └─────────────────────────────────┘
```

**为什么需要一个备份槽。** 一次 CAN 更新绝不会写入当前正在运行的那个槽。它会先写入备份槽，并在那里被完整验证——大小、CRC32，以及一个使用共享对称密钥的 HMAC-SHA256 校验，表明它由持有该密钥的人生成（已发布的镜像使用公开的开发密钥），而不仅仅是完整到达——只有那之后才会被复制到主槽中。在这次复制开始之前的任何时刻发生断电，都会让当前运行的固件完全不受影响，因此不存在一个被中断的下载可能使板卡变砖的窗口期。如果断电恰好发生在复制过程*本身*期间，引导程序会在下一次启动时注意到这一点（备份槽在复制期间从未被触碰，依然完整无损），并简单地从那里恢复复制，直到成功为止。

### 0. 从源码编译（可选——预构建的二进制文件是每个 GitHub 发布的资源，而不是仓库中的文件）

从本仓库的源码得到上述 4 个二进制文件有两种方式：

- **自动化：** 仓库根目录下的 `build_firmware.sh`（Linux）或 `build_firmware.bat`（Windows）。两者都会在 ARM GNU 工具链缺失时安装它，获取固定版本的 ST HAL/CMSIS 提交，并将全部 4 个二进制文件（主板应用 + 引导程序，扩展从属应用 + 引导程序）编译、链接、`objcopy` 直接输出到 `firmware/`，然后重新生成 `firmware/firmware_manifest.json`。不带参数运行进行完整构建，`--clean` 先清空本地 `build/` 缓存，或 `master`/`slave` 只构建某一颗芯片自身的一对文件。`build_firmware.sh` 针对本项目真实的源码树端到端运行；`build_firmware.bat` 在 Windows 上镜像同样的逻辑——如果两者出现分歧，以 `.sh` 脚本的逻辑为准。
- **手动：** 无论哪个脚本运行的每一条命令，以及每一项工具链/HAL 选择背后的推理，都在 `docs/COMPILE_STM32F303.TXT` 中逐步详细列出——适用于不同的操作系统、不同的 HAL/CMSIS 源，或只是想确切看看这些脚本自动化了什么。
- **发布资源：** 固件映像（`.bin`、`.hex`、`.elf` 及其校验和与清单文件）不纳入 git 版本管理。`build_firmware.sh`/`.bat` 将它们写入 `firmware/`（已被 git 忽略；CI 模式即 `URTC_CI=1` 时写入 `build/firmware`），`python tools/publish_release_assets.py` 会检查整套文件并输出带有待附加文件的准确 `gh release create` 命令。它们仅作为每个版本的 GitHub 发布资源发布。 谁读取什么：[`docs/FIRMWARE_DISTRIBUTION.md`](docs/FIRMWARE_DISTRIBUTION.md)。

每次固件源码更改之后（或在信任一次版本递增之前），从仓库根目录运行 **`check_version_consistency.sh`**：它以 Track A/E 的版本常量（主板固件、扩展从属应用）作为权威来源，检查 `VERSION_CHECKLIST.txt` 为该版本标签记录的每一处位置，报告任何不匹配之处——它只报告，不会自行修复任何内容。`VERSION_CHECKLIST.txt` 是本项目所携带的全部 5 条独立版本轨道（主固件、硬件/PCB、主引导程序、扩展从属应用、扩展从属引导程序）的完整参考，以及递增其中任意一条时确切需要修改的内容。

### 1. 首次设置——需要 JTAG/SWD（一次性）

引导程序只能通过物理编程进入芯片——没有办法对一块尚未拥有引导程序的板卡进行 CAN 刷写。这是一次性步骤：

1. 在 **STM32CubeIDE** 中打开项目（已针对 STM32F303CC 目标构建和测试），或直接使用 **STM32CubeProgrammer** 配合下方的编译输出。
2. 通过板载 `STM_JTAG` 接口，经由 SWD（ST-Link）刷写**两个**镜像——每个 `.hex` 文件都已内置目标地址，因此大多数工具（包括 STM32CubeProgrammer）都可以在同一会话中加载两者：
   * `URTC_MAIN_BOOTLOADER_v<version>.hex` → `0x08000000`
   * `URTC_MAIN_FIRMWARE_v<version>.hex` → `0x08008000`
3. 在通电之前通过 ID 焊接跳线设置工具身份——板卡在开机时读取一次，一如既往。5 个跳线（ID0-ID4），覆盖完整的 32 地址空间（31 个直接工具地址，加上保留的 `11111` 自由配置地址——见上方工具矩阵一节）。
4. 通电。引导程序监听约 600ms，未发现任何内容，直接跳转进入应用程序——从此刻起，一切行为都与本 README 其余部分所描述的完全一致。该窗口在引导程序检查完已存储的应用之后才开始，想要更新的工具必须在窗口内发送开始更新帧（0x7F1）；监听状态（0x7F5 = 0x01）在窗口结束且未跳转时发送，而不是在窗口开始时发送。

**JTAG 接口从不会被移除或禁用。** 它始终作为后备手段存在——如果某次 CAN 更新出了问题，或者你只是更喜欢这种方式，随时都可以通过 SWD 重新刷写任意一个镜像。

**板载的两个按钮 BOOT 和 RESET** 同样用于恢复——RESET 是普通的硬件复位（`NRST`），BOOT 则将 `BOOT0` 拉高，这是一个在本仓库中任何代码运行*之前*就已在芯片层面做出的决定：正常情况下（未按住）芯片从闪存启动，进入如上所述的本项目自身引导程序；复位时按住，则改为进入 ST 自身的出厂系统内存引导程序（USB DFU/UART 恢复，与本仓库中的任何内容完全无关）。完整技术细节见 `src/F303-master/README.md` 第 4a 节。

### 2. 后续更新——通过 CAN 总线

一旦引导程序就位，更新应用程序就完全不再需要物理接触板卡——只需通过已经承载着工具头指令的同一条脐带 CAN 线发送新的固件构建即可。

**更新流程：**

1. **触发。** 主机向*正在运行的应用程序*发送 `0x7F0`（DLC 4，负载 `B0 07 1D 5A`）。它会安全地内联切断每一个执行器的电源——电机、加热器、激光——并复位芯片。这个魔术负载要求意味着一个损坏或畸形的帧不能意外触发进入更新模式的复位。
2. **开始。** 复位后，引导程序开始监听。主机发送 `0x7F1`（DLC 8，大端序固件总大小 + 大端序硬件 ID）。为不同硬件构建的镜像会在此刻立即被拒绝，甚至还没有触碰一个字节的闪存。引导程序会正好擦除新镜像所需数量的备份槽页面，并回复一个状态帧（`0x7F5`）。
3. **签名。** 主机以四个 `0x7F7` 帧（每个 8 字节，按顺序）发送预期的 HMAC-SHA256 签名——该签名基于固件镜像和引导程序与签名构建的工具之间共享的密钥计算得出。 这是共享的对称密钥，因此该校验只能证明镜像来自持有该密钥的人，而不能证明对方是谁；已发布的镜像使用公开的开发密钥（参见 `docs/FIRMWARE_SIGNING.TXT`）。
4. **数据。** 主机以一系列 `0x7F2` 帧（每帧至多 8 字节的原始固件数据）连续发送 `.bin` 文件——CAN 保证帧按照在单一总线上发送的顺序到达，因此不需要逐帧序号。引导程序将传入的字节缓冲进 RAM 中的一个 2KB 页面，写满后写入*备份*槽，在认为该页面完成之前逐个半字读回并与本应写入的内容比对，并在每次经过验证的写入后发送一个 `0x7F3` 确认（附带页面索引）。一个合理的主机实现会在发送下一页数据之前等待每一页的 ACK，以避免溢出引导程序的接收缓冲区。
5. **结束与验证。** 一旦所有字节都已发送完毕，主机发送 `0x7F4`（DLC 8，大端序 CRC32 + 主/次版本号）。引导程序检查备份槽的大小，计算其 CRC32 和 HMAC-SHA256，并将两者都与主机所声明的值进行比对。只有一切都匹配时，它才会逐页将备份复制到主槽中，采用与上文相同的回读验证方式。一旦该复制完成并确认无误，它会保存新的元数据并复位进入更新后的应用程序。任何不匹配——大小、CRC32、HMAC 或硬件 ID——都不会触碰主槽，引导程序只会回到监听状态，等待一次全新的尝试。

**状态帧（`0x7F5`，DLC 1）：** `0x01` 监听中，`0x02` 擦除中，`0x03` 接收中，`0x06` 验证中，`0x07` 正在将备份复制到主槽，`0x04` 验证通过（即将跳转），`0x05` 验证失败，`0xFF` 错误。

**心跳（`0x7F6`，DLC 2，监听或更新期间每约 1 秒一次）：** 状态字节 + 进度百分比（0-100，百分比不适用时为 `0xFF`）。让主机能够区分“节点存活但尚未开始监听”与“节点完全无响应”——对于自动化上电流程以及在不等待超时的情况下发现卡住的引导程序很有用。

**屏幕上的进度。** 引导程序在更新期间直接驱动 OLED——没有人需要猜测是否有事情正在发生。页面写入或复制期间，它显示“UPDATING”加实时进度条和百分比，在复位进入新固件前短暂显示“FLASH OK”，如果某页写入失败、传输停滞超过 10 秒，或验证返回不匹配，则显示“ERROR”。

**⚠️ 在信任它之前先在实验台上测试。** 上述协议编译和链接干净，逻辑也经过了仔细推敲，但引导程序恰恰是那种“编译正确”与“在硬件上值得信赖”相去甚远的固件类型——真实的闪存编程时序、跨越数千帧传输的 CAN 行为，以及引导程序到应用程序的交接，都需要在真实板卡上（理想情况下手边备有 JTAG 作为后备）验证，之后才能在连接了真实执行器的无人值守更新场景中依赖它。

### PC 工具

有两款独立的、跨平台（Windows/Linux）的 GUI 工具支持本板——**URTC Flasher**（CAN-OTA 及全芯片 SWD/JTAG 更新，同时适用于本板和高级扩展变体自身的扩展从属芯片）以及 **URTC Tester**（一个实时 CAN 总线测试器，显示当前跳接的任意工具配置文件）。两者曾经都存在于本仓库 `tools/` 目录下；如今各自都是独立项目，拥有各自的 README、许可证和翻译：

- [URTC Flasher](https://github.com/JuanenRac/URTC-FLASHER)
- [URTC Tester](https://github.com/JuanenRac/URTC-TESTER)

还存在一个基于网页的替代方案，覆盖类似的功能范围（实时监控、CAN 分析、OTA 刷写、热成像检测），无需在本地安装任何东西：[URTC Web Studio](https://github.com/JuanenRac/URTC-WEB-STUDIO)。

## 🛡️ 热保护（仅在主机上测试）、固件签名（开发密钥）与 CAN（无身份验证）

- **热失控保护**（`thermal_guard.c`，自应用 0.3.2 起）用于烙铁／热风和 3D 热端：对*实测*温度的 MAXTEMP（T12 449 °C，热端 310 °C）、加热监视（加热器开启时温度必须真正上升——可检测松脱的热敏电阻或烙铁头）以及保温检查（达到目标后，加热器开启时不得长时间下降）。一旦故障即切断加热器、锁定板卡错误，并通过 `0x111` 的第 4 字节报告原因。已在主机上用模拟加热器测试（`tests/test_thermal_guard.c`）。 这些限值从未在真实加热器上测量过，这一切也从未在真实板卡上运行过。
- **围绕硬件的固件逻辑的主机测试**（`tools/run_host_tests.py`，38 个测试套件）：除引导程序、热保护和分发器外，13 种工具的真实 CAN 处理程序（`firmware_can_*.c`）、加热器控制回路（`firmware_control_thermal.c`：ADC 换算、不可能的读数、设定值上限、迟滞、加热器卡死、保护触发的故障）、故障记录器（`firmware_blackbox.c`，与 URTC-COMMON 由自身模型写出的场景逐字节比对，并检查其 CAN 读出）、由跳线识别工具、错误门之后的命令以及热像仪命令，都针对模拟 HAL（`tests/host_hal/`）编译，并在地址与未定义行为检查器及 `gcc -fanalyzer` 下运行；`--coverage-floor`（CI 中为 `URTC_COVERAGE_FLOOR`）要求执行的行数达到一定比例（全部 78 个固件源文件的行为 95%，其中现在包括应用程序的启动与主循环、OLED 驱动及其画面、状态 LED、中断处理程序、扩展接口的总线、三种热像仪、扩展从机以及两个引导程序的主程序；CI 的下限为 90）。以上均未在开发板上运行过。
- **诊断：** `0x111` 的第 5 字节报告上次复位原因（bit0 = 硬件看门狗触发）；`0x1AA`/`0x1AB` 返回保存在 F-RAM 中的累计计数——热故障次数、最后一次故障代码和加热器总开启时间（自 0.3.3 起）；黑匣子采样也会记录热故障代码。
- **固件签名：** 引导程序的 HMAC 密钥在构建时从私有文件（`URTC_SIGNING_KEY_FILE`）注入；没有该文件时构建使用*公开开发密钥*并给出提示，`URTC_RELEASE=1` 会拒绝该密钥。`firmware/` 中每个镜像都附带 `<bin>.sha256` 和 `<bin>.manifest.json`（含预先计算的签名），因此 URTC Flasher 和 URTC Web Studio 不包含任何密钥。参见 [`docs/FIRMWARE_SIGNING.TXT`](docs/FIRMWARE_SIGNING.TXT)。 HMAC 使用共享的对称密钥：已发布的应用镜像使用公开的开发密钥签名，因此任何人都可以为使用该密钥构建的板卡签出它会接受的镜像；非对称签名（Ed25519）仍在计划中。 正式发布的每个文件还带有分离式 Ed25519 发布签名（`<文件>.sig`，公钥见 [`docs/RELEASE_SIGNING.md`](docs/RELEASE_SIGNING.md)）；刷写器和 Web Studio 在签名存在时会进行验证。
- **CAN 总线：** 不验证发送方——任何节点都可以控制工具。使用激光、加热器或焊接设备的安装**必须**配备独立于 MCU 的硬件联锁和封闭总线：[`docs/CAN_THREAT_MODEL.TXT`](docs/CAN_THREAT_MODEL.TXT)。
- **同一 CAN 总线上的多个工具头（默认关闭）：** 一个持久开关（CAN `0x1AC` 设置，`0x1AD` 读取，F-RAM 记录见 [`docs/EEPROM.TXT`](docs/EEPROM.TXT)）会启用由五个 ID 跳线得到的节点地址、发现机制和选择规则：只有被选中的工具头才执行命令，未被选中的工具头绝不动作。开关关闭时（这是默认值，空白的 F-RAM 读出的也是关闭），电路板的行为与单个工具头完全相同，帧和标识符不变。已在固件中实现，仅在计算机上测试，尚未在电路板上试验。设计和帧见 URTC-COMMON（`docs/MULTINODE.md`）以及 [`docs/CANBUS.TXT`](docs/CANBUS.TXT) 的附录。
- **引导程序的应答范围与从机补丁号：** 回读（`0x7FE`）和降级授权（`0x7FD`）只在由应用程序接受 `0x7F0` 后启动的引导程序会话中应答（或在没有有效应用程序的板上）；正常上电后会被忽略，编译选项可关闭此限制。扩展从机把补丁号放在版本寄存器的第十个字节中，主机用 `0x222` 转发（字节为 0 表示未发送，与旧从机相同）。第三个字节携带次版本号的低字节，程序因此可以显示完整版本。已在源码中实现并在主机上测试；目前还没有固件映像包含它。

## 📋 更新日志

固件和引导程序是独立版本化和发布的——刷写一个新的引导程序并不意味着有新的应用版本，反之亦然，因此各自在自己的文件中拥有自己的历史，而不是用一个隐含二者始终同步移动的合并版本号：

- 固件（`src/F303-master/`）：[`src/F303-master/CHANGELOG.md`](src/F303-master/CHANGELOG.md)
- 引导程序（`src/F303-master/boot/`）：[`src/F303-master/boot/CHANGELOG.md`](src/F303-master/boot/CHANGELOG.md)
- 扩展从属应用（`src/F303-slave/`，STM32F303CBT6）：[`src/F303-slave/CHANGELOG.md`](src/F303-slave/CHANGELOG.md)
- 扩展从属引导程序（`src/F303-slave/boot/`）：[`src/F303-slave/boot/CHANGELOG.md`](src/F303-slave/boot/CHANGELOG.md)

**版本管理策略：** 全部 4 个组件（2 个应用固件、2 个引导程序——`FIRMWARE_VERSION_MAJOR`/`MINOR`/`PATCH` 和 `BOOTLOADER_VERSION_MAJOR`/`MINOR`/`PATCH`）都是**递增式**的——该组件自身的 `PATCH` 只会通过显式的 `python bump.py` 步骤加 1（由它调用仓库根目录下的 `bump_version.py`），编译从不改变版本，一旦 `PATCH` 会超过 9，就进位到 `MINOR`（然后是 `MAJOR`），与汽车自身里程表使用的相同十进制“里程表”规则一致——例如 `0.1.7` → `0.1.8` → `0.1.9` → `0.2.0`，绝不会是 `0.1.10`。每个引导程序还保留一份对应应用程序 `FIRMWARE_VERSION_*` 的自身副本，由同一次递增自动保持同步。全部 4 个组件当前版本的一览见仓库根目录下的 [`CHANGELOG.md`](CHANGELOG.md)，每条轨道完整的运作机制见 [`VERSION_CHECKLIST.txt`](VERSION_CHECKLIST.txt)。

## 🔍 当前状态

**固件（`src/F303-master/`）：** 全部 25 种工具配置文件功能完整——带滞回的热开关式控制、逐工具遥测、通信看门狗、失速/故障检测，以及 OLED 自身的实时诊断，此外还有一对当前工具查询命令（`0x110`/`0x111`）、面向扩展接口的通用 SPI 透传（`0x180`/`0x181`）、一块在断电时保留设定值的板载 F-RAM（`0x190`/`0x191`）、`11111` 跳线自由工具配置机制（`0x1A2`/`0x1A3`）、外设类型 + 设备序列号报告（`0x1A4`/`0x1A5`，用于在共享总线上区分多块原本相同的板卡），以及一条到达高级扩展板上从属芯片的 CAN 转 I2C 桥（`0x210`-`0x221`）。独立于引导程序进行版本管理（见下方更新日志）。 这一切都从未在真实板卡上运行过，热保护仅在主机上用模拟加热器测试过。

**引导程序（`src/F303-master/boot/`）：** 功能完整的黄金镜像 A/B 更新系统——通过 CAN 进行、以 CRC32 和共享密钥 HMAC-SHA256 校验的 OTA 更新（并非非对称签名），一个确保失败的更新绝不会使板卡变砖的备份槽，以及独立于应用程序的自身版本报告（`0x7FA`）。编译和链接干净；在无人值守、连接真实执行器的场景中信任它之前，请先参见上方的实验台测试注意事项。

**PC 工具：** [URTC Flasher](https://github.com/JuanenRac/URTC-FLASHER)（CAN OTA 更新 + 全芯片 SWD/JTAG 编程）和 [URTC Tester](https://github.com/JuanenRac/URTC-TESTER)（实时逐工具控制/遥测测试器）两者对于各自的既定目标而言功能都已完整，如今各自都是独立项目，各自拥有详细覆盖设置和每一项控制的 README。

**硬件：** 原理图和 BOM 仍在最终确定中；目前尚不存在已组装的板卡可用于针对真实芯片验证上述任何内容。上述一切均可编译、链接，并经过仔细推敲，但“构建正确”和“已在硬件上验证”是两种不同的说法——见本 README 顶部的安全提示，请以任何一块新板卡都应得的谨慎态度对待首次上电。

如果社区中有人正在为 PAROL6、Faze4 或任何其他机械臂平台开发定制末端执行器、智能换刀器或高级工具集成，我很乐意交流、交换想法，或更深入地探讨 CAN 指令！

## 📂 仓库结构

```
/
├── 3D/
│   ├── RACK/                    板卡安装架，2 种变体（x1、x3）——各自提供
│   │                            .stl/.3mf/.amf/.scad
│   ├── REVOLVER/                占位——空目录，内容尚未开始
│   └── TOOLS/
│       └── PAROL6/              面向 PAROL6 机械臂的逐工具可 3D 打印零件——
│                                每种工具一个子文件夹（0.通用零件，然后是与上方
│                                工具目录编号对应的 1-12），已填充的均提供
│                                .stl/.3mf/.amf/.scad；其中若干（4、6-12）
│                                仍是空占位符
├── ani/                          27 个 GIF：每种工具配置文件一个 4 帧动画（00-24，
│                                 对应每种工具自身的数字 ID），开机画面
│                                 （splash_boot.gif），以及无效 ID 警告
│                                 （error_warning.gif）——全部直接从本项目
│                                 自身的固件源码（firmware_render.c 自身的
│                                 ToolIcons[]/SplashFace[]/ErrorText[] 表）
│                                 解码而来，而非单独手绘，因此始终与真实
│                                 OLED 实际显示的内容一致
├── BOM/
│   ├── BOM.TXT                  PCB 板的完整物料清单
│   ├── BOM_EXPANSION_BASIC_TMC2209.TXT     扩展板，基础 + TMC2209
│   ├── BOM_EXPANSION_BASIC_TMC5160A.TXT    扩展板，基础 + TMC5160A
│   ├── BOM_EXPANSION_ADVANCED_TMC2209.TXT  扩展板，高级 + TMC2209
│   ├── BOM_EXPANSION_ADVANCED_TMC5160A.TXT 扩展板，高级 + TMC5160A
│   ├── BOM_EXPANSION_BASIC_ADS1115.TXT     扩展板，基础 + ADS1115（仅传感器，无驱动器/MCU）
│   └── BOM_EXPANSION_BASIC_MLX9064X.TXT    扩展板，基础 + MLX9064x（仅传感器，无驱动器/MCU）
├── docs/
│   ├── CANBUS.TXT               CAN 总线协议参考（所有指令/遥测 ID）
│   ├── ECOVIA.TXT               工具识别矩阵与引脚变换逻辑
│   ├── TOOLS.TXT                全部 25 种工具的高层目录——每种做什么、
│   │                            使用哪些外设，不含引脚级细节
│   ├── PINOUT.TXT               完整 MCU 引脚定义，逐模块列出
│   ├── PINOUT_CONNECTORS.TXT    物理接口引脚定义（CONN_DRILL、CONN_SEN 等）
│   ├── EXPANSION.TXT            CONN_EXPANSION 接口及附加板变体
│   ├── PINOUT_SLAVE.txt         扩展从属芯片的完整引脚定义（仅限高级变体）
│   ├── EEPROM.TXT               完整的 F-RAM 寄存器映射（每一项持久化设置、字节偏移量）
│   ├── COMPILE_STM32F303.TXT    全部 4 个固件二进制文件从零开始的构建指南——
│   │                            工具链、ST HAL/CMSIS 设置、确切的编译/链接命令；
│   │                            仓库根目录下的 build_firmware.sh/.bat 端到端
│   │                            自动化了同一个流程
│   ├── FIRMWARE_DISTRIBUTION.md 固件文件的发布位置以及各使用方读取的内容
│   ├── FIRMWARE_SIGNING.TXT     固件镜像如何签名：目前为 HMAC，计划改用 Ed25519
│   ├── RELEASE_SIGNING.md       发布版本如何签名（每个文件一个分离的 Ed25519 签名）以及公钥
│   ├── KEYS.md                  整个系列所依赖的每一把密钥：用途、公开值、嵌入位置以及私钥所在位置
│   ├── CAN_THREAT_MODEL.TXT     CAN 总线信任什么，以及由此产生的安装要求
│   ├── COMPATIBILITY.md         哪些版本的程序可以一起使用（由 URTC-COMMON 生成）
│   ├── datasheet/               PCB/datasheet/ 下尚未涵盖的 2 份元器件数据手册
│   │                            （CFM_40.pdf、EFB0424VHD-CP0.pdf）
│   └── tool_image_generator/    生成 images/TOOL_*.png 的工具包（见下文）——
│                                render_engine.py + tool_data.py + generate_all.py，
│                                以及说明如何添加新工具自身图像或重新生成现有
│                                图像的 PROCEDURE.TXT
├── src/
│   ├── F303-master/
│   │   ├── STM32F303CC_main.c    入口点——全局定义和 main()
│   │   ├── firmware_*.c/.h       另外约 100 个文件，每个子系统一个（OLED、LED、
│   │   │                         逐工具 CAN 处理程序、初始化、持久化等），
│   │   │                         包括 firmware_ads1115.c（直接 ADS1115 驱动，
│   │   │                         Basic+ADS1115 板）——完整的逐文件表见本
│   │   │                         文件夹自身的 README.md
│   │   ├── melexis_mlx90640/     Melexis 自身官方的 MLX90640 库（Apache-2.0，
│   │   │                         纯 C），加上本板自身在其之上构建的直连驱动，
│   │   │                         用于 Basic+MLX9064x 扩展板
│   │   ├── melexis_mlx90641/     同样的思路，MLX90641 库（Apache-2.0，C++——
│   │   │                         该库为何是 C++ 见本文件夹自身 README.md 第 8a 节，
│   │   │                         在这个原本全 C 的项目中）
│   │   ├── melexis_mlx90642/     同样的思路，MLX90642 库（Apache-2.0，纯 C）——
│   │   │                         该传感器自身驱动为何确实比另外两个更简单，
│   │   │                         见第 8a 节
│   │   ├── STM32F303CCTx_APP.ld  应用程序的链接脚本（0x08008000 处的 112K 主槽）
│   │   ├── README.md             技术参考：硬件平台、ID 跳线工具选择系统、
│   │   │                         逐工具外设接线——线级协议见 CANBUS.TXT，
│   │   │                         此处解释的是其原因
│   │   └── boot/
│   │       ├── bootloader_main.c  引导程序的入口点
│   │       ├── bootloader_*.c/.h  另外 9 个文件（共享类型/常量、加密学、
│   │       │                      闪存/元数据、OLED、CAN 协议）
│   │       ├── STM32F303CCTx_BOOTLOADER.ld  引导程序的链接脚本（0x08000000 处的 30K 区域）
│   │       └── README.md          与应用程序相同的技术参考角色，针对引导程序
│   ├── common/                   应用、引导程序和从机共用的头文件：启动会话标记、从机寄存器以及总线上的版本格式
│   └── F303-slave/               仅存在于 2 种高级扩展板变体上的伴侣芯片
│       │                         （STM32F303CBT6）——见上方扩展接口一节。
│       │                         拥有自己的引导程序/应用对，自己基于 I2C（而非
│       │                         CAN）的更新协议，自己独立的版本管理。
│       ├── slave_main.c          入口点
│       ├── slave_*.c/.h          另外 7 个文件（共享类型/常量、I2C 链路协议、
│       │                         本地传感器总线、本地 PWM）
│       ├── STM32F303CBTx_SLAVEAPP.ld  链接脚本（0x08005000 处的 54K 主槽）
│       ├── README.md             技术参考：本芯片为何存在、本地
│       │                         ADS1115/MLX9064x 传感器总线、本地 PWM、
│       │                         到主板的 I2C 链路协议
│       ├── melexis_mlx90640/     Melexis 自身官方的 MLX90640 库（Apache-2.0，
│       │                         纯 C，未修改，自带许可证文件）——作为其自身
│       │                         独立的编译单元保留，刻意从未并入本项目自身
│       │                         的源码中，因为 Apache-2.0 要求该代码自身的
│       │                         版权声明保持完整
│       ├── melexis_mlx90641/     Melexis 自身官方的 MLX90641 库（Apache-2.0，
│       │                         C++——一个与 MLX90640 自身真正独立的库，而
│       │                         非其变体——为何是 C++ 及构建如何处理这一点，
│       │                         见本文件夹自身 README.md 第 3 节）
│       ├── melexis_mlx90642/     Melexis 自身官方的 MLX90642 库（Apache-2.0，
│       │                         纯 C）——比另外 2 个传感器自身的传输接口
│       │                         确实更简单，原因见 README.md 第 3 节
│       └── boot/
│           ├── slaveboot_main.c   引导程序的入口点
│           ├── slaveboot_*.c/.h   另外 7 个文件（加密学、闪存/元数据、协议）
│           ├── STM32F303CBTx_SLAVEBOOT.ld  链接脚本（0x08000000 处的 18K 区域）
│           └── README.md          与应用程序相同的技术参考角色
├── firmware/  （被 git 忽略的构建输出——仅作为发布资源发布）
│   ├── URTC_MAIN_BOOTLOADER_v<version>.bin  引导程序编译产物，刷写至 0x08000000
│   ├── URTC_MAIN_BOOTLOADER_v<version>.elf  引导程序编译产物，刷写至 0x08000000
│   ├── URTC_MAIN_BOOTLOADER_v<version>.hex  引导程序编译产物，刷写至 0x08000000（地址已内置）
│   ├── URTC_MAIN_FIRMWARE_v<version>.bin    应用程序 bin 编译产物，刷写至 0x08008000
│   ├── URTC_MAIN_FIRMWARE_v<version>.elf    应用程序 elf 编译产物，刷写至 0x08008000
│   ├── URTC_MAIN_FIRMWARE_v<version>.hex    应用程序 HEX 编译产物，刷写至 0x08008000（地址已内置）
│   ├── URTC_SLAVE_BOOTLOADER_v<version>.{bin,elf,hex}  扩展从属自身的引导程序，
│   │                             刷写至 STM32F303CBT6 上的 0x08000000（仅限高级扩展板）
│   ├── URTC_SLAVE_FIRMWARE_v<version>.{bin,elf,hex}  扩展从属自身的应用程序，刷写至 0x08005000
│   └── firmware_manifest.json    上述全部 4 个组件的机器可读索引——版本、
│                                 闪存地址，以及每个文件自身的大小/CRC32，
│                                 供外部工具检查此处有什么以及比它当前所拥有
│                                 的更新的内容。由 generate_manifest.py 自动
│                                 重新生成（由 build_firmware.sh/.bat 自身的
│                                 最后一步调用）——从不手动编辑。
├── images/
│   ├── OLED_DIRECT_MOUNT.jpg     LCD1/CONN_OLED2——裸 30 针 FPC 面板，直接安装选项
│   ├── OLED_BREAKOUT_MODULE.jpg  CONN_OLED——外部 I2C 分线模组，替代选项
│   ├── URTC_BANNER.svg             项目通用 Logo，嵌入本 README 顶部
│   ├── URTC_BOARD.png           板卡照片
│   ├── URTC_SCHEMATIC.png       板卡原理图
│   ├── URTC_PCB_TOP.png         板卡顶层（添加后）
│   ├── URTC_PCB_BOTTOM.png      板卡底层（添加后）
│   └── TOOL_*.png               逐工具跳线/接线参考图，每个配置文件一张
│                                （全部 25 张均已提供——各工具自身链接见上方工具目录）
├── PCB/
│   ├── URTC_V1.0.sch            Eagle 原理图（添加后）
│   ├── URTC_V1.0.brd            Eagle 板卡布局（添加后）
│   ├── URTC_V1.0_JLCPCB.ZIP     Gerber、BOM 和 CPL 文件（添加后）
│   ├── URTC_BOM.TXT             Eagle 导出的原始 BOM（真实来源导出——本项目
│   │                            自身经过整理组织的版本见 BOM/BOM.TXT）
│   ├── datasheet/               板上所用全部元件的数据手册
│   └── *_PARLIST/PINLIST/NETLIST.TXT   Eagle 导出的网表（引脚映射的真实来源）
├── keys/ registry.json：整个系列的公钥以及每把公钥的嵌入位置（由 tools/keys.py 检查）
├── tests/ 固件的主机测试，针对模拟 HAL（tests/host_hal/）和共享向量编译，另有 Python 工具的测试
├── tools/
│   ├── build_test.py            不递增版本号的构建/编译检查
│   ├── publish_release_assets.py  列出要附加的发布资源并输出 gh 命令
│   ├── ci_validate.py           CI 使用的 manifest/CHANGELOG/docs 校验
│   ├── keys.py                  将 docs/KEYS.md 与 keys/registry.json 相互比对，并与代码比对
│   ├── release_sign.py          为发布版本的文件签名（分离的 Ed25519 签名）
│   ├── run_host_tests.py        对模拟 HAL 编译并运行主机测试套件，并设有覆盖率下限
│   ├── changelog_section.py / fuzz_virtual_board.py 某个发布版本的 CHANGELOG 段落文本；对虚拟板的模糊测试运行
│   └── _doc_policy.py / _readme_parity.py / _tracked_names.py ci_validate.py 使用的共享检查
├── VERSION_CHECKLIST.txt        用于正确递增本项目自身 4 个独立版本号中
│                                任意一个的机械化检查清单
├── check_version_consistency.sh  自动化的版本/文件一致性检查——在信任
│                                VERSION_CHECKLIST.txt 自身的说法之前运行
├── check_shared_drivers.py       检查两颗芯片上的 Melexis 驱动副本是否一致
├── firmware_signing.py           固件签名密钥，以及每个镜像旁的签名和摘要文件
├── ruff.toml / urtc.project.json 静态检查的设置，以及项目清单（版本、成熟度、证据）
├── build_firmware.sh            安装工具链，获取 ST 自身的 HAL/CMSIS，并
│                                端到端编译全部 4 个固件二进制文件（Linux）
├── build_firmware.bat           同上，适用于 Windows——两个脚本自动化的
│                                完整手动流程见 docs/COMPILE_STM32F303.TXT
├── build-test.sh / build-test.bat  不递增版本号的构建/编译检查
│                                （tools/build_test.py 的轻量包装）
├── bump.py                      显式的版本递增命令（编译从不递增）
├── bump_version.py              单个头文件的里程表式递增，由 bump.py 调用
├── bump_manifest_version.py     将 urtc.project.json 的版本与原生版本同步（--sync）
├── generate_manifest.py         重新生成 firmware/firmware_manifest.json——
│                                作为完整 build_firmware.sh/.bat 运行的最后
│                                一步自动调用，或在清单需要跟上进度而无需
│                                完整重新构建时随时独立运行
├── LICENSE
├── README.md                    本文件
└── README_spa.md / README_ita.md / README_fra.md / README_deu.md / README_zho.md / README_jpn.md / README_por.md  <- 翻译
```

硬件设计文件（Eagle 原理图/板卡/网表）将随着布局稳定下来而添加。

**固件可选功能与检查(仅在主机上测试):** 步进电机加减速、健康帧、振荡器微调和加热器 PID,每一项都位于默认关闭的开关之后(`0x1AE` / `0x1AF`,见 [CANBUS.TXT](docs/CANBUS.TXT));一块带 sanitizer 的虚拟板,以及用协议规范生成的帧驱动的模糊测试器(`tools/fuzz_virtual_board.py`);还有 `tools/keys.py verify`,用来检查嵌入在程序或引导程序中的每个公钥是否就是登记的那一个([KEYS.md](docs/KEYS.md))。这些都没有在电路板上运行过。

## 🔗 相关项目

本项目是同一作者(JuanenRac / Electro Hobby 3D)打造的 HYDRA-UMC 机器人生态系统的一部分。值得了解,因为某个请求实际上可能是关于这些项目之一,而非本仓库本身。

**子项目** —— 每一个都是专为烧录、测试或驱动本固件而构建的桌面或浏览器工具
- **[URTC-FLASHER](https://github.com/JuanenRac/URTC-FLASHER)** —— 面向 URTC 板卡的桌面图形烧录工具,支持 CAN-OTA 以及全芯片 SWD/JTAG;正是烧录本固件本身。
- **[URTC-COMMON](https://github.com/JuanenRac/URTC-COMMON)** — 只写一次的 CAN 协议数据（规范、生成的表、一致性向量）以及各程序共享的代码；本仓库带有其所用部分的副本。
- **[URTC-GATEWAY](https://github.com/JuanenRac/URTC-GATEWAY)** — 位于 CAN 总线前的网关，只放行列表中的标识符，限制其速率，并要求会话密钥。
- **[URTC-TESTER](https://github.com/JuanenRac/URTC-TESTER)** —— 面向 URTC 板卡的桌面实时 CAN 总线诊断工具,每种工具配置对应一个面板;对应本固件自身 25 种工具配置各一个面板。
- **[URTC-WEB-STUDIO](https://github.com/JuanenRac/URTC-WEB-STUDIO)** —— 通过 Web Serial API 实现的浏览器版 URTC-TESTER 替代方案,无需本地安装;与上述两款桌面工具使用相同的 CAN 协议。

**直接相关**
- **[HYDRA-UMC](https://github.com/JuanenRac/HYDRA-UMC)** —— 机器人手臂主板设计——CM5 主机 + 双核 STM32H745,目标是通过 CAN-OTA/SPI-OTA 协调最多 8 条工具臂(固件处于早期阶段,尚无已组装的电路板);其自身真实的 FDCAN2 中继隧道正是本固件工具头所依托的线路。
- **[HYDRA-UMC-SDK](https://github.com/JuanenRac/HYDRA-UMC-SDK)** —— 每个桥接都据此校验自身指令的共享 JSON-Schema 契约与软件作业门限边界(并非经认证的安全功能);同样覆盖服务、界面、CM5 适配器以及本固件。
- **[HYDRA-UMC-CONNECTOR-HUB](https://github.com/JuanenRac/HYDRA-UMC-CONNECTOR-HUB)** — 面向外部机器连接器的声明式适配器清单注册与校验工具；把 SDK 自身的契约理念扩展到外部机器，而不取代工业网关类项目。
- **[URTC-SMART-RACK](https://github.com/JuanenRac/URTC-SMART-RACK)** —— 面向工具头智能存储架的固件,具备真实的工具 ID 解码与 Smart Idle 预热逻辑;与本固件共享同一工具生态与 CAN 总线。
- **[URTC-VISION-TOOL](https://github.com/JuanenRac/URTC-VISION-TOOL)** —— 面向热成像/RGB 检测工具头的固件及真实 Python 视觉伴侣程序;与本固件共享同一工具生态与 CAN 总线。
- **[HYDRA-UMC-DETECTION-HEF](https://github.com/JuanenRac/HYDRA-UMC-DETECTION-HEF)** —— 具备 Hailo 架构/校验和安全加载验证的真实编译模型注册表;为本固件自身的工具提供背后的视觉识别能力。

**生态系统中的其他项目**

*核心硬件与平台*
- **[HYDRA-UMC-OS](https://github.com/JuanenRac/HYDRA-UMC-OS)** —— 面向 CM5 的可复现 Raspberry Pi OS 产品层——只读代理、经过验证的配置/配置文件、WiFi 首次配网。

*核心后端与客户端*
- **[HYDRA-UMC-SERVER](https://github.com/JuanenRac/HYDRA-UMC-SERVER)** —— 每个控制客户端真正通信的真实无头后端(REST/WebSocket)。
- **[HYDRA-UMC-STUDIO](https://github.com/JuanenRac/HYDRA-UMC-STUDIO)** —— 具有实时多机器人 3D 可视化的网页控制面板。
- **[HYDRA-UMC-SUITE](https://github.com/JuanenRac/HYDRA-UMC-SUITE)** —— 面向多台服务器的桌面(PySide6)集群指挥中心,打包为独立可执行文件。
- **[HYDRA-UMC-ANDROID-CONTROL](https://github.com/JuanenRac/HYDRA-UMC-ANDROID-CONTROL)** —— 具有生物识别登录和配对 Wear OS 伴侣应用的原生 Android 控制应用。
- **[HYDRA-UMC-IOS-CONTROL](https://github.com/JuanenRac/HYDRA-UMC-IOS-CONTROL)** —— 具有实时 WebSocket 同步的 iOS/iPadOS 控制应用(Flutter)。
- **[HYDRA-UMC-DSI](https://github.com/JuanenRac/HYDRA-UMC-DSI)** —— 面向机载 7 英寸 DSI 触摸屏的原生触控界面,直接嵌入 CM5 本体。
- **[HYDRA-UMC-EDITOR-URDF](https://github.com/JuanenRac/HYDRA-UMC-EDITOR-URDF)** —— 将完成的模型推送到 STUDIO 自身目录的桌面版图形化 URDF 创建/编辑工具。
- **[HYDRA-UMC-EDITOR-STL](https://github.com/JuanenRac/HYDRA-UMC-EDITOR-STL)** — 桌面 STL 模型编辑器，在 HYDRA-UMC-EDITOR-URDF 编辑的同一模型目录中变换/替换/移除/添加真实部件。
- **[HYDRA-UMC-BRIDGE-AMR](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-AMR)** —— 通过 VDA 5050 MQTT 发布者为 AGV/AMR 车队提供的协调边界(仅针对仿真车队测试)。
- **[HYDRA-UMC-BRIDGE-CNC](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-CNC)** —— 具备 GRBL 状态/控制字节访问能力的高层 CNC 单元协调器(仅针对模拟器测试)。
- **[HYDRA-UMC-BRIDGE-DROIDS](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-DROIDS)** —— 面向足式/人形机器人的协调边界,带有 Boston Dynamics Spot 指令发送器(仅针对模拟器测试)。
- **[HYDRA-UMC-BRIDGE-LASER](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-LASER)** —— 读取 3 路钥匙/外壳/联锁 GPIO 信号的激光单元协调器(仅针对模拟芯片测试;无法使激光器上膛或发射)。
- **[HYDRA-UMC-BRIDGE-OPENPNP](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-OPENPNP)** —— 面向 OpenPnP 贴片机板级流程的安全高层协调器。
- **[HYDRA-UMC-BRIDGE-PRINTER3D](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-PRINTER3D)** —— 面向 Moonraker/Klipper 3D 打印机的安全协调边界,具备真实的受控作业指令。
- **[HYDRA-UMC-BRIDGE-ROS2](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-ROS2)** —— 带有惰性导入 rclpy ROS 2 传输层的协调器(尚未在 ROS 2 环境中运行)。
- **[HYDRA-UMC-BRIDGE-UAV](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-UAV)** —— 面向搭载摄像头的无人机的协调边界,带有 MAVLink 指令发送器(仅针对模拟器测试)。

*视觉 AI 节点(Hailo-8)*
- **[HYDRA-UMC-VISION-NODE](https://github.com/JuanenRac/HYDRA-UMC-VISION-NODE)** —— 面向 Hailo-8 视觉流水线的集成中枢,具备逐阶段的真实硬件就绪检测。
- **[HYDRA-UMC-VISION-STREAMER](https://github.com/JuanenRac/HYDRA-UMC-VISION-STREAMER)** —— 具备真实 HailoRT 集成边界的真实 GStreamer 流水线 + MediaMTX 配置生成器。
- **[HYDRA-UMC-VISUAL-SERVOING-API](https://github.com/JuanenRac/HYDRA-UMC-VISUAL-SERVOING-API)** —— 以区域状态为授权条件的 Position-Based Visual Servoing 修正律;尚无摄像头位姿估计。
- **[HYDRA-UMC-SAFETY-ZONES](https://github.com/JuanenRac/HYDRA-UMC-SAFETY-ZONES)** —— 从文件读取物体位置的 v0 区域入侵、校准新鲜度与 E-STOP 请求逻辑;没有检测器为其供数,也尚未连接任何物理 E-STOP。

*认知 AI 节点(Hailo-10)*
- **[HYDRA-UMC-COGNITIVE-NODE](https://github.com/JuanenRac/HYDRA-UMC-COGNITIVE-NODE)** —— 面向 Hailo-10 认知流水线(LLM/VLA/语音编排)的集成中枢。
- **[HYDRA-UMC-VLA-ENGINE](https://github.com/JuanenRac/HYDRA-UMC-VLA-ENGINE)** —— 面向 Vision-Language-Action 模型的真实动作 token 编解码与轨迹生成。
- **[HYDRA-UMC-VOICE-UI](https://github.com/JuanenRac/HYDRA-UMC-VOICE-UI)** —— 具备受限、需确认的 Watch 中继的真实语音前端(VAD + 意图解析)。
- **[HYDRA-UMC-SEMANTIC-PLANNER](https://github.com/JuanenRac/HYDRA-UMC-SEMANTIC-PLANNER)** —— 基于真实规则的任务分解,以及针对 MCU 错误码的语义化错误恢复。
- **[HYDRA-UMC-DOCS-QA](https://github.com/JuanenRac/HYDRA-UMC-DOCS-QA)** —— 面向本生态系统自身 Markdown 文档的真实纯标准库 TF-IDF 文档检索。
- **[HYDRA-UMC-LOCAL-TECHNICIAN](https://github.com/JuanenRac/HYDRA-UMC-LOCAL-TECHNICIAN)** — 面向生态系统自身的本地、按策略门控的 AI 维护技师——观察、诊断并提出修复建议;两个最高风险等级目前特意尚未实现。

*编排与集群*
- **[HYDRA-UMC-ORCHESTRATOR](https://github.com/JuanenRac/HYDRA-UMC-ORCHESTRATOR)** —— 具备真实 gRPC/Protobuf 健康报告契约与任务状态机的集成中枢。
- **[HYDRA-UMC-JOB-DISPATCHER](https://github.com/JuanenRac/HYDRA-UMC-JOB-DISPATCHER)** —— 基于真实 HTTP API 的真实优先级任务队列,支持去重。
- **[HYDRA-UMC-NODE-HEALING](https://github.com/JuanenRac/HYDRA-UMC-NODE-HEALING)** —— 具备重试/退避与身份不匹配检测的真实基于 gRPC 的车队健康看门狗。
- **[HYDRA-UMC-PATH-PLANNER-3D](https://github.com/JuanenRac/HYDRA-UMC-PATH-PLANNER-3D)** —— 具备真实障碍物/工作空间碰撞校验的真实基于 RRT 的三维路径规划器。
- **[HYDRA-UMC-SWARM-SYNC](https://github.com/JuanenRac/HYDRA-UMC-SWARM-SYNC)** —— 经过多单元收敛属性测试的真实 CRDT LWW-Element-Map 状态同步。

*数字孪生与仿真*
- **[HYDRA-UMC-TWIN](https://github.com/JuanenRac/HYDRA-UMC-TWIN)** —— 面向数字孪生引擎的集成中枢,具备真实的版本兼容性同步契约。
- **[HYDRA-UMC-HIL-BRIDGE](https://github.com/JuanenRac/HYDRA-UMC-HIL-BRIDGE)** —— 在仿真与真实硬件之间带有软件碰撞联锁的硬件在环路由器(未在真实硬件上测试)。
- **[HYDRA-UMC-PHYSICS-REPLICA](https://github.com/JuanenRac/HYDRA-UMC-PHYSICS-REPLICA)** —— 面向真实 URDF 子集的真实正向运动学与关节限位校验。
- **[HYDRA-UMC-SYNTHETIC-DATA-GEN](https://github.com/JuanenRac/HYDRA-UMC-SYNTHETIC-DATA-GEN)** —— 具备 YOLO/COCO 标注导出功能的真实程序化 2D 场景生成器。

*数据与分析*
- **[HYDRA-UMC-DATALAKE](https://github.com/JuanenRac/HYDRA-UMC-DATALAKE)** —— 具备真实数据摄入/查询 HTTP API 的真实 sqlite3 时序数据存储。
- **[HYDRA-UMC-ANOMALY-DETECTOR](https://github.com/JuanenRac/HYDRA-UMC-ANOMALY-DETECTOR)** —— 具备漂移监测能力的真实 FFT + 统计基线异常检测器。
- **[HYDRA-UMC-PRODUCTION-REPORTS](https://github.com/JuanenRac/HYDRA-UMC-PRODUCTION-REPORTS)** —— 基于 DATALAKE 历史数据的真实 OEE/可用率计算,支持可复现的 CSV 导出。
- **[HYDRA-UMC-TELEMETRY-COLLECTOR](https://github.com/JuanenRac/HYDRA-UMC-TELEMETRY-COLLECTOR)** —— 面向 DATALAKE 的真实 CAN/WebSocket 数据摄入管道,支持序列去重。

*工业网关*
- **[HYDRA-UMC-GATEWAY-INDUSTRIAL](https://github.com/JuanenRac/HYDRA-UMC-GATEWAY-INDUSTRIAL)** —— 中继至工业协议的集成中枢,具备真实的指令白名单/背压控制层。
- **[HYDRA-UMC-OPCUA-SERVER](https://github.com/JuanenRac/HYDRA-UMC-OPCUA-SERVER)** —— 经真实二进制协议客户端会话验证的真实 OPC-UA 地址空间。
- **[HYDRA-UMC-MQTT-BROKER](https://github.com/JuanenRac/HYDRA-UMC-MQTT-BROKER)** —— 具备可选按客户端认证与主题 ACL 的真实 MQTT 代理。
- **[HYDRA-UMC-MTCONNECT-ADAPTER](https://github.com/JuanenRac/HYDRA-UMC-MTCONNECT-ADAPTER)** —— 具备降级模式输出的真实 MTConnect `/probe` 与 `/current` XML 端点。

*辅助工具与生态系统运维*
- **[HYDRA-UMC-DASHBOARD-AI](https://github.com/JuanenRac/HYDRA-UMC-DASHBOARD-AI)** —— 基于 DATALAKE/ANOMALY-DETECTOR 的智能摘要与异常高亮面板,具备诚实的统计回退机制。
- **[HYDRA-UMC-TOOL-CLI](https://github.com/JuanenRac/HYDRA-UMC-TOOL-CLI)** —— 具备真实、稳定退出码契约的车队 CLI,是 HYDRA-UMC-SERVER 自身 API 的真实在线客户端。
- **[HYDRA-UMC-WATCH](https://github.com/JuanenRac/HYDRA-UMC-WATCH)** —— 带有触觉提醒模式与配对手机语音中继的 WearOS 伴侣应用(尚未在实体手表上运行)。
- **[URTC-UPDATER](https://github.com/JuanenRac/URTC-UPDATER)** — 桌面工具，用于检测、安装和更新 URTC 系列的仓库，每次只执行一项明确的操作。
- **[HYDRA-UMC-UPDATER](https://github.com/JuanenRac/HYDRA-UMC-UPDATER)** —— 发现、克隆并更新本生态系统中每个仓库的管理类桌面工具。
- **[HYDRA-UMC-OS-REBUILDER](https://github.com/JuanenRac/HYDRA-UMC-OS-REBUILDER)** —— 构建即刻可烧录、预装生态系统最新版本的 CM5 镜像的 Windows/Linux 桌面工具,具备类似 Raspberry Pi Imager 风格的首次启动 Wi-Fi/用户/SSH 配置。
- **[HYDRA-UMC-OPS-AGENT](https://github.com/JuanenRac/HYDRA-UMC-OPS-AGENT)** — 维护事件协调器：一个低权限的边缘角色采集经过脱敏的库存/健康快照，一个控制面角色以只读方式渲染它，并请求某个 AI 提供方给出诊断建议——从不应用补丁，也从不部署任何内容。
- **[HYDRA-UMC-DEV-SERVER](https://github.com/JuanenRac/HYDRA-UMC-DEV-SERVER)** —— 可复现的开发主机（Raspberry Pi 5 / CM5），保存整个生态系统的源代码，并在持久队列下运行有界的构建/测试任务；这是专用的开发角色，明确区别于运行中的 CM5。

---

## 📚 文档与社区

- **[CONTRIBUTING.md](CONTRIBUTING.md)** —— 提交 Pull Request 所需的技术栈和编码规范。
- **[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)** —— 本社区所期望的行为准则。
- **[SECURITY.md](SECURITY.md)** —— 如何报告漏洞，以及本项目真实的安全关注重点。
- **[SUPPORT.md](SUPPORT.md)** —— 在哪里提问和报告缺陷。
- **[LICENSE.md](LICENSE.md)** —— 本项目自身的许可证。

## 👤 作者
**JuanenRac** (Electro Hobby 3D)
📧 electrohobby3d@gmail.com
📺 [youtube.com/@electrohobby3d](https://youtube.com/@electrohobby3d)

<!-- urtc-section:compatibility -->
## 🧩 兼容性

哪些版本的 URTC 程序可以配合使用、各自使用哪个协议版本，列在 URTC-COMMON 生成的兼容性表中：[docs/COMPATIBILITY.md](https://github.com/JuanenRac/URTC/blob/main/docs/COMPATIBILITY.md)。
<!-- /urtc-section:compatibility -->

<!-- urtc-section:status_firmware -->
## 💡 项目状态

🟢 已完成并已验证 · 🟡 可用，但有已知限制 · 🟠 做了一半或未验证 · 🔴 未完成或不能用。从未在真实硬件上运行过的内容不会是绿色。

| 部分 | 状态 | 仓库能够证明的内容 |
| --- | :---: | --- |
| 软件 | 🟡 | 可编译并通过电脑上的测试；在真实工具头上的运行仍待进行。 |
| 测试 | 🟢 | 每次推送时测试套件都会在 CI 中通过。 |
| 真实硬件 | 🟠 | 尚未在真实板子上运行：台架测试仍待进行。 |
| 安全 | 🟡 | 发布版使用 Ed25519 签名，并在使用前检查；密钥的处理没有他人审阅过。 |
| 文档 | 🟡 | 提供八种语言的 README 和参考文档；翻译未经母语者审阅。 |

<!-- /urtc-section:status_firmware -->

## 📜 许可证

URTC 版权所有 (c) 2026 JuanenRac（Electro Hobby 3D）。分发本项目或其衍生作品时必须包含此声明。

由于本项目由几种不同类型的内容组成，各个部分依据不同的许可证提供——各自适合其实际所涵盖的内容，而不是强行用一种许可证套用一切：

1. 位于 `./firmware` 的**固件**（应用程序和 CAN 引导程序均在内）依据 **GNU 通用公共许可证 v3.0（GPL-3.0-or-later）** 提供。完整文本见 https://www.gnu.org/licenses/gpl-3.0.html。

2. **硬件设计**（Eagle 原理图/板卡文件、Gerber 文件，以及 `./PCB` 和 `./3D` 下可 3D 打印的部件）依据 **CERN 开放硬件许可证 v2 - 强互惠版（CERN-OHL-S v2）** 提供。完整文本见 https://cern-ohl.web.cern.ch/。

3. **文档**（本 README 及其自身的翻译版本——`README_spa.md`、`README_ita.md`、`README_fra.md`、`README_deu.md`、`README_zho.md`、`README_jpn.md`——加上 `./docs` 下的参考文件）依据 **知识共享 署名-相同方式共享 4.0 国际许可协议（CC BY-SA 4.0）** 提供。完整文本见 https://creativecommons.org/licenses/by-sa/4.0/。

如果你基于本项目进行开发，请留意这种许可证划分：对固件的代码更改应保持 GPL-3.0-or-later，硬件改动应保持 CERN-OHL-S，文档衍生品应保持 CC BY-SA——每一项都需附带指向本项目的署名。

本仓库仅涵盖 URTC 板卡自身的固件和硬件——曾经存在于此的 PC 工具（URTC Flasher、URTC Tester）如今是拥有各自许可证的独立项目，见上方“PC 工具”一节。
