# W25Q64 Flash 实验报告

> **日期**: 2026-07-16  
> **芯片**: BT892X + W25Q64JV  
> **通信方式**: 软件 GPIO 模拟 SPI（Mode 0）

---

## 实验 1: 读取 JEDEC ID — ✅ 通过

### 目的

建立 SPI 通信，验证硬件连接正确。

### 原理

- 命令 `0x9F`：Read JEDEC ID
- 返回 3 字节：
  - Manufacturer ID: `0xEF`（Winbond）
  - Memory Type: `0x40`（QSPI Flash）
  - Capacity: `0x17`（64Mbit = 8MB）

### 引脚接线

| BT892X (PORTE) | W25Q64 | 功能 |
|:---:|:---:|------|
| PE4 | CS | 片选 |
| PE5 | CLK | 时钟 |
| PE6 | MOSI | DI (主机→从机) |
| PE7 | MISO | DO (从机→主机) |
| 3.3V | VCC, HOLD, WP | 电源及写保护 |
| GND | GND | 地 |

### 时序

```
CS:  ‾‾‾‾\_______________________/‾‾‾‾
MOSI: ===== \__0x9F__/ ===== \__0xFF__/ \__0xFF__/ \__0xFF__/ =====
MISO: ===== ======== \__0xEF__/ \__0x40__/ \__0x17__/ ===========
```

### 结果

```
JEDEC ID: 0xEF 0x40 0x17
MATCH! W25Q64 detected successfully.
```

### 学到的技能

- SPI 初始化（CS/CLK/MOSI 输出, MISO 输入）
- 片选控制（CS 低有效）
- 软件 SPI 全双工收发（发 0xFF 同时收数据）
- JEDEC ID 识别芯片型号
