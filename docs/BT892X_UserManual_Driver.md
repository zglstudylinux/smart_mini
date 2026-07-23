# BT892X Audio Player Microcontroller 驱动开发手册

> **版本**: 0.0.1  
> **日期**: 2021.03.30  
> **厂商**: 中科蓝讯 (Bluetrum)  
> **适用场景**: 音频播放器、蓝牙音频、低功耗触控等嵌入式应用

---

## 1. 芯片概述

### 1.1 简介
BT892X 是一款 **32 位 RISC 微控制器**，专为音频播放器应用设计，集成了先进的数字和模拟外设。

### 1.2 主要特性

| 特性 | 说明 |
|:---|:---|
| **CPU** | 32 位 RISC，最高 120MHz 工作频率 |
| **Cache** | 16KB I-Cache / D-Cache |
| **GPIO** | 可编程上下拉电阻，支持多组 Port（PA/PB/PE/PF/PG） |
| **音频解码** | 支持 AAC、mSBC 高品质解码 |
| **触摸按键** | 支持低功耗触摸按键（Touch Key） |
| **入耳检测** | 支持低功耗入耳检测 |
| **定时器** | 3 个基础 32 位定时器（Timer0/1/2）+ 3 个多功能 32 位定时器（Timer3/4/5，支持捕获和 PWM） |
| **通信接口** | 3 路全双工 UART、2 路 SPI、1 路 IIC（Master） |
| **存储** | SD Card Host 控制器 |
| **USB** | 全速 USB 2.0 HOST/DEVICE 控制器 |
| **ADC** | 16 通道 10 位 SARADC；1 路高性能音频 ADC（90dB SNR） |
| **DAC** | 高性能立体声 DAC（98dB SNR），支持单端/差分模式 |
| **电源管理** | 内置 PMU（Charger/Buck/LDO） |
| **其他** | WatchDog、IR 控制器、集成 RTC、蓝牙（Bluetooth） |

---

## 2. 中断系统

### 2.1 异常向量表

| 中断号 | 地址 | 描述 |
|:---|:---|:---|
| 0 | 0x00 | Reset |
| 4 | 0x10 | 低优先级中断 |
| 8 | 0x20 ~ 0x9C | 高优先级中断（见下表） |

### 2.2 高优先级中断向量

| 中断号 | 地址 | 描述 |
|:---|:---|:---|
| 2 | 0x28 | 软件中断 |
| 3 | 0x2C | Timer0 中断 |
| 4 | 0x30 | Timer1 中断 |
| 5 | 0x34 | Timer2 中断 |
| 6 | 0x38 | IR 接收中断 |
| 14 | 0x58 | UART0 / UART1 / UART2 中断 |
| 16 | 0x60 | Timer3 中断 |
| 17 | 0x64 | Timer4 中断 |
| 18 | 0x68 | Timer5 中断 |
| 26 | 0x88 | 端口中断（Port interrupt） |

### 2.3 中断控制寄存器

#### PICCON - 外设中断控制寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:17 | - | - | - | 未使用 |
| 16 | GIEM | WR | 1 | 全局中断使能掩码位。0: 禁用中断；1: 使能中断 |
| 15:7 | - | - | - | 未使用 |
| 6:5 | HPSDEN | WR | 0x0 | 高优先级影子寄存器选择。00: 高优先级；01: 高优先级 2；10/11: 高优先级 3 |
| 4:2 | - | - | - | 未使用 |
| 1 | LPINTEN | WR | 0 | 低优先级中断使能。0: 禁用；1: 使能 |
| 0 | GIE | WR | 0 | 全局中断使能位。0: 禁用；1: 使能 |

#### PICCONSET - 外设中断控制置位寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 16 | GIEM | W | 0 | 写 1 使能全局中断掩码 |
| 2 | HPINTEN | W | 0 | 写 1 使能高优先级中断 |
| 1 | LPINTEN | W | 0 | 写 1 使能低优先级中断 |
| 0 | GIE | W | 0 | 写 1 使能全局中断 |

#### PICCONCLR - 外设中断控制清除寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 16 | GIEMDIS | W | 0 | 写 1 禁用全局中断掩码 |
| 2 | HPINTDIS | W | 0 | 写 1 禁用高优先级中断 |
| 1 | LPINTDIS | W | 0 | 写 1 禁用低优先级中断 |
| 0 | GIEDIS | W | 0 | 写 1 禁用全局中断 |

#### PICEN - 外设中断使能寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:0 | IntEN | WR | 0x0 | 中断 31~0 使能位。0: 禁用；1: 使能 |

#### PICENSET / PICENCLR - 中断使能置位/清除

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:0 | IntEN / IntDIS | W | 0x0 | 写 1 使能/禁用对应中断 |

