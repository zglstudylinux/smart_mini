# SARADC_CTL (逐次逼近型 ADC) 数据手册摘要

## 11.1 特性

- 支持 **16 个**输入通道
- 最高采样率：**78k/s**
- SAR ADC 位时钟最大：**1 MHz**
- ADC 内部集成 **100 kΩ** 上拉电阻

---

## 11.2 特殊功能寄存器

### 寄存器 11‑1 SADCCON – SARADC 控制寄存器

| 位    | 名称     | 模式 | 默认值 | 描述                                              |
| ----- | -------- | ---- | ------ | ------------------------------------------------- |
| 31:20 | —        | R    | -      | 未使用                                            |
| 19    | ADCAEN   | WR   | 0      | SARADC 自动使能模拟使能位<br>0：禁用，1：使能     |
| 18    | ADCANGIO | WR   | 0      | SARADC 自动使能模拟 IO 使能位<br>0：禁用，1：使能 |
| 17    | ADCIE    | WR   | 0      | SARADC 中断使能位<br>0：禁用，1：使能             |
| 16    | ADCEN    | WR   | 0      | SARADC 使能位<br>0：禁用，1：使能                 |
| 15    | CH15PUEN | WR   | 0      | 通道 15 内部上拉使能<br>0：禁用，1：使能          |
| 14    | CH14PUEN | WR   | 0      | 通道 14 内部上拉使能                              |
| 13    | CH13PUEN | WR   | 0      | 通道 13 内部上拉使能                              |
| 12    | CH12PUEN | WR   | 0      | 通道 12 内部上拉使能                              |
| 11    | CH11PUEN | WR   | 0      | 通道 11 内部上拉使能                              |
| 10    | CH10PUEN | WR   | 0      | 通道 10 内部上拉使能                              |
| 9     | CH9PUEN  | WR   | 0      | 通道 9 内部上拉使能                               |
| 8     | CH8PUEN  | WR   | 0      | 通道 8 内部上拉使能                               |
| 7     | CH7PUEN  | WR   | 0      | 通道 7 内部上拉使能                               |
| 6     | CH6PUEN  | WR   | 0      | 通道 6 内部上拉使能                               |
| 5     | CH5PUEN  | WR   | 0      | 通道 5 内部上拉使能                               |
| 4     | CH4PUEN  | WR   | 0      | 通道 4 内部上拉使能                               |
| 3     | CH3PUEN  | WR   | 0      | 通道 3 内部上拉使能                               |
| 2     | CH2PUEN  | WR   | 0      | 通道 2 内部上拉使能                               |
| 1     | CH1PUEN  | WR   | 0      | 通道 1 内部上拉使能                               |
| 0     | CH0PUEN  | WR   | 0      | 通道 0 内部上拉使能                               |

---

### 寄存器 11‑2 SADCCH – SARADC 通道使能寄存器

| 位    | 名称   | 模式 | 默认值 | 描述                                                         |
| ----- | ------ | ---- | ------ | ------------------------------------------------------------ |
| 31:17 | —      | R    | -      | 未使用                                                       |
| 16    | ADCPND | WR   | 0      | SARADC 转换完成标志<br>0：未完成，1：完成<br>**写 SADCCH 寄存器会自动清除此位** |
| 15    | CH15EN | WR   | 0      | 通道 15 使能                                                 |
| 14    | CH14EN | WR   | 0      | 通道 14 使能                                                 |
| 13    | CH13EN | WR   | 0      | 通道 13 使能                                                 |
| 12    | CH12EN | WR   | 0      | 通道 12 使能                                                 |
| 11    | CH11EN | WR   | 0      | 通道 11 使能                                                 |
| 10    | CH10EN | WR   | 0      | 通道 10 使能                                                 |
| 9     | CH9EN  | WR   | 0      | 通道 9 使能                                                  |
| 8     | CH8EN  | WR   | 0      | 通道 8 使能                                                  |
| 7     | CH7EN  | WR   | 0      | 通道 7 使能                                                  |
| 6     | CH6EN  | WR   | 0      | 通道 6 使能                                                  |
| 5     | CH5EN  | WR   | 0      | 通道 5 使能                                                  |
| 4     | CH4EN  | WR   | 0      | 通道 4 使能                                                  |
| 3     | CH3EN  | WR   | 0      | 通道 3 使能                                                  |
| 2     | CH2EN  | WR   | 0      | 通道 2 使能                                                  |
| 1     | CH1EN  | WR   | 0      | 通道 1 使能                                                  |
| 0     | CH0EN  | WR   | 0      | 通道 0 使能                                                  |

---

### 寄存器 11‑3 SADCST – SARADC 通道建立时间寄存器

