# BT892X SPI1 测试报告

> **日期**: 2026-07-15  
> **芯片**: BT892X  
> **测试工具**: 逻辑分析仪、跳线  
> **参考手册**: BT892X_UserManual_Driver.md §7 SPI  
> **测试结果**: ✅ 通过（256 字节回环零错误）

---

## 1. 测试目的

验证 BT892X SPI1 硬件模块的主机模式收发功能。

---

## 2. 测试原理

### 2.1 SPI 寄存器（参考手册 §7）

| 寄存器 | 功能 |
|--------|------|
| `SPI1CON` | 控制寄存器（SPIEN, SPISM, BUSMODE, CLKIDS, SMPS, SPIOSS, SPIPND） |
| `SPI1BUF` | 数据寄存器（写=发送, 读=接收） |
| `SPI1BAUD` | 波特率 = Fsys / (BAUD + 1) |
| `SPI1CPND` | 清除挂起（写 BIT(16) 清除 SPIPND） |

### 2.2 配置参数

| 参数 | 值 |
|------|-----|
| SPI 模块 | SPI1（SPI0 被板载 MCP Flash 占用） |
| 模式 | Mode 0 (CPOL=0, CPHA=0) |
| CLKIDS | 0（空闲低） |
| SMPS | 0（下降沿输出数据） |
| SPIOSS | 0（采样与输出不同边沿 → 上升沿采样） |
| 主机/从机 | 主机（SPISM=0） |
| 数据宽度 | 8 位，3 线模式（BUSMODE=00） |
| 时钟 | 100KHz（SPI1BAUD = 239, 24MHz/240 = 100KHz） |

### 2.3 引脚映射

| 信号 | 映射组 | 引脚 | FUNCMCON1[15:12] |
|------|--------|------|-------------------|
| SPI1 CLK | G4 | **PE6** | 0x4 |
| SPI1 MOSI | G4 | **PE7** | 0x4 |
| SPI1 MISO | G4 | **PE5** | 0x4 |

---

## 3. 硬件连接

```
PE6(CLK)  ─── 逻辑分析仪 CH1
PE7(MOSI) ─── 逻辑分析仪 CH2 ──[跳线]── PE5(MISO)
GND       ─── 逻辑分析仪 GND
```

---

## 4. 测试步骤与结果

### 4.1 发送模式测试 — ✅

发送 0x55, 0xAA, 0x00, 0xFF, 0xF0, 0x0F，逻辑分析仪捕获 CLK 和 MOSI。

- 初始用 1MHz 时钟，逻辑分析仪采样率（1µs）接近 Nyquist 极限，产生混叠
- 改 100KHz 后波形清晰

### 4.2 回环测试 — ✅

跳线 PE7(MOSI) ↔ PE5(MISO)，发送 0x00~0xFF 共 256 字节全通过，零错误。

---

## 5. 关键代码

```c
// SPI1 G4 映射
FUNCMCON1 &= ~(0xF << 12);
FUNCMCON1 |=  (0x4 << 12);

// PE6/PE7/PE5 → 功能IO
GPIOEFEN |= BIT(6) | BIT(7) | BIT(5);
GPIOEDE  |= BIT(6) | BIT(7) | BIT(5);
GPIOEDIR &= ~(BIT(6) | BIT(7));    // CLK, MOSI = 输出
GPIOEDIR |=  BIT(5);               // MISO = 输入

// 100KHz, Mode 0
SPI1BAUD = 239;
SPI1CON  = BIT(0);                 // SPIEN

// 收发单字节
u8 spi1_transfer(u8 tx) {
    SPI1BUF = tx;
    while (!(SPI1CON & BIT(16)));
    SPI1CPND = BIT(16);
    return (u8)SPI1BUF;
}
```

---

## 6. 结论

✅ **SPI1 硬件模块验证通过**。主机模式 Mode 0 收发正常，回环 256 字节零错误。逻辑分析仪观察建议用 ≤100KHz 时钟以获得清晰波形。