#### PICPR - 外设高优先级中断选择寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:0 | IntPR | WR | 0x0 | 中断 31~0 优先级选择。0: 低优先级；1: 高优先级 |

#### PICPR1 - 外设高优先级中断选择寄存器 1

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:0 | IntPR1 | WR | 0x0 | 与 PICPR 组合使用：00=低优先级；01=高优先级；10=高优先级 2；11=高优先级 3 |

#### PICADR - 外设中断入口地址寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:8 | BADR | WR | 0x800 | 中断入口地址 |
| 7:0 | - | - | 0x0 | 未使用 |

#### PICPND - 外设中断挂起寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:3 | IntPND[31:3] | R | 0x0 | 中断 31~3 挂起位。0: 无挂起；1: 挂起 |
| 2 | SWIPND | WR | 0 | 软件中断挂起。写 1 清除 |
| 1:0 | IntPND[1:0] | R | 0x0 | 中断 1~0 挂起位 |

---

## 3. GPIO 管理

### 3.1 特性
- 通过方向寄存器控制 GPIO 输入/输出方向
- 内部上拉/下拉电阻可配置
- 可选择输出驱动电流能力（8mA / 32mA）

### 3.2 GPIO 通用控制寄存器（以 Port A 为例）

| 寄存器 | 名称 | 关键说明 |
|:---|:---|:---|
| **GPIOA** | Port A 数据寄存器 | 7:0 位有效。读为输入状态，写为输出状态 |
| **GPIOASET** | Port A 置位寄存器 | 写 1 置位对应位输出高电平 |
| **GPIOACLR** | Port A 清除寄存器 | 写 1 清除对应位输出低电平 |
| **GPIOADIR** | Port A 方向寄存器 | 0: 输出；1: 输入（默认 0xFF，即全输入） |
| **GPIOAPU** | Port A 上拉寄存器 | 10KΩ 上拉控制。输入模式下有效 |
| **GPIOAPD** | Port A 下拉寄存器 | 10KΩ 下拉控制 |
| **GPIOAPU200K** | Port A 200K 上拉 | 200KΩ 上拉控制 |
| **GPIOAPD200K** | Port A 200K 下拉 | 200KΩ 下拉控制 |
| **GPIOAPU300** | Port A 300Ω 上拉 | 300Ω 上拉控制 |
| **GPIOAPD300** | Port A 300Ω 下拉 | 300Ω 下拉控制 |
| **GPIOADE** | Port A 数字功能使能 | 0: 模拟 IO；1: 数字 IO（默认 0xFF） |
| **GPIOAFEN** | Port A 功能映射使能 | 0: 用作 GPIO；1: 用作功能 IO（默认 0xFF） |
| **GPIOADRV** | Port A 输出驱动选择 | 0: 8mA；1: 32mA（默认 0x0） |

### 3.3 GPIO 功能映射

#### FUNCMCON0 - 端口功能映射控制寄存器 0

| Bit | Name | Default | Description |
|:---|:---|:---|:---|
| 31:28 | UT1RXMAP | 0x0 | UART1 RX 映射。0001=G1, 0010=G2, 0011=由 UT1TXMAP 选择 TX 引脚, 1111=清除 |
| 27:24 | UT1TXMAP | 0x0 | UART1 TX 映射。0001=G1, 0010=G2, 1111=清除 |
| 15:12 | UT0RXMAP | 0x0 | UART0 RX 映射。0001~0110=G1~G6, 0111=由 UT0TXMAP 选择, 1111=清除 |
| 11:8 | UT0TXMAP | 0x0 | UART0 TX 映射。0001~0111=G1~G7, 1111=清除 |
| 7:4 | SPI0MAP | 0x0 | SPI0 映射。0001~0011=G1~G3, 1111=清除 |
| 3:0 | SD0MAP | 0x0 | SD0 映射。0001~0110=G1~G6, 1111=清除 |

#### FUNCMCON1 - 端口功能映射控制寄存器 1

| Bit | Name | Default | Description |
|:---|:---|:---|:---|
| 11:8 | UT2RXMAP | 0x0 | UART2 RX 映射。0001=G1, 0010=G2, 0011=由 UT2TXMAP 选择 TX 引脚, 1111=清除 |
| 7:4 | UT2TXMAP | 0x0 | UART2 TX 映射。0001=G1, 0010=G2, 1111=清除 |

#### FUNCMCON2 - 端口功能映射控制寄存器 2