| Bit   | Name   | Mode | Default | Description                                                  |
| ----- | ------ | ---- | ------- | ------------------------------------------------------------ |
| 31:30 | CH15ST | WO   | 0x0     | Channel 15 setup time<br>00: 0 SARADC_CLK<br>01: 2 SARADC_CLK<br>10: 4 SARADC_CLK<br>11: 8 SARADC_CLK |
| 29:28 | CH14ST | WO   | 0x0     | Channel 14 setup time<br>00: 0 SARADC_CLK<br>01: 2 SARADC_CLK<br>10: 4 SARADC_CLK<br>11: 8 SARADC_CLK |
| 27:26 | CH13ST | WO   | 0x0     | Channel 13 setup time<br>00: 0 SARADC_CLK<br>01: 2 SARADC_CLK<br>10: 4 SARADC_CLK<br>11: 8 SARADC_CLK |
| 25:24 | CH12ST | WO   | 0x0     | Channel 12 setup time<br>00: 0 SARADC_CLK<br>01: 2 SARADC_CLK<br>10: 4 SARADC_CLK<br>11: 8 SARADC_CLK |
| 23:22 | CH11ST | WO   | 0x0     | Channel 11 setup time<br>00: 0 SARADC_CLK<br>01: 2 SARADC_CLK<br>10: 4 SARADC_CLK<br>11: 8 SARADC_CLK |
| 21:20 | CH10ST | WO   | 0x0     | Channel 10 setup time<br>00: 0 SARADC_CLK<br>01: 2 SARADC_CLK<br>10: 4 SARADC_CLK<br>11: 8 SARADC_CLK |
| 19:18 | CH9ST  | WO   | 0x0     | Channel 9 setup time<br>00: 0 SARADC_CLK<br>01: 2 SARADC_CLK<br>10: 4 SARADC_CLK<br>11: 8 SARADC_CLK |
| 17:16 | CH8ST  | WO   | 0x0     | Channel 8 setup time<br>00: 0 SARADC_CLK<br>01: 2 SARADC_CLK<br>10: 4 SARADC_CLK<br>11: 8 SARADC_CLK |
| 15:14 | CH7ST  | WO   | 0x0     | Channel 7 setup time<br>00: 0 SARADC_CLK<br>01: 2 SARADC_CLK<br>10: 4 SARADC_CLK<br>11: 8 SARADC_CLK |
| 13:12 | CH6ST  | WO   | 0x0     | Channel 6 setup time<br>00: 0 SARADC_CLK<br>01: 2 SARADC_CLK<br>10: 4 SARADC_CLK<br>11: 8 SARADC_CLK |
| 11:10 | CH5ST  | WO   | 0x0     | Channel 5 setup time<br>00: 0 SARADC_CLK<br>01: 2 SARADC_CLK<br>10: 4 SARADC_CLK<br>11: 8 SARADC_CLK |

| 位   | 名称  | 模式 | 默认值 | 描述                        |
| ---- | ----- | ---- | ------ | --------------------------- |
| 9:8  | CH4ST | WO   | 0x0    | 通道 4 建立时间（编码见上） |
| 7:6  | CH3ST | WO   | 0x0    | 通道 3 建立时间             |
| 5:4  | CH2ST | WO   | 0x0    | 通道 2 建立时间             |
| 3:2  | CH1ST | WO   | 0x0    | 通道 1 建立时间             |
| 1:0  | CH0ST | WO   | 0x0    | 通道 0 建立时间             |

---

### 寄存器 11‑4 SADCBAUD – SARADC 波特率寄存器

| 位    | 名称     | 模式 | 默认值 | 描述                                                         |
| ----- | -------- | ---- | ------ | ------------------------------------------------------------ |
| 31:10 | —        | R    | -      | 未使用                                                       |
| 9:0   | SADCBAUD | WO   | 0x0    | SARADC 波特率<br>**公式**：`Baud Rate = Fadc_clock / [2 × (SADCBAUD + 1)]` |

---

### 寄存器 11‑5 SADC0AT0~15 – SARADC 通道 0~15 数据寄存器

| Bit   | Name    | Mode | Default | Description                                   |
| ----- | ------- | ---- | ------- | --------------------------------------------- |
| 31:10 | -       | -    | -       | Unused                                        |
| 9:0   | SADCDAT | R    | 0x0     | SARADC data, channel 0 to channel 15 register |

---

## 11.3 使用指南 (User Guide)

按照以下步骤配置并使用 SARADC：

1. **配置波特率**  
   设置 `SADCBAUD` 寄存器，根据目标采样率计算合适的值。

2. **配置建立时间（可选）**  
   若需要，配置 `SADCST` 寄存器设置各通道的建立时间。

3. **使能 SARADC 模块**  
   设置 `SADCCON` 寄存器的 `ADCEN` 位为 1。

4. **启动转换**  
   向 `SADCCH` 寄存器写入要使能的通道（可同时使能多个通道）。  
   **写操作会自动清除 `ADCPND` 标志并启动 ADC 转换。**

5. **等待转换完成**  
   轮询 `SADCCH` 寄存器的 `ADCPND` 位，当其变为 1 时表示转换完成。

6. **读取数据**  
   从对应通道的数据寄存器 `SADC0ATx` 读取转换结果。

---

## 注意事项

- 采样率上限为 **78k/s**，请确保配置的波特率和时钟满足该限制。
- 内部上拉电阻（100 kΩ）可通过 `SADCCON` 中各通道的 `PUEN` 位独立使能，适用于无外部上拉的传感器输入。
- 所有寄存器位默认均为 0，上电后 ADC 处于禁用状态。

---

**文档版本**：基于所提供 PDF 内容整理  
**日期**：2026-07-20