# BT892X 引脚功能 (Pin Function) 表

> **文档日期:** 2023-08-30  
> **适用芯片:** BT892X (Audio Player Microcontroller)  
> **关联文档:** bt892x_usermanual.pdf (寄存器手册)

---

## 目录

- [1. 概述](#1-概述)
- [2. 表头说明 (Header Mapping)](#2-表头说明-header-mapping)
- [3. FUNCMCON 寄存器映射 (控制位)](#3-funcmcon-寄存器映射-控制位)
- [4. 通用 IO 引脚定义 (PA/PB/PE/PF/PG)](#4-通用-io-引脚定义-papbepfpg)
  - [4.1 PORTA 引脚 (PA3–PA7)](#41-porta-引脚-pa3pa7)
  - [4.2 PORTB 引脚 (PB0–PB5)](#42-portb-引脚-pb0pb5)
  - [4.3 PORTE 引脚 (PE0, PE4–PE7)](#43-porte-引脚-pe0-pe4pe7)
  - [4.4 PORTF 引脚 (PF0–PF5)](#44-portf-引脚-pf0pf5)
  - [4.5 PORTG 引脚 (PG1, PG2, PG4, PG5)](#45-portg-引脚-pg1-pg2-pg4-pg5)
- [5. 音频 DAC 引脚](#5-音频-dac-引脚)
- [6. 电源 / USB 引脚](#6-电源--usb-引脚)
- [7. 参考源引脚 (Analog Reference)](#7-参考源引脚-analog-reference)
- [8. 信号速查表](#8-信号速查表)

---

## 1. 概述

本表为 **BT892X** 多功能 IO 引脚的功能复用表。每个 PAD 可根据 `FUNCMCON` 相关寄存器配置为不同的外设功能。

- 表中所有引脚默认 **上电后为 INPUT（输入）/ Hiz（高阻）** 状态。
- **Drive current** 默认值由 `GPIOADRV`/`GPIOBDRV` 等寄存器决定（详见 user manual §3.2）。
- 通过 `FUNCMCON0/1/2/3` 选择外设功能映射（详见 user manual §3.3）。
- 表中 "G1/G2/G3…" 表示该功能可映射到对应 Group 的某个具体 PAD，通过对应 FUNCMCON 位域选择。

---

## 2. 表头说明 (Header Mapping)

| 列 | 表头 (Header) | 说明 |
|----|---------------|------|
| A | **PAD Name** | 引脚名称 |
| B | **备注** (Remarks) | 引脚用途说明 |
| C | **IO TYPE** | IO 类型（TYPE1 / TYPE4 等）|
| D | **Power** | 供电域（VDDIO 等）|
| E | **Pull up** | 上拉电阻选项（如 0.3K / 10K / 200K, 即 0.3KΩ / 10KΩ / 200KΩ）|
| F | **Pull down** | 下拉电阻选项 |
| G | **Drive current(mA)** | 可选驱动电流档位（如 8/32 = 8mA 或 32mA）|
| H | **Deault Drive current(mA)** | 默认驱动电流 |
| I | **H/L/Hiz** | 复位后默认电平（High / Low / 高阻）|
| J | **At Reset** | 复位时引脚状态（默认 INPUT）|
| K | **ADC** | ADC 输入通道（如 ADC0~ADC15）|
| L | **AUX** | 模拟 AUX 输入（AUXL0~3 / AUXR0~3）|
| M | **SD Card** | SD 卡控制器功能 |
| N | **SPI0** | SPI0 接口 |
| O | **SPI1** | SPI1 接口 |
| P | **UART0** | UART0 (TX/RX) |
| Q | **UART1** | UART1 (TX/RX) |
| R | **UART2** | UART2 (TX/RX) |
| S | **HS UART** | 高速 UART (HSTRX 等) |
| T | **FMOSC OUT** | FM 振荡器输出 |
| U | **General PWM-T3** | 通用 PWM - Timer3 |
| V | **General PWM-T4** | 通用 PWM - Timer4 |
| W | **General PWM-T5** | 通用 PWM - Timer5 |
| X | **IIS** | IIS 音频接口 |
| Y | **IIC** | IIC 接口 |
| Z | **DVP** | DVP（数字视频端口）接口 |
| AA | **Timers/IR** | 定时器捕获 / IR |

---

## 3. FUNCMCON 寄存器映射 (控制位)

下表是每个外设列对应的 **FUNCMCON** 寄存器位域，用于控制该引脚是否映射到对应功能：

| 寄存器位域 | 对应列 (功能) |
|------------|---------------|
| `FUNCMCON0[3:0]` | M (SD Card) |
| `FUNCMCON0[8:4]` | N (SPI0) |
| `FUNCMCON1[15:12]` | O (SPI1) |
| `TXMAP=FUNCMCON0[11:8]` / `RXMAP=FUNCMCON0[15:12]` <br/>*when RXMAP=0x7, TX pin will map to RX* | P (UART0) |
| `TXMAP=FUNCMCON0[27:24]` / `RXMAP=FUNCMCON0[31:28]` <br/>*when RXMAP=0x7, TX pin will map to RX* | Q (UART1) |
| `TXMAP=FUNCMCON1[7:4]` / `RXMAP=FUNCMCON1[11:8]` <br/>*when RXMAP=0x7, TX pin will map to RX* | R (UART2) |
| `TXMAP=FUNCMCON0[19:16]` / `RXMAP=FUNCMCON0[23:20]` | S (HS UART) |
| `FUNCMCON1[3:0]` | T (FMOSC OUT) |
| `FUNCMCON2[11:8]` | U (General PWM-T3) |
| `FUNCMCON2[15:12]` | V (General PWM-T4) |
| `FUNCMCON2[19:16]` | W (General PWM-T5) |
| `FUNCMCON2[3:0]` | X (IIS) |
| `FUNCMCON2[24:27]` | Y (IIC) |
| `FUNCMCON2[31:28]` | Z (DVP) |
| `IR_MAP=FUNCMCON2[23:20]` <br/>`T3CAP_MAP=FUNCMCON2[7:4]` | AA (Timers/IR) |

> 注意：UART 列说明中的 "when RXMAP=0x7, TX pin will map to RX" 表示当 RXMAP 设置为 0x7 时，TX 引脚会映射到 RX 引脚。

---

## 4. 通用 IO 引脚定义 (PA/PB/PE/PF/PG)

### 4.1 PORTA 引脚 (PA3–PA7)

| PAD | 备注 | IO TYPE | Power | Pull up | Pull down | Drive (mA) | 默认 Drive (mA) | 复位后 | At Reset | ADC | AUX | SD Card | SPI0 | SPI1 | UART0 | UART1 | HS UART | FMOSC | PWM-T3 | PWM-T5 | IIS | IIC | DVP | Timers/IR |
|-----|------|---------|-------|---------|-----------|-----------|----------------|--------|---------|-----|-----|---------|------|------|-------|-------|---------|-------|--------|--------|-----|-----|-----|----------|
| **PA3** |  | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT |  |  |  |  | SPI1CLK-G1 |  | RX1-G2 |  |  | PWM1-T3-G3 |  | IISDI-G1 |  | DVP_DIN[3] |  |
| **PA4** |  | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT |  |  |  |  | SPI1DO/SPI1DATA-G1 |  | TX1-G2(RX) |  |  | PWM2-T3-G3 |  | IISMCLK-G1 |  | DVP_DIN[4] |  |
| **PA5** | SD升级接口0 | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | ADC0 |  | SDCMD-G1/G4/G5 |  | SPI1DI-G1/SPI1DI-G2 |  |  |  | FMOSC-G1 |  | PWM0-T5-G1 | IISSCLK-G1 | IIC_DAT-G2 | DVP_DIN[5] | TMR3CAP_G1/IR_G1 |
| **PA6** |  | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | ADC1 | AUXL0 | SDCLK-G1/G4/G5/G6 |  | SPI1CLK-G2 | RX0-G1 | RX1-G1 | HSTRX-G6 | FMOSC-G2 |  | PWM1-T5-G1 | IISLRCLK-G1 | IIC_CLK-G1/G2 | DVP_DIN[6] | TMR3CAP_G2/IR_G2 |
| **PA7** | *(update)* | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | ADC2 | AUXR0 | SDDAT0-G1/G7 |  | SPI1DO/SPI1DATA-G2 | TX0-G1(RX) | TX1-G1(RX) | HSTRX-G1 |  |  | PWM2-T5-G1 | IISDO/DAT-G1 | IIC_DAT-G1 | DVP_DIN[7] |  |

### 4.2 PORTB 引脚 (PB0–PB5)

| PAD | 备注 | IO TYPE | Power | Pull up | Pull down | Drive (mA) | 默认 Drive (mA) | 复位后 | At Reset | ADC | AUX | SD Card | SPI0 | SPI1 | UART0 | UART1 | UART2 | HS UART | FMOSC | PWM-T3 | IIS | IIC | Timers/IR |
|-----|------|---------|-------|---------|-----------|-----------|----------------|--------|---------|-----|-----|---------|------|------|-------|-------|--------|---------|-------|--------|-----|-----|----------|
| **PB0/WK1** |  | TYEP1_WKO | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | ADC11 |  | SDCMD-G2 |  | SPI1DI-G3 |  |  |  |  | FMOSC-G3 | PWM0-T3-G1 |  | IIC_DAT-G4 | TMR3CAP_G3/IR_G3 |
| **PB1/WK2** |  | TYEP1_WKO | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | ADC3 | AUXL1 | SDCLK-G2 |  | SPI1CLK-G3 | RX0-G2 |  | RX2-G2 | HSTRX-G7 | FMOSC-G4 | PWM1-T3-G1 | IISMCLK-G3/IISMCLK-G2 | IIC_CLK-G3/G4 | TMR3CAP_G4/IR_G4 |
| **PB2/WK3** |  | TYEP1_WKO | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | ADC4 | AUXR1 | SDDAT0-G2 |  | SPI1DO/SPI1DATA-G3 | TX0-G2 |  | TX2-G2(RX) | HSTRX-G2 |  | PWM2-T3-G1 | IISSCLK-G3/IISDI-G2 | IIC_DAT-G3 |  |
| **PB3/USBDP** | USB升级接口, *(update)* | TYEP1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | ADC5 |  | SDDAT0-G5/SDCMD-G6 | SPI0DO-G3 |  | TX0-G3(RX) |  |  | HSTRX-G3 |  | PWM0-T3-G2 |  | IIC_CLK-G8 |  |
| **PB4/USBDM** |  | TYEP1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | ADC6 |  | SDDAT0-G4/G6 | SPI0CLK-G3 |  | RX0-G3 |  |  | HSTRX-G8 |  | PWM1-T3-G2 |  | IIC_DAT-G8 |  |
| **PB5/WKO** | 10S Reset<br/>主唤醒源 | TYEP1_WKO | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | ADC12 |  |  |  |  |  |  |  |  |  | PWM2-T3-G2 | IISDI-G3 |  |  |

### 4.3 PORTE 引脚 (PE0, PE4–PE7)

| PAD | 备注 | IO TYPE | Power | Pull up | Pull down | Drive (mA) | 默认 Drive (mA) | 复位后 | At Reset | ADC | AUX | SD Card | SPI0 | SPI1 | UART0 | HS UART | FMOSC | PWM-T3 | PWM-T4 | IIS | IIC | DVP | Timers/IR |
|-----|------|---------|-------|---------|-----------|-----------|----------------|--------|---------|-----|-----|---------|------|------|-------|---------|-------|--------|--------|-----|-----|-----|----------|
| **PE0** | 高压PIN MUTE | TYPE4 | VDDIO | 10K | 10K | 8 | 8 | hiz | INPUT |  |  |  | SPI0DI-G3 |  | TX0-G5(RX) |  |  | PWM0-T3-G4 |  |  |  |  | TMR3CAP_G5/IR_G5 |
| **PE4/SPIDIN** | SD PG | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT |  |  |  |  |  |  |  |  | PWM1-T3-G4 |  |  |  | DVP_PCLK_IN |  |
| **PE5** |  | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | ADC7 |  | SDCMD-G3 |  | SPI1DI-G4 |  |  | FMOSC-G5 |  | PWM0-T4-G1 | IISSCLK-G2 | IIC_DAT-G6 | DVP_HSYNC(HREF) | TMR3CAP_G6/IR_G6 |
| **PE6** |  | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | ADC8 | AUXL2 | SDCLK-G3 |  | SPI1CLK-G4 | RX0-G4 | HSTRX-G9 | FMOSC-G6 |  | PWM1-T4-G1 | IISLRCLK-G2/IISLRCLK-G3 | IIC_CLK-G5/G6 | DVP_VSYNC | TMR3CAP_G7/IR_G7 |
| **PE7** | ADKEY | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | ADC9 | AUXR2 | SDDAT0-G3 |  | SPI1DO/SPI1DATA-G4 | TX0-G4(RX) | HSTRX-G4 |  |  | PWM2-T4-G1 | IISDO/DAT-G2/IISDO/DAT-G3 | IIC_DAT-G5 |  | TMR4CAP_G1/IR_G8 |

### 4.4 PORTF 引脚 (PF0–PF5)

| PAD | 备注 | IO TYPE | Power | Pull up | Pull down | Drive (mA) | 默认 Drive (mA) | 复位后 | At Reset | ADC | AUX | SD Card | SPI1 | UART0 | PWM-T3 | PWM-T4 | PWM-T5 | IIC | DVP | Timers/IR |
|-----|------|---------|-------|---------|-----------|-----------|----------------|--------|---------|-----|-----|---------|------|-------|--------|--------|--------|-----|-----|----------|
| **PF0** |  | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT |  |  |  |  |  | PWM0-T3-G3 |  |  |  | DVP_DIN[2] |  |
| **PF1** |  | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT |  |  |  | SPI1DI-G5 | TX0-G6(RX) |  | PWM0-T4-G2 |  |  | DVP_DIN[1] | TMR5CAP_G1/IR_G9 |
| **PF2/MICNR** |  | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT |  |  |  |  |  |  | PWM1-T4-G2 |  |  | DVP_DIN[0] |  |
| **PF3/MICPR** |  | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT |  |  |  |  |  |  | PWM2-T4-G2 |  |  | DVP_PCLK_OUT |  |
| **PF4/MICNL** | MICNL->ADCR | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT |  | AUXR3 | SDCLK-G7 | SPI1CLK-G5 |  |  |  | PWM0-T5-G2 | IIC_CLK-G7 |  |  |
| **PF5/MICPL** | MICPL->ADCL | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | ADC10 | AUXL3 | SDCMD-G7 | SPI1DO/SPI1DATA-G5 | TX0-G7(RX) |  |  | PWM1-T5-G2 | IIC_DAT-G7 |  |  |

### 4.5 PORTG 引脚 (PG1, PG2, PG4, PG5)

> PG 引脚为 SPI-Flash / MCP 专用引脚 (默认连接到外挂 SPI Flash / MCP)。  
> 默认 IO 类型 TYPE1，可在 `FUNCMCON0` 控制下切到 GPIO 模式。

| PAD | 备注 | IO TYPE | Power | Pull up | Pull down | Drive (mA) | 默认 Drive (mA) | 复位后 | At Reset | SPI0 |
|-----|------|---------|-------|---------|-----------|-----------|----------------|--------|---------|------|
| **PG1/SPIDIN--MCP_SO** | MCP SPI Data In (Slave Output) | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | SPI0DIN-G1 / SPI4W_DIO1/SPI4W_DI0 |
| **PG2/SPICS--MCP_CS** | MCP SPI Chip Select | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | SPI0CS-G1 |
| **PG4/SPICLK--MCP_CLK** | MCP SPI Clock | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | SPI0CLK-G1 / SPI4W_CLK |
| **PG5/SPIDOUT--MCP_SI** | MCP SPI Data Out (Slave Input) | TYPE1 | VDDIO | 0.3K\|10K\|200K | 0.3K\|10K\|200K | 8/32 | 8 | hiz | INPUT | SPI0DO/SPI0DATA-G1 / SPI4W_DIO0/SPI4W_DO0 |

---

## 5. 音频 DAC 引脚

| PAD | 备注 | 复用映射 |
|-----|------|----------|
| **DACR** | 右声道 DAC 输出 | DACR -> DACL |
| **DACR#** | 右声道 DAC 反相输出 (差分模式) | DACR# -> DACR |
| **DACL** | 左声道 DAC 输出 | (无映射) |
| **DACL#/VCMBUF** | 左声道 DAC 反相输出 / 共模电压缓冲 | DACL# -> DACL |

> **说明**:  
> - 单端模式下使用 **DACR** 与 **DACL** 输出。  
> - 差分模式下使用 **DACR / DACR#** 与 **DACL / DACL#** 输出。  
> - `VCMBUF` 是 VCM (Common Mode Voltage) 缓冲输出。  
> - 复用映射可选: `DACR#->DACR` 表示将 DACR# 引脚复用为 DACR 输出 (单端模式)；同理 `DACR->DACL` 表示 DACR 改为 DACL 输出。

---

## 6. 电源 / USB 引脚

| PAD | 备注 | UART0 | UART1 | UART2 | HS UART |
|-----|------|-------|-------|-------|---------|
| **VUSB** *(update)* | USB VBUS 输入 / USB 升级接口 | TX0-G8(RX) | TX1-G3(RX) | TX2-G3(RX) | HSTRX-G11 |

> 这些信号功能通过 FUNCMCON 中的对应 MAP 域选择。

---

## 7. 参考源引脚 (Analog Reference)

| PAD | 备注 | ADC |
|-----|------|-----|
| **BG** | 参考源 VBG 电源 (Bandgap Reference) | ADC13 |
| **VBATDIV2** | 电池电压 1/2 分压 (Battery Voltage / 2) | ADC14 |
| **VUSBDIV** | 充电电压 1/3 分压 (USB Voltage / 3) | ADC15 |

> 这三个参考电压可通过 SARADC 读取，用于 PMU 监控和电池电量检测。

---

## 8. 信号速查表

### 8.1 UART 信号

| 信号 | 可分配 PAD |
|------|-----------|
| UART0 TX | PA5, PA6, PA7, PB1, PB2, PB3, PB4, PE0, PE5, PE6, PE7, PF1, PF5, VUSB(G8) |
| UART0 RX | PA5, PA6, PA7, PB1, PB2, PB3, PB4, PE0, PE5, PE6, PE7, PF1, VUSB(G8) |
| UART1 TX/RX | PA3, PA4, PA5, PA6, PA7, PB2, VUSB(G3) |
| UART2 TX/RX | PB1, PB2, VUSB(G3) |
| HSTRX (高速 UART) | PA5, PA6, PA7, PB1, PB2, PB3, PE5, PE6, PE7, VUSB(G11) |

### 8.2 SPI 信号

| 信号 | 可分配 PAD |
|------|-----------|
| SPI0 CS / DIN / DO / CLK | PB3, PB4, PE0, PG1, PG2, PG4, PG5 |
| SPI1 CS / DIN / DO / CLK | PA3, PA4, PA5, PA6, PA7, PB0, PB1, PB2, PE5, PE6, PE7, PF1, PF4, PF5 |
| SPI 4-Wire / Bidirectional | PG1, PG4, PG5 (与 SPI0 复用) |

### 8.3 SD Card 信号

| 信号 | 可分配 PAD |
|------|-----------|
| SDCMD | PA5, PB0, PE5, PF5 |
| SDCLK | PA6, PB1, PE6, PF4 |
| SDDAT0 | PA7, PB2, PB3, PB4, PE7 |

### 8.4 IIS 信号

| 信号 | 可分配 PAD |
|------|-----------|
| IISMCLK | PA4, PB1 |
| IISSCLK | PA5, PB2, PE5 |
| IISDO/DAT | PA7, PE7 |
| IISDI | PA3, PB5 |
| IISLRCLK | PA6, PE6 |

### 8.5 IIC 信号

| 信号 | 可分配 PAD |
|------|-----------|
| IIC_CLK (SCL) | PA6, PB1, PE6, PF4 |
| IIC_DAT (SDA) | PA5, PA7, PB0, PB2, PB3, PB4, PE5, PE7, PF5 |

### 8.6 PWM 信号

| 通道 | 可分配 PAD |
|------|-----------|
| PWM0-T3 | PB0, PB1, PB2, PB3, PE0, PE4, PF0 |
| PWM1-T3 | PA3, PA4, PB1, PB2, PB3, PE4 |
| PWM2-T3 | PA4, PB2, PB5, PE0 |
| PWM0-T4 | PE5, PE6, PF1 |
| PWM1-T4 | PE6, PF2, PF1 |
| PWM2-T4 | PE7, PF3, PF1 |
| PWM0-T5 | PA5, PF4 |
| PWM1-T5 | PA6, PF5 |
| PWM2-T5 | PA7 |

### 8.7 DVP (数字视频端口) 信号

| 信号 | 可分配 PAD |
|------|-----------|
| DVP_PCLK_IN | PE4 |
| DVP_PCLK_OUT | PF3 |
| DVP_HSYNC (HREF) | PE5 |
| DVP_VSYNC | PE6 |
| DVP_DIN[0] | PF2 |
| DVP_DIN[1] | PF1 |
| DVP_DIN[2] | PF0 |
| DVP_DIN[3] | PA3 |
| DVP_DIN[4] | PA4 |
| DVP_DIN[5] | PA5 |
| DVP_DIN[6] | PA6 |
| DVP_DIN[7] | PA7 |

### 8.8 Timer / IR 捕获

| 信号 | 可分配 PAD |
|------|-----------|
| TMR3CAP / IR_G1 | PA5 |
| TMR3CAP / IR_G2 | PA6 |
| TMR3CAP / IR_G3 | PB0 |
| TMR3CAP / IR_G4 | PB1 |
| TMR3CAP / IR_G5 | PE0 |
| TMR3CAP / IR_G6 | PE5 |
| TMR3CAP / IR_G7 | PE6 |
| TMR4CAP / IR_G8 | PE7 |
| TMR5CAP / IR_G9 | PF1 |

### 8.9 FMOSC (FM 振荡器)

| 信号 | 可分配 PAD |
|------|-----------|
| FMOSC OUT | PA5, PA6, PB0, PB1, PE5, PE6 |

### 8.10 SARADC 通道分配

| ADC 通道 | 引脚 |
|----------|------|
| ADC0 | PA5 |
| ADC1 | PA6 |
| ADC2 | PA7 |
| ADC3 | PB1 |
| ADC4 | PB2 |
| ADC5 | PB3 |
| ADC6 | PB4 |
| ADC7 | PE5 |
| ADC8 | PE6 |
| ADC9 | PE7 |
| ADC10 | PF5 |
| ADC11 | PB0 |
| ADC12 | PB5 |
| ADC13 | BG |
| ADC14 | VBATDIV2 |
| ADC15 | VUSBDIV |

### 8.11 AUX (模拟辅助输入)

| AUX | 引脚 |
|-----|------|
| AUXL0 | PA6 |
| AUXR0 | PA7 |
| AUXL1 | PB1 |
| AUXR1 | PB2 |
| AUXL2 | PE6 |
| AUXR2 | PE7 |
| AUXL3 | PF5 |
| AUXR3 | PF4 |

---

## 附录 A: 关键使用注意事项

1. **上拉电阻**: TYPE1 引脚支持 0.3K / 10K / 200K 三档可配置上下拉；TYPE4 (PE0) 仅 10K 固定上下拉（用于高压 MUTE PIN）。
2. **驱动电流**: GPIO 默认为 8mA，可配置为 32mA（详见 `GPIOADRV` 寄存器）。
3. **复用冲突**: 选择某项功能 (如 SPI0) 时，需通过 `FUNCMCON0` 位域选择具体的 Group (G1~G7)，并且保证该 Group 内未被其他外设独占。
4. **唤醒源**: PB0/WK1、PB1/WK2、PB2/WK3、PB5/WKO 兼作唤醒源；PB5 为 **10S Reset 主唤醒源**。
5. **USB 升级接口**: PB3 (USBDP) 与 PB4 (USBDM) 同时用作 USB 升级接口，需要保留为 USB 模式进行烧录。
6. **PG 引脚**: 默认为 MCP / SPI-Flash 接口信号，不可随意占用。
7. **DAC 差分模式**: 通过 `DACR#->DACR` / `DACL#->DACL` 复用，可选差分或单端输出。

---

## 附录 B: 主要寄存器索引

| 寄存器 | 用途 | 详见手册章节 |
|--------|------|-------------|
| `FUNCMCON0[3:0]` | SD Card Group 选择 | §3.3 |
| `FUNCMCON0[8:4]` | SPI0 Group 选择 | §3.3 |
| `FUNCMCON0[11:8]` | UART0 TX Group | §3.3 |
| `FUNCMCON0[15:12]` | UART0 RX Group | §3.3 |
| `FUNCMCON0[19:16]` / `[23:20]` | HS UART TX/RX Group | §3.3 |
| `FUNCMCON0[27:24]` / `[31:28]` | UART1 TX/RX Group | §3.3 |
| `FUNCMCON1[3:0]` | FMOSC Group 选择 | §3.3 |
| `FUNCMCON1[7:4]` / `[11:8]` | UART2 TX/RX Group | §3.3 |
| `FUNCMCON1[15:12]` | SPI1 Group 选择 | §3.3 |
| `FUNCMCON2[3:0]` | IIS Group 选择 | §3.3 |
| `FUNCMCON2[7:4]` | T3 Capture Group | §3.3 |
| `FUNCMCON2[11:8]` | PWM-T3 Group | §3.3 |
| `FUNCMCON2[15:12]` | PWM-T4 Group | §3.3 |
| `FUNCMCON2[19:16]` | PWM-T5 Group | §3.3 |
| `FUNCMCON2[23:20]` | IR Group | §3.3 |
| `FUNCMCON2[24:27]` | IIC Group | §3.3 |
| `FUNCMCON2[31:28]` | DVP Group | §3.3 |
| `FUNCMCON3[3:0]` / `[7:4]` | PDM / MPDM Group | §3.3 |

---

*Copyright © 2021, www.bluetrum.com. All Rights Reserved.*