| Bit | Name | Default | Description |
|:---|:---|:---|:---|
| 19:16 | TMR5MAP | 0x0 | Timer5 PWM 映射。0001=G1, 1111=清除 |
| 15:12 | TMR4MAP | 0x0 | Timer4 PWM 映射。0001=G1, 1111=清除 |
| 11:8 | TMR3MAP | 0x0 | Timer3 PWM 映射。0001=G1, 1111=清除 |
| 7:4 | TMR3CPTMAP | 0x0 | Timer3 捕获引脚映射。0001~0111=G1~G7, 1111=清除 |

#### FUNCMCON3 - 端口功能映射控制寄存器 3

| Bit | Name | Default | Description |
|:---|:---|:---|:---|
| 7:4 | MPDMMAP | 0x0 | MPDM 接口映射。0001~0100=G1~G4, 1111=清除 |
| 3:0 | PDMMAP | 0x0 | PDM 接口映射。0001~0100=G1~G4, 1111=清除 |

### 3.4 外部端口中断唤醒

支持 8 路唤醒源输入，其中唤醒电路 6/7 专用于 32 路端口中断唤醒。

**端口中断源**: `Port_intsrc = {PG[4:0], PF[5:0], PE[7:0], PB[4:0], PA[7:0]}`

| 唤醒源 | 唤醒电路 |
|:---|:---|
| PA7 | Wakeup circuit 0 |
| PB1 | Wakeup circuit 1 |
| PB2 | Wakeup circuit 2 |
| PB3 | Wakeup circuit 3 |
| PB4 | Wakeup circuit 4 |
| WKO (PB5) | Wakeup circuit 5 |
| PORT_INT_FALL | Wakeup circuit 6 |
| PORT_INT_RISE | Wakeup circuit 7 |

#### WKUPCON - 唤醒控制寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 16 | WKIE | WR | 0 | 唤醒中断使能。0: 禁用；1: 使能 |
| 7:0 | WKEN | WR | 0x0 | 唤醒输入 7~0 使能。0: 禁用；1: 使能 |

#### WKUPEDG - 唤醒边沿选择寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 23:16 | WKPND | R | 0x0 | 唤醒输入 7~0 挂起状态 |
| 7:0 | WKEDG | WR | 0x0 | 唤醒边沿选择。0: 上升沿；1: 下降沿 |

#### WKUPCPND - 唤醒清除挂起寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 23:16 | WKCPND | W | 0x0 | 写 1 清除对应唤醒挂起 |

#### PORTINTEN - 端口中断使能寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:0 | PORTINTEN | WR | 0x0 | 端口中断 0~31 使能。0: 禁用；1: 使能 |

#### PORTINTEDG - 端口中断边沿选择寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:0 | PORTINTEDG | WR | 0x0 | 0: 上升沿；1: 下降沿 |

---

## 4. 定时器

### 4.1 特性
- **Timer0/1/2**: 仅支持 32 位定时器功能
- **Timer3/4/5**: 可配置为定时器模式、计数器模式、捕获模式和 PWM 模式

### 4.2 Timer0/1/2 特殊功能寄存器

#### TMR0CON / TMR1CON / TMR2CON - 控制寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 9 | TPND | WR | 0 | 定时器溢出挂起。0: 未溢出；1: 溢出 |
| 7 | TIE | WR | 0 | 溢出中断使能。0: 禁用；1: 使能 |
| 6 | INCSRC | WR | 0 | 递增源选择。0: TMR_INC；1: 外部引脚 |
| 3:2 | INCSEL | WR | 0x0 | 递增时钟选择。00: 系统时钟；01: 计数器输入上升沿；10: 下降沿；11: 双边沿 |
| 0 | TMREN | WR | 0 | 定时器使能。0: 禁用；1: 使能 |

#### TMR0CPND / TMR1CPND / TMR2CPND - 清除挂起寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 9 | TPCLR | W | 0 | 写 1 清除溢出挂起 |

#### TMR0CNT / TMR1CNT / TMR2CNT - 计数寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:0 | TMRCNT | WR | 0x0 | 定时器计数器。使能后递增，等于 TMRPR 时溢出清零并置中断标志 |

#### TMR0PR / TMR1PR / TMR2PR - 周期寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:0 | TMRPR | WR | 0xFFFF | 定时器周期 = TMRPR + 1 |

### 4.3 Timer3/4/5 特殊功能寄存器

#### TMR3CON / TMR4CON / TMR5CON - 控制寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 17 | CPND | WR | 0 | 捕获挂起。0: 未捕获；1: 捕获完成 |
| 16 | TPND | WR | 0 | 溢出挂起。0: 未溢出；1: 溢出 |
| 11 | PWM2EN | WR | 0 | PWM2 使能 |
| 10 | PWM1EN | WR | 0 | PWM1 使能 |
| 9 | PWM0EN | WR | 0 | PWM0 使能 |
| 8 | CIE | WR | 0 | 捕获中断使能 |
| 7 | TIE | WR | 0 | 溢出中断使能 |
| 6 | INCSRC | WR | 0 | 递增源选择。0: TMR_INC；1: 外部引脚 |
| 5:4 | CPTEDSEL | WR | 0x0 | 捕获边沿选择。00: 不捕获；01: 上升沿；10: 下降沿；11: 双边沿 |
| 3:2 | INCSEL | WR | 0x0 | 递增时钟选择（同 Timer0） |
| 1 | CPTEN | WR | 0 | 捕获使能 |
| 0 | TMREN | WR | 0 | 定时器使能 |

#### TMR3CPND / TMR4CPND / TMR5CPND - 清除挂起寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 17 | CPCLR | W | 0 | 写 1 清除捕获挂起 |
| 16 | TPCLR | W | 0 | 写 1 清除溢出挂起 |

#### TMR3CNT / TMR4CNT / TMR5CNT - 计数寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:0 | TMRCNT | WR | 0x0 | 定时器计数器（同 Timer0） |

#### TMR3PR / TMR4PR / TMR5PR - 周期寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:0 | TMRPR | WR | 0xFFFF | 定时器周期 = TMRPR + 1 |

#### TMR3CPT / TMR4CPT / TMR5CPT - 捕获值寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:0 | TMRCPT | R | X | 定时器捕获值 |

#### TMR3DUTY0 / TMR4DUTY0 / TMR5DUTY0 - PWM0 占空比寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 15:0 | TMRDUTY0 | W | X | PWM0 低电平长度 = TMRDUTY0 + 1；高电平长度 = TMRPR - TMRDUTY0 |

#### TMR3DUTY1 / TMR4DUTY1 / TMR5DUTY1 - PWM1 占空比寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 15:0 | TMRDUTY1 | W | X | PWM1 低电平长度 = TMRDUTY1 + 1；高电平长度 = TMRPR - TMRDUTY1 |

#### TMR3DUTY2 / TMR4DUTY2 / TMR5DUTY2 - PWM2 占空比寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 15:0 | TMRDUTY2 | W | X | PWM2 低电平长度 = TMRDUTY2 + 1；高电平长度 = TMRPR - TMRDUTY2 |

---

## 5. RTC

### 5.1 特性
- 支持 32 位独立供电实时计数器
- 支持闹钟中断和秒中断

### 5.2 特殊功能寄存器

#### RTCCON - RTC 控制寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 22 | INBOX | R | 0 | 入盒状态。0: 出盒；1: 入盒 |
| 21 | VUSBOFF | R | 0 | VUSB 断开状态。0: 在线；1: 断开 |
| 20 | VUSBONLINE | R | 0 | VUSB 在线状态。0: 不在线；1: 在线 |
| 19 | RTCWKP | R | 0 | RTC WK 引脚状态 |
| 18 | RTCWKSLPPND | R | 0 | RTC 唤醒睡眠挂起 |
| 17 | ALMPND | R | 0 | RTC 闹钟挂起 |
| 8 | ALM_WKEN | WR | 0 | 闹钟唤醒使能 |
| 7 | RTC_WKSLPEN | WR | 0 | RTC 唤醒睡眠使能 |
| 6 | VUSBRSTEN | WR | 0 | VUSB 插入复位系统使能 |
| 5 | WKUPRSTEN | WR | 0 | RTC 唤醒掉电模式复位系统使能 |
| 4 | ALMIE | WR | 0 | RTC 闹钟中断使能 |
| 3 | RTC1SIE | WR | 0 | RTC 1 秒中断使能 |
| 2:1 | BAUDSEL | WR | 0x1 | 递增时钟选择。00: 系统时钟/4；01: /8；10: /16；11: /32 |

#### RTCCPND - RTC 清除挂起寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 18 | CWKSLPPND | W | 0 | 写 1 清除 RTC 唤醒睡眠挂起 |
| 17 | CALMPND | W | 0 | 写 1 清除 RTC 闹钟挂起 |

### 5.3 独立电源 RTC 寄存器

| 寄存器 | 名称 | 说明 |
|:---|:---|:---|
| **RTCCNT** | RTC 计数寄存器 | 32 位 RTC 计数器 |
| **RTCALM** | RTC 闹钟寄存器 | 32 位闹钟值（默认 0xFF） |
| **RTCCON0** | RTC 控制寄存器 0 | 时钟选择、32K 晶振配置等 |
| **RTCCON1** | RTC 控制寄存器 1 | VRTC 使能、WK 引脚配置 |
| **RTCCON2** | RTC 控制寄存器 2 | VDD 上拉、32K 选择 |
| **RTCCON3** | RTC 控制寄存器 3 | 各唤醒源使能、电源控制（BUCK/LDO/VCORE 等） |
| **RTCCON5** | RTC 控制寄存器 5 | BUCK 低功耗模式、BUCK/LDO 模式选择 |
| **RTCCON10** | RTC 控制寄存器 10 | WK 引脚 10 秒复位、各类唤醒挂起状态 |
| **RTCCON11** | RTC 控制寄存器 11 | RTC 定时器唤醒睡眠、VUSB 滤波、WK 引脚滤波 |
| **RTCCON12** | RTC 控制寄存器 12 | WK 引脚 10 秒复位使能 |

> **驱动开发提示**: RTCCON3 是电源管理核心寄存器，包含 PDCOREEN、VCORESHTEN、VDDXOEN、VCOREAONEN、VCOREEN、VIOEN、BUCKEN 等关键电源控制位。

---

## 6. UART0

### 6.1 特性
- 支持异步串口通信
- 全双工工作模式

### 6.2 特殊功能寄存器

#### UART0CON - UART 控制寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 9 | RXPND | R | 0 | 接收挂起。0: 未接收完 1 字节；1: 接收完成 1 字节 |
| 8 | TXPND | R | 0 | 发送挂起。0: 未发送完 1 字节；1: 发送完成 1 字节 |
| 7 | RXEN | WR | 0 | 接收使能。0: 禁用；1: 使能 |
| 6 | ONELINE | WR | 0 | 单线模式。0: TX/RX 分离；1: TX/RX 共用一线 |
| 5 | CLKSRC | WR | 0 | 时钟源选择。0: 系统时钟；1: uart_inc |
| 4 | SB2EN | WR | 0 | 两停止位使能。0: 1 位停止位；1: 2 位停止位 |
| 3 | TXIE | WR | 0 | 发送中断使能 |
| 2 | RXIE | WR | 0 | 接收中断使能 |
| 1 | BIT9EN | WR | 0 | 9 位模式使能。0: 8 位模式；1: 9 位模式 |
| 0 | UTEN | WR | 0 | UART 模块使能 |

#### UART0CPND - 清除挂起寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 17 | CRSTKEYPND | W | 0 | 清除复位键匹配挂起 |
| 16 | CKEYPND | W | 0 | 清除键匹配挂起 |
| 9 | CRXPND | W | 0 | 清除接收挂起 |
| 8 | CTXPND | W | 0 | 清除发送挂起（写 UART0DATA 也会清除 TXPND） |

#### UART0BAUD - 波特率寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:16 | UART0RXBAUD | W | 0 | 接收波特率 = Fsys / (UART0RXBAUD + 1) |
| 15:0 | UART0TXBAUD | W | 0 | 发送波特率 = Fsys / (UART0TXBAUD + 1) |

#### UART0DATA - 数据寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 8 | UART0BIT8 | WR | X | 第 9 位数据（9 位模式时有效） |
| 7:0 | UART0DAT | WR | X | UART 数据。写：加载到发送缓冲区；读：从接收缓冲区读取 |

### 6.3 使用指南
1. 设置 IO 为正确方向
2. 配置 UART0BAUD 选择采样率
3. 置位 UTEN 使能 UART
4. 根据需要置位 TXIE 或 RXIE
5. 向 UART0DATA 写入数据
6. 等待 PND 变为 1，或等待中断
7. 从 UART0DATA 读取接收数据

---

## 7. SPI0

### 7.1 特性
支持多种模式：
1. 通用 3 线模式（1 位时钟、1 位数据输出、1 位数据输入）
2. 2 线模式（1 位时钟、1 位数据输出或输入）
3. 2 位数据总线模式
4. 4 位数据总线模式

### 7.2 特殊功能寄存器

#### SPI0CON - SPI0 控制寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 16 | SPIPND | R | 0 | SPI 挂起。0: 未完成；1: 收发完成 |
| 13 | HOLDENSW | WR | 0 | SPI 软件 Hold 使能 |
| 12 | HOLDENTX | WR | 0 | 蓝牙发送时 SPI Hold 使能 |
| 11 | HOLDENRX | WR | 0 | 蓝牙接收时 SPI Hold 使能 |
| 10 | SPIOSS | WR | 0 | 采样边沿。0: 与输出数据不同边沿；1: 与输出数据同一边沿 |
| 9 | SPIMBEN | WR | 0 | 多位总线使能 |
| 8 | SPILF_EN | WR | 0 | SPI LFSR 使能 |
| 7 | SPIIE | WR | 0 | SPI 中断使能 |
| 6 | SMPS | WR | 0 | 输出边沿选择（SPIOSS=0 时）。0: 下降沿输出；1: 上升沿输出 |
| 5 | CLKIDS | WR | 0 | 空闲时钟状态。0: 低电平；1: 高电平 |
| 4 | RXSEL | WR | 0 | DMA 或 2 线模式下的收发选择。0: 发送；1: 接收 |
| 3:2 | BUSMODE | WR | 0x0 | 数据总线宽度。00: 3 线；01: 2 线；10: 2 位双向；11: 4 位双向 |
| 1 | SPISM | WR | 0 | 从机模式。0: 主机；1: 从机 |
| 0 | SPIEN | WR | 0 | SPI 使能 |

#### SPI0BAUD - 波特率寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 15:0 | SPI0BAUD | W | 0 | 波特率 = Fsys / (SPI_BAUD + 1) |

#### SPI0CPND - 清除挂起寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 16 | SPICPND | W | 0 | 写 1 清除 SPI 挂起 |

#### SPI0BUF - 收发数据寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 7:0 | SPI0BUF | WR | X | 写：加载到发送缓冲区；读：从接收缓冲区读取 |

#### SPI0DMACNT - DMA 计数寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 10:0 | SPI0DMACNT | W | X | DMA 收发字节数。写此寄存器启动 DMA 传输 |

#### SPI0DMAADR - DMA 地址寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 20:0 | SPI0DMAADR | W | X | SPI DMA 字节地址 |

### 7.3 使用指南

**正常 1 位模式操作流程**:
1. 设置 3 线或 2 线模式，选择引脚映射
2. 选择 RXSEL（发送或接收）
3. 配置时钟频率
4. 选择四种时序模式之一
5. 置位 SPIEN 使能模块
6. 根据需要置位 SPIIE
7. 向 SPIBUF 写数据启动传输
8. 等待 SPIPND 变为 1 或等待中断
9. 从 SPIBUF 读取接收数据

**多位模式操作流程**:
- 2 位模式：写 SPIBUF 两次启动传输
- 4 位模式：写 SPIBUF 四次启动传输
- 接收时只需写一次启动

**DMA 模式操作流程**:
1. 设置 IO 方向和数据宽度
2. 选择 RXSEL 确定 DMA 方向
3. 配置时钟频率和时序模式
4. 使能 SPI
5. 配置 SPI0DMAADR
6. 写 SPI0DMACNT 启动 DMA
7. 等待 SPIPND 或中断

---

## 8. IIC

### 8.1 特性
- 支持 IIC 单主机模式
- 支持异步时钟源（RC2M 或 XOSC26M）
- 最大支持 4 字节输出数据、4 字节输入数据
- 支持 IIC 完成中断

### 8.2 特殊功能寄存器

#### IICON0 - IIC 控制寄存器 0

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31 | DONE | R | 0 | IIC 完成标志 |
| 30 | ACKSTATUS | R | 0 | 从机 ACK 状态。0: ACK；1: NAK |
| 29 | CLR_DONE | W | 0 | 写 1 清除 DONE 标志 |
| 28 | KS | W | 0 | 写 1 启动传输 |
| 27 | CLR_ALL | W | 0 | 写 1 清除所有状态 |
| 9:4 | POSDIV | WR | 0 | SCL 高电平分频。值 N 表示分频 (N+1) |
| 3:2 | HOLDCNT | WR | 0 | SCL 下降沿后 SDA 保持周期。0: 1 周期；1: 2 周期 |
| 1 | INTEN | WR | 0 | IIC 中断使能 |
| 0 | IIC_EN | WR | 0 | IIC 使能 |

> **波特率配置**: `IICCLK = source_clk / (preclkdiv + 1)`，`SCL = IICCLK / (posdiv + 1)`

#### IICON1 - IIC 控制寄存器 1

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 12 | TXNAK_EN | WR | 0 | 读最后一个数据时发送 NAK 使能 |
| 11 | STOP_EN | WR | 0 | 发送 STOP 使能 |
| 10 | WDAT_EN | WR | 0 | 发送数据使能 |
| 9 | RDAT_EN | WR | 0 | 接收数据使能 |
| 8 | CTL1_EN | WR | 0 | 发送控制字节 1 使能 |
| 7 | START1_EN | WR | 0 | 发送起始位 1 使能 |
| 6 | ADR1_EN | WR | 0 | 发送地址 1 使能 |
| 5 | ADR0_EN | WR | 0 | 发送地址 0 使能 |
| 4 | CTL0_EN | WR | 0 | 发送控制字节 0 使能 |
| 3 | START0_EN | WR | 0 | 发送起始位 0 使能 |
| 2:0 | DATA_CNT | WR | 0 | 收发数据字节数。0~N 字节 |

#### IICCMDA - IIC 命令/地址寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:24 | CTL1 | WR | 0 | 控制字节 1 |
| 23:16 | ADR1 | WR | 0 | 地址 1 |
| 15:8 | ADR0 | WR | 0 | 地址 0 |
| 7:0 | CTL0 | WR | 0 | 控制字节 0 |

#### IICDATA - IIC 数据寄存器

| Bit | Name | Mode | Default | Description |
|:---|:---|:---|:---|:---|
| 31:24 | DATA3 | WR | 0 | 数据 3 |
| 23:16 | DATA2 | WR | 0 | 数据 2 |
| 15:8 | DATA1 | WR | 0 | 数据 1 |
| 7:0 | DATA0 | WR | 0 | 数据 0 |

### 8.3 使用指南
1. 配置 IO 映射，SDA 设置上拉使能
2. 选择时钟源（RC2M 或 XOSC26M），设置预分频
3. 配置 IICON0
4. 配置 IICCMDA（控制字节和地址字节）
5. 配置 IICDATA（写入数据）
6. 配置 IICON1
7. 写 KS 启动
8. 等待 DONE 标志或中断
9. 清除 DONE 标志，更新 IICCMDA 或 IICDATA
10. 循环步骤 7

---

## 9. 电气特性

### 9.1 PMU 参数

**表 9-1: PMU 电压输入参数**

| 符号 | 特性 | 最小 | 典型 | 最大 | 单位 | 条件 |
|:---|:---|:---|:---|:---|:---|:---|
| VUSB | 充电电压输入 | 3.0 | 5.0 | 5.5 | V | - |
| VBAT | 电压输入 | 3.0 | 3.7 | 5.0 | V | - |

**表 9-2: 3.3V LDO 参数**

| 符号 | 特性 | 最小 | 典型 | 最大 | 单位 | 条件 |
|:---|:---|:---|:---|:---|:---|:---|
| VDDIO | 3.3V LDO 输出 | - | 3.3 | - | V | 轻载 |
| △VVDDIO | 输出失配 1-sigma | - | 56 | - | mV | VDDIO=3.3V |
| ILOAD | 最大输出电流 | - | - | 150 | mA | @VBAT=3.6V |
| ISC | 短路电流限制 | - | - | 300 | mA | @VBAT=3.8V |

**表 9-3: 1.2V LDO 参数**

| 符号 | 特性 | 典型 | 最大 | 单位 | 条件 |
|:---|:---|:---|:---|:---|:---|
| VDDBT | 1.2V LDO 输出 | 1.2 | - | V | 轻载 |
| ILOAD | 最大输出电流 | - | 100 | mA | @VBAT=3.0V |
| ISC | 短路电流限制 | - | 200 | mA | @VBAT=3.8V |

**表 9-4: 1.1V LDO 参数**

| 符号 | 特性 | 典型 | 最大 | 单位 | 条件 |
|:---|:---|:---|:---|:---|:---|
| VDDCORE | 1.1V LDO 输出 | 1.1 | - | V | 轻载 |
| ILOAD | 最大输出电流 | - | 80 | mA | @VBAT=3.6V |
| ISC | 短路电流限制 | - | 120 | mA | @VBAT=3.8V |

### 9.2 IO 参数

**表 9-5: GPIO 电气特性**

| 符号 | 描述 | 最小 | 典型 | 最大 | 单位 | 条件 |
|:---|:---|:---|:---|:---|:---|:---|
| VIL | 低电平输入电压 | -0.3 | - | 1.27 | V | VDDIO=3.3V |
| VIH | 高电平输入电压 | 2.03 | - | 3.6 | V | VDDIO=3.3V |
| Driver Ability 1 | 输出驱动能力 1 | - | 32 | - | mA | VDDIO=3.3V |
| Driver Ability 0 | 输出驱动能力 0 | - | 8 | - | mA | VDDIO=3.3V |
| RPUP0 | 内部上拉电阻 0 | 8 | 10 | 12 | KΩ | - |
| RPUP1 | 内部上拉电阻 1 | 0.24 | 0.3 | 0.36 | KΩ | - |
| RPUP2 | 内部上拉电阻 2 | 160 | 200 | 240 | KΩ | - |
| RPDN0 | 内部下拉电阻 0 | 8 | 10 | 12 | KΩ | - |
| RPDN1 | 内部下拉电阻 1 | 0.24 | 0.3 | 0.36 | KΩ | - |
| RPDN2 | 内部下拉电阻 2 | 160 | 200 | 240 | KΩ | - |

### 9.3 Audio DAC 参数

| 符号 | 特性 | 典型 | 单位 | 条件 |
|:---|:---|:---|:---|:---|
| SNR | 信噪比 | 98.8 | dB | VCM cap=NC, VDDDAC cap=1uF, A-wt filter, Output=-4.2dBV, Fin=1KHz |
| THD+N | 总谐波失真+噪声 | -73 | dB | 同上，10K 负载 |
| Output Range | 最大输出电压 | -4.2 | dBVrms | 32Ω 负载 |

### 9.4 Audio ADC 参数

| 符号 | 特性 | 典型 | 单位 | 条件 |
|:---|:---|:---|:---|:---|
| SNR | 信噪比 | 90 | dB | VCM cap=NC, VDDDAC cap=1uF, A-wt filter, 输入 850mV RMS, Fin=1KHz |
| Input Range | 输入正弦波峰值幅度 | VCM-1.2 ~ VCM+1.2 | V | 从 AUX 输入，AUX 0dB 增益 |

### 9.5 BT 参数

| 特性 | 最小 | 典型 | 最大 | 单位 | 条件 |
|:---|:---|:---|:---|:---|:---|
| 最大发射功率 | - | - | 9 | dBm | - |
| RMS DEVM | - | 5.5 | - | % | 最大发射功率，2-DH5 包 |
| Peak DEVM | - | 12.5 | - | % | 同上 |
| EDR 相对发射功率 | - | -0.2 | - | dB | 同上 |
| 基本速率灵敏度 | - | -91.8 / -94 | - | dBm | BER=0.1%，DH5 包 |

### 9.6 电流参数

| 符号 | 特性 | 典型 | 最大 | 单位 | 条件 |
|:---|:---|:---|:---|:---|:---|
| IRTC | RTC 模式电流 | 4 | - | uA | 4.2V 输入，室温 |
| Sleep | 睡眠电流 | 500 | 2000 | uA | 3.3V 输入，室温 |

---

## 10. 缺失内容说明

根据手册实际内容，以下章节**在原手册中未提供**，驱动开发时需注意向原厂或同事索取补充资料：

| 缺失内容 | 说明 | 驱动开发影响 |
|:---|:---|:---|
| **引脚定义图（Pinout）** | 手册未提供封装引脚排列图和引脚功能表 | 需通过 `FUNCMCONx` 寄存器映射推断 GPIO 功能分配，或向硬件同事索取原理图 |
| **时序图说明** | 未提供 SPI/IIC/UART 等接口的详细时序图 | 需参考通用时序和寄存器配置进行调试，或向原厂索取完整 datasheet |
| **应用电路图** | 未提供典型应用电路和外围器件推荐 | 需参考现有项目原理图或向硬件同事确认外围电路设计 |
| **存储器映射** | 未提供完整的内存映射表（寄存器基地址） | 需结合 SDK 头文件中的基地址定义进行开发 |
| **时钟树** | 未提供系统时钟结构和分频关系详细图 | 需通过 `BAUDSEL`、`CLKSRC` 等寄存器位推断时钟配置 |

---

## 附录：驱动开发速查表

| 外设 | 关键寄存器 | 启动/使能位 | 中断/状态位 | 波特率公式 |
|:---|:---|:---|:---|:---|
| **UART0** | UART0CON, UART0BAUD, UART0DATA | UTEN (bit0) | RXPND/TXPND, TXIE/RXIE | Fsys / (BAUD + 1) |
| **SPI0** | SPI0CON, SPI0BAUD, SPI0BUF | SPIEN (bit0) | SPIPND, SPIIE | Fsys / (BAUD + 1) |
| **IIC** | IICON0, IICON1, IICCMDA, IICDATA | IIC_EN (bit0), KS (bit28) | DONE (bit31), INTEN | source_clk / ((pre+1)*(pos+1)) |
| **Timer0/1/2** | TMRxCON, TMRxCNT, TMRxPR | TMREN (bit0) | TPND, TIE | 周期 = PR + 1 |
| **Timer3/4/5** | TMRxCON, TMRxCNT, TMRxPR, TMRxDUTYx | TMREN (bit0), PWMxEN | TPND/CPND, TIE/CIE | 周期 = PR + 1 |
| **GPIO** | GPIOxDIR, GPIOxPU/GPIOxPD, GPIOxDE, GPIOxFEN | GPIOxFEN (功能使能) | PORTINTEN, PORTINTEDG | - |
| **RTC** | RTCCON, RTCCNT, RTCALM, RTCCON3 | 多电源控制位 | ALMPND, RTC1SIE, ALMIE | 32 位秒计数 |

---

> **文档说明**: 本手册基于 BT892X User Manual v0.0.1 (2021.03.30) 整理。该版本为早期草稿版本，部分章节（引脚定义、时序图、应用电路、完整内存映射等）未包含在内。建议结合最新版 SDK 和完整 datasheet 进行驱动开发。
