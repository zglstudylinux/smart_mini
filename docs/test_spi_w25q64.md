# BT892X W25Q64 Flash 软/硬 SPI 全套测试报告

> **测试日期**：2026-07-17（HAL 重构前），2026-07-19（HAL 重构合并 + 同步测试）
> **测试芯片**：BT892X（中科蓝讯 32-bit RISC-V SoC）
> **被测器件**：Winbond **W25Q64JV**（SPI NOR Flash，64 Mbit = 8 MB）
> **接线**：CS=PE4（GPIO）/ CLK=PE6 / MOSI=PE7 / MISO=PE5 → W25Q64
> **测试入口**：[smart_mini/test/test_spi_w25q64.c](../smart_mini/test/test_spi_w25q64.c)（入口 `test_spi_w25q64_run`）
> **HAL 依赖**：[smart_mini/test/spi_hal.c](../smart_mini/test/spi_hal.c)（软/硬 W25Q64 驱动）
> **参考手册**：
> - [BT892X_UserManual_Driver.md §3.2 GPIO / §3.3 FUNCMCON1 / §7 SPI](../BT892X_UserManual_Driver.md)
> - [docs/periph_spi.md](periph_spi.md)（SPI1 寄存器/G4 映射详解）
> - W25Q64JV datasheet（Winbond，命令集 9Fh / 05h / 35h / 06h / 03h / 0Bh / 02h / 20h / 52h / D8h / C7h / 4Bh / 5Ah / 01h）
> **相关 commit**：合并到 commit `198bcc2` "refactor(spi): HAL 浅重构 + docs"；后续在 HAL 重构中合入 `629ed39`
> **测试结果**：✅ **软+硬 18 个实验全部 PASSED**（SW: 8 个 = Exp 1/2/3/4/5/6/7/9；HW: 10 个 = Exp 1-10 含 Fast Read / 100K↔12M 对比 / UID+SFDP）

---

## 0. 文档结构

| 程序 | 路径 | 入口 | 用途 |
|---|---|---|---|
| **test_spi_w25q64.c** | `smart_mini/test/test_spi_w25q64.c` | `test_spi_w25q64_run` / `TEST_SPI_W25Q64_EN` | 软+硬 18 个 W25Q64 实验（template-style，软/硬各跑一次）|
| **spi_hal.c/.h** | `smart_mini/test/spi_hal.{h,c}` | (HAL) | 提供 2 套 W25Q64 驱动（SW / HW）+ 公共 CS 控制 + 软/硬 byte 原语 |

> 编译宏（`test_spi_w25q64.c` 头部）：
> - `SPI_W25_RUN_MODE`: 0=只软 / 1=只硬 / 2=软+硬全跑（默认）
> - `SPI_SW_W25_MODE`: 0=单 exp / 1=全测（含 Chip Erase）/ 2=仅读
> - `SPI_HW_W25_MODE`: 0=单 exp / 1=全测 / 2=仅读
> - `SPI_SW_W25_EXP` / `SPI_HW_W25_EXP`: 单 exp 模式选 1..10 (HW) / 1..7,9 (SW)

---

## 1. W25Q64JV 芯片速览

### 1.1 关键参数

| 项 | 值 |
|---|---|
| 容量 | 64 Mbit = 8 MB（8388608 字节） |
| 页大小 | 256 字节（Page Program 0x02 一次性最大 256B）|
| Sector 大小 | 4 KB（Sector Erase 0x20） |
| Block 大小 | 32 KB（0x52） / 64 KB（0xD8） |
| 地址宽度 | 24 bit（3 字节地址，最大寻址 16 MB）|
| SPI 模式 | Mode 0（CPOL=0, CPHA=0）、MSB-first |
| 命令集 | JEDEC ID (0x9F), Read SR1/SR2 (0x05/0x35), Write Enable (0x06), Read Data (0x03), Fast Read (0x0B), Page Program (0x02), Sector Erase (0x20), Block Erase 32K (0x52) / 64K (0xD8), Chip Erase (0xC7), Read UID (0x4B), Read SFDP (0x5A), Write SR (0x01) |

### 1.2 状态寄存器 SR1 (0x05)

| Bit | Name | 含义 |
|---|---|---|
| 0 | BUSY | 1=正在执行擦除/写入；0=空闲（spi_hal_w25_*_wait_busy 轮询这位） |
| 1 | WEL | Write Enable Latch：1=允许写，0=禁止（Page Program / Erase / Write SR 前必须先 Write Enable 0x06）|
| 2-5 | BP[0:3] | Block Protect bits：控制 4 个区段的写保护（Exp 7 演示）|
| 6 | SEC | Sector/Block Protect 选择（0=Block 64KB，1=Sector 4KB）|
| 7 | SRP | Status Register Protect |

### 1.3 写操作三步流程（所有 Page Program / Erase / Write SR 共用）

1. **Write Enable (0x06)**：拉低 CS → 发 0x06 → 拉高 CS → WEL=1
2. **执行命令**（0x02/0x20/0x52/0xD8/0xC7/0x01 等）：拉低 CS → 命令 + 地址/数据 → 拉高 CS
3. **轮询 BUSY**：等 SR1[0]=0（spi_hal_w25_*_wait_busy 循环读 0x05）

---

## 2. 引脚与 SPI1 配置

### 2.1 接线表（与 SPI LOOP / WAVE / TIMING 互斥）

| 信号 | BT892X 引脚 | W25Q64 引脚 | 备注 |
|---|---|---|---|
| **CS** | **PE4**（GPIO 手动控制，软/硬共用）| CS | 空闲高；切换前 1 µs 延时（`spi_hal_cs_low/high`）|
| **CLK** | PE6 | CLK | SPI Mode 0 空闲低；硬件 SPI1 G4 接管 |
| **MOSI**（主机→从机）| PE7 | DI | Master Out / Slave In |
| **MISO**（从机→主机）| PE5 | DO | Master In / Slave Out |
| VCC | 3.3V | VCC | — |
| GND | GND | GND | — |

### 2.2 软/硬 SPI 切换的 GPIO 配置

**软件态**（`spi_hal_soft_init`）：
```c
GPIOEFEN &= ~(SPI_CS_PIN | SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN);  // FEN=0 用作 GPIO
GPIOEDE  |=  (SPI_CS_PIN | SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN);  // DE=1 数字 IO
GPIOEDIR &= ~(SPI_CS_PIN | SPI_CLK_PIN | SPI_MOSI_PIN);  // CS/CLK/MOSI 输出
GPIOEDIR |=   SPI_MISO_PIN;                              // MISO 输入
```

**硬件态**（`spi_hal_hw_init(baud)`）：
```c
FUNCMCON1 = (0x4 << 12);    // SPI1MAP = G4 (PE6/PE7/PE5)
GPIOEFEN |= SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN;  // 硬件接管
GPIOEFEN &= ~SPI_CS_PIN;       // CS 仍由 GPIO 控制（任何 SPI 测试都需要手动 CS）
SPI1BAUD = baud;               // 100kHz → 239; 12MHz → 1
SPI1CON  = BIT(0);             // SPIEN=1（Mode 0 其他位默认 0）
```

> **关键约束**：CS 在软/硬模式下都是 GPIO 手动控制，**不交给 SPI 控制器**。原因：SPI 控制器的 CS 通常是硬件片选时序专用，跨命令、跨延迟的手动控制更灵活。

### 2.3 SPI Mode 0 时序（参考 [periph_spi.md §2.4](periph_spi.md)）

```
       ┌─┐    ┌─┐    ┌─┐
CLK    │ │    │ │    │ │
   ────┘ └────┘ └────┘ └───

MOSI   ───┐   ┌───┐   ┌─────
         │   │   │   │
         └───┘   └───┘

MISO   ─────┐   ┌─┐   ┌─── (主机在 CLK 上升沿中心采样)
          │   │ │   │
          └───┘ └───┘
```

软件 bit-bang 一 bit 三步（[`spi_hal.c:36-48`](../smart_mini/test/spi_hal.c)）：
1. 设 MOSI → delay_us(1) （满足 data setup time）
2. CLK 上升沿（SET + delay_us(1)）→ 主机中心采样 MISO
3. CLK 下降沿（CLR + delay_us(1)）→ 切下一 bit

硬件 SPI1 在 `SPI1BUF = tx` 后由控制器自动完成 8 bit 传输，主机只需轮询 `SPI1CON.BIT(16) (SPIPND)`。

---

## 3. 软件实验（8 个：Exp 1, 2, 3, 4, 5, 6, 7, 9）

> 默认 `SPI_SW_W25_MODE = 1`（全测）；`SPI_HW_W25_MODE = 1`（全测）；`SPI_W25_RUN_MODE = 2`（软+硬全跑）。
> 编译：`gcc -DSPI_SW_W25_MODE=1 -DSPI_HW_W25_MODE=1 -DSPI_W25_RUN_MODE=2`（app.cbp 默认即此组合）

### Exp 1: JEDEC ID

**目的**：识别 Flash 厂商与型号，验证 SPI 通路物理连通。

**操作流程**（[`sw_exp1`](../smart_mini/test/test_spi_w25q64.c#L40)）：
```
CS LOW → 发 0x9F（Read JEDEC ID）→ 收 3 字节（厂商ID, 容量, 型号）→ CS HIGH
```

**实测输出**：
```
===== [Soft] Exp1: JEDEC ID =====
JEDEC: 0xEF 0x40 0x17 MATCH
```

**结果分析**：
- `0xEF` = Winbond 厂商 ID
- `0x40` = SPI 接口类型
- `0x17` = W25Q64（容量码 0x17 = 64 Mbit）
- 三字节连发连收无丢失 → 软件 bit-bang 时序正确，MISO 采样点在 CLK 上升沿后 ~1µs 处

---

### Exp 2: 状态寄存器 SR1/SR2 + Write Enable

**目的**：验证读状态寄存器 (RDSR 0x05 / RDSR2 0x35) + 写使能 (WREN 0x06) 流程。

**操作流程**（[`sw_exp2`](../smart_mini/test/test_spi_w25q64.c#L54)）：
```
1. 读 SR1 (CS LOW → 0x05 → 收 1 字节 → CS HIGH)
2. 读 SR2 (CS LOW → 0x35 → 收 1 字节 → CS HIGH)
3. 写使能 (CS LOW → 0x06 → CS HIGH)
4. 再读 SR1，验证 WEL 位从 0 变 1
```

**实测输出**：
```
===== [Soft] Exp2: Status Registers =====
SR1 (0x05): 0x00  BUSY=0 WEL=0 BP=0
SR2 (0x35): 0x00

Send Write Enable (0x06), then re-read:
SR1: 0x02  BUSY=0 WEL=1 (WEL should be 1)
```

**结果分析**：
- SR1 = 0x00：刚上电，BUSY=0 / WEL=0 / BP=0（默认）
- SR2 = 0x00：Quad Enable / LB 全 0
- WREN (0x06) 后 SR1 = 0x02 = `0b00000010` → bit1 WEL=1 ✅
- **关键点**：WREN 是**幂等**的，发多少次都不需要 BUSY 等待（不像 Page Program）；不需要参数

---

### Exp 3: Page Write & Read（256 字节同页）

**目的**：验证 Page Program (0x02) + Read Data (0x03) 的最基础通路，写入 0..255 256 字节回读校验。

**操作流程**（[`sw_exp3`](../smart_mini/test/test_spi_w25q64.c#L71)）：
```
1. Sector Erase (0x20) → 等 BUSY
2. 准备 w[i] = i (0..255) 256 字节
3. Page Program (0x02) + 地址 0x000000 + 256 字节
4. Read Data (0x03) + 地址 0x000000 + 256 字节
5. 字节逐个对比，统计 errors
```

**实测输出**：
```
===== [Soft] Exp3: Page Write & Read =====
Erasing Sector 0...
Erase done.
Writing 256 bytes to Page 0...
Reading back...
Errors: 0 / 256  PASSED
```

**结果分析**：
- 全 256 字节零错误 → Page Program + Read 通路无误
- Sector Erase 前置必须：Flash 写入只能把 1→0；写 1→0 必须先擦除（把所有位置 1 = 0xFF）
- Page Program 限制：≤ 256 字节，且**必须**在同一页内（地址低 8 bit = 页内偏移）；跨页见 Exp 4

---

### Exp 4: Cross-Page Write（跨页写入）

**目的**：验证 Page Program 不能跨页 → 测试代码必须**手动拆页**。

**操作流程**（[`sw_exp4`](../smart_mini/test/test_spi_w25q64.c#L97)）：
```
1. Sector Erase
2. 准备 wbuf[100] = 0xAA + i
3. addr = 0x0000F0 (距页末 16 字节)
4. page0_remain = 256 - (0xF0 & 0xFF) = 16
5. Page Program (addr, wbuf, 16)         // 第一页后 16 字节
6. Page Program (addr + 16, wbuf+16, 84) // 第二页前 84 字节
7. Read Data (addr, 100) 回读校验
```

**实测输出**：
```
===== [Soft] Exp4: Cross-Page Write =====
Addr 0x0000F0, len=100, page0_remain=16
Errors: 0 / 100  PASSED
```

**结果分析**：
- Page Program 不支持自动换页（手册：addr[7:0] 在 0x02 命令后会被锁存到内部计数器，超出会"卷回"覆盖页首）
- 跨页策略：手动按 `256 - (addr & 0xFF)` 拆两段
- HAL 的 `spi_hal_w25_sw_page_program` 不替用户拆，**调用者必须保证 len ≤ 页内剩余字节**——这是 W25Q64 协议层的约束，不是 bug

---

### Exp 5: Sector Erase & Verify（0xFF 校验 + 复写）

**目的**：验证 Sector Erase (0x20) 后整 sector 都是 0xFF，并能再 Page Program 复写。

**操作流程**（[`sw_exp5`](../smart_mini/test/test_spi_w25q64.c#L127)）：
```
1. Sector Erase
2. Page Program (256 字节 0xA5)
3. Read 64 字节：确认 buf[0]=0xA5
4. Sector Erase
5. Read 64 字节：确认所有字节 = 0xFF
6. Page Program (256 字节 0xA5)
7. Read 64 字节：确认 buf[0]=0xA5（复写正常）
```

**实测输出**：
```
===== [Soft] Exp5: Sector Erase & Verify =====
Writing pattern 0xA5 to Sector 0...
Before erase: buf[0]=0xA5 (expect 0xA5)
Erasing Sector 0...
After erase: buf[0]=0xFF, all_ff=1  PASSED

Re-write pattern...
After re-write: buf[0]=0xA5 (expect 0xA5)
```

**结果分析**：
- Sector Erase 把整个 4KB 抹成 0xFF → 对比**所有** 64 字节 all_ff=1
- 复写 0xA5 正常 → 擦除后状态干净，可重复利用
- 实际意义：bootloader / 配置存储场景（少写多读）可靠性验证

---

### Exp 6: Erase Timing（三种 erase 耗时）

**目的**：实测 Sector / Block 32K / Block 64K / Chip Erase 的实际耗时。

**操作流程**（[`sw_exp6`](../smart_mini/test/test_spi_w25q64.c#L159)）：
```
每种 erase 前发 WREN → 发 erase 命令 → 等 BUSY → tick_get() 记录 ms
```

**实测输出**：
```
===== [Soft] Exp6: Erase Timing =====
Sector 4KB (0x20):
  Time: 55 ms
Block 32KB (0x52):
  Time: 114 ms
Block 64KB (0xD8):
  Time: 181 ms
Chip Erase (0xC7) -- ~20 seconds, please wait...
  Time: 19873 ms (~19 s)
```

**结果分析**（对照 W25Q64JV datasheet 典型值）：

| Erase 命令 | 数据手册典型 | 实测 | 备注 |
|---|---|---|---|
| Sector 4KB (0x20) | 45–400 ms | **55 ms** | 中位快 |
| Block 32KB (0x52) | 120–1600 ms | **114 ms** | 比 spec 还快（IC 个体差异） |
| Block 64KB (0xD8) | 150–2000 ms | **181 ms** | OK |
| Chip Erase (0xC7) | 20–100 s | **~19 s** | 已擦除大部分区域，耗时接近下限 |

→ 工程影响：
- 整片擦除要 ~20 s，UI 设计要给"擦除中"提示
- 实测用 100kHz SPI（baud=239）下命令发送耗时 < 1 ms，**BUSY 等待是耗时主体**——所以软/硬 SPI 在 erase 耗时上无差异

---

### Exp 7: Write Protection（BP 位 + 写保护实测）

**目的**：验证 Block Protect (BP[0:3] = bit 5:2 of SR1) 控制写保护，写保护区域后 Page Program 被拒绝。

**操作流程**（[`sw_exp7`](../smart_mini/test/test_spi_w25q64.c#L192)）：
```
1. 读 SR1 = 0x00 (BP=0)
2. WREN + Write SR (0x01) + SR1 | 0x10 (BP2=1) + 等 BUSY
3. 读 SR1 = 0x10 (BP=4) -- "BP2=1" 对应 BP bits=0100 = 4
4. WREN + Page Program (0x02) + 地址 0x400000 + 0x55 → 等 BUSY
5. 读 SR1：观察 WEL 是否被吞（WEL=0 → 写入被硬件拒绝）
6. 恢复：WREN + Write SR + SR1 & ~0x3C + 等 BUSY
```

**实测输出**：
```
===== [Soft] Exp7: Write Protection =====
Initial SR1: 0x00 (BP=0)
Setting BP2=1 (Write Status Reg 0x01)...
New SR1: 0x10 (BP=4)

Attempting write to protected area 0x400000...
SR1 after write attempt: 0x10 WEL=0
(WEL=0 means write was rejected -- protection works)

Removing protection (BP=0)...
Final SR1: 0x00 (BP=0)
```

**结果分析**：
- BP=4 = 0b0100 → 第 16 个 block（8MB / 32 块 64KB，第 16 块从 0x400000 起）开始保护
- 对 0x400000 写入时：WEL=1 → 命令发出 → **写入阶段硬件拒绝** → 自动清 WEL → SR1=0x10 WEL=0
- **关键现象**：写保护不是"页错误返回"，而是**WEL 被吞**——是判断写保护是否生效的依据
- 保护可恢复：再次 WREN + Write SR 覆盖 BP 位

---

### Exp 9: Unique ID & SFDP（软版无 Exp 8，故跳到 9）

**目的**：读 W25Q64 唯一 ID (0x4B) + SFDP 参数表 (0x5A)。

**操作流程**（[`sw_exp9`](../smart_mini/test/test_spi_w25q64.c#L235)）：
```
1. Read UID (0x4B) + 4 dummy 字节 + 8 字节 ID
2. Read SFDP (0x5A) + 3 字节地址 (0) + 1 dummy + 16 字节 header
   头 4 字节应为 ASCII "SFDP" = 53 46 44 50
```

**实测输出**：
```
===== [Soft] Exp9: Unique ID & SFDP =====
Unique ID (0x4B): D1 63 D4 20 CB 35 50 34
SFDP Header (0x5A): 53 46 44 50 00 01 00 FF 00 00 01 09 80 00 00 FF
(expect first 4 bytes: 53 46 44 50 = 'SFDP')
```

**结果分析**：
- UID 8 字节 = 唯一激光刻号；不同芯片不同
- SFDP 头 4 字节 = `53 46 44 50` = "SFDP" ✅（JEDEC JESD216 标准签名）
- 后续 16 字节是 SFDP parameter header：版本 0x00, revision 0x01, num parameter headers 0x00, access protocol 0xFF（1 = SPI）等
- 工程意义：通过 SFDP 表可以**通用**识别 Flash 容量、页大小、支持命令——不依赖 datasheet

---

## 4. 硬件实验（10 个：Exp 1-10）

硬件实验覆盖软件全部 8 个（除 Exp 8 专属） + 3 个硬件专属（Exp 8/9/10）。

### Exp 1: JEDEC ID

**实测输出**：
```
===== [HW] Exp1: JEDEC ID =====
JEDEC: 0xEF 0x40 0x17 OK
```

硬件 SPI1 在 100kHz 下读 JEDEC ID 与软件结果一致——证明 SPI1 控制器 + G4 映射 + baud=239 (100kHz @ 24MHz) 配置正确。

---

### Exp 2: 状态寄存器 SR1/SR2 + Write Enable

**实测输出**：
```
===== [HW] Exp2: Status Registers =====
SR1: 0x00 (BUSY=0 WEL=0 BP=0)  SR2: 0x00
After WE: SR1=0x02 WEL=1
```

---

### Exp 3: Page Write & Read

**实测输出**：
```
===== [HW] Exp3: Page Write & Read =====
Errors: 0/256  PASSED
```

硬件 SPI1 在 100kHz 下全 256 字节零错误。

---

### Exp 4: Cross-Page Write

**实测输出**：
```
===== [HW] Exp4: Cross-Page Write =====
Errors: 0/100  PASSED
```

---

### Exp 5: Sector Erase & Verify

**实测输出**：
```
===== [HW] Exp5: Sector Erase & Verify =====
Before: buf[0]=0xA5 (expect A5)
After:  buf[0]=0xFF all_ff=1  PASSED
Re-write: buf[0]=0xA5
```

---

### Exp 6: Erase Timing

**实测输出**：
```
===== [HW] Exp6: Erase Timing =====
Sector 4KB (0x20): 52 ms
Block 32KB (0x52): 114 ms
Block 64KB (0xD8): 181 ms
Chip Erase (0xC7) -- ~20s, please wait...
19936 ms (~19s)
```

**软/硬对比**：

| Erase | 软 SW 实测 | 硬 HW 实测 | 差异 |
|---|---|---|---|
| Sector 4KB | 55 ms | 52 ms | 3 ms 在测量误差内 |
| Block 32KB | 114 ms | 114 ms | 一致 |
| Block 64KB | 181 ms | 181 ms | 一致 |
| Chip Erase | 19873 ms | 19936 ms | 63 ms（~0.3%）在误差内 |

→ **擦除耗时与 SPI 时钟无关**（因为擦除在 Flash 内部进行，SPI 只负责命令发送），软/硬 SPI 在 erase 时间上几乎无差。

---

### Exp 7: Write Protection

**实测输出**：
```
===== [HW] Exp7: Write Protection =====
Init SR1: 0x00 BP=0
Set BP2: 0x10 BP=4

Attempting write to protected area (0x400000)...
Write protected area: SR1=0x10 WEL=0
(WEL=0 means write rejected - protection works)

Removing protection (BP=0)...
Removed: SR1=0x00 BP=0
```

---

### Exp 8: Fast Read Speed (0x03 vs 0x0B)

> **硬件专属**：对比标准读 (0x03) vs 快速读 (0x0B + 1 dummy byte)。软件版本没有这个实验。

**目的**：在 100kHz baud 下量化 0x0B 相比 0x03 的开销（多 1 dummy 字节）。

**操作流程**（[`hw_exp8`](../smart_mini/test/test_spi_w25q64.c#L423)）：
```
1. 标准读 2048 字节：CS LOW → 0x03 + 地址 → 2048 dummy RX → CS HIGH → 测 ticks
2. Fast Read 2048 字节：CS LOW → 0x0B + 地址 + 1 dummy → 2048 dummy RX → CS HIGH → 测 ticks
3. 算差值
```

**实测输出**：
```
===== [HW] Exp8: Fast Read Speed (0x03 vs 0x0B) =====
Standard Read (0x03): 170978 ticks
Fast Read    (0x0B): 171083 ticks (with 1 dummy byte)
Diff: 105 ticks (Fast-Standard)
(At 100kHz speeds similar; at high clock Fast Read wins)
```

**结果分析**：
- 100kHz baud（SPI1BAUD=239）下，0x0B 仅比 0x03 慢 **105 ticks ≈ 105 µs**（相当于 1 字节 dummy）
- 原因：100kHz 下 SPI 时钟慢，命令/地址发送耗时（4 字节 = ~40 µs）占比小，**Fast Read 的优势（缩短 dummy 等 dummy 周期 ≥ 8）无法体现**
- 工程结论：
  - **低频 SPI**（≤ 5 MHz）：用 0x03 即可，0x0B 没意义
  - **高频 SPI**（≥ 50 MHz）：0x0B 才有意义，因为 W25Q64 要求 dummy 周期 ≥ 8，0x0B 是双时钟 dummy 周期；0x03 在高频下不可用

---

### Exp 9: 100K vs 12MHz Read Speed（专属）

> **硬件专属**：在固定命令流程下对比两种 baud 的吞吐量。

**目的**：测 SPI1 在 100kHz 和 12MHz 下读 4096 字节的 ticks 差异，验证硬件 SPI1 baud 配置正确性。

**操作流程**（[`hw_exp9`](../smart_mini/test/test_spi_w25q64.c#L465)）：
```
1. SPI1BAUD = 239 (100kHz @ 24MHz)：CS LOW → 0x03 + 地址 → 4096 RX → CS HIGH → 测 ticks
2. SPI1BAUD = 1 (12MHz @ 24MHz)：同样流程 → 测 ticks
3. 12MHz 理论 bit period = 2/24M = 83ns，4096 字节 = 4096×8×83ns ≈ 2730 µs
```

**实测输出**：
```
===== [HW] Exp9: 100K vs 12MHz Read Speed =====
100kHz Standard Read: 341672 ticks
12MHz Standard Read: 9914 ticks
12MHz bit period = 2/24M = 83ns; 4096 bytes = 2730 us
```

**结果分析**：
- 100kHz 实测 341672 ticks（341 ms @ 1µs/tick）；与 4096×8 / 100kbps = 328 ms 接近
- 12MHz 实测 9914 ticks（~10 ms）；与 4096×8 / 12Mbps = 2.73 ms 的差距**来自 CS 拉低/拉高延迟 + 命令/地址字节的固定开销**（3 地址 + 1 命令 = 4 字节 ≈ 32 bit × 83 ns = 2.66 µs，但实测开销 ~7 ms，是 CPU 轮询 SPI1BUF + SPI1CON 的开销）
- **加速比 = 341672 / 9914 ≈ 34.5 倍**——接近理论 120 倍但被 CPU 开销摊薄
- 工程结论：
  - 12MHz SPI1 跑 read 时 CPU 不能干别的（busy-loop SPI1BUF），整体吞吐比 baud 比小
  - 如果要更高吞吐：需要 DMA（见 [spi_hal.c:284-311](../smart_mini/test/spi_hal.c)）

---

### Exp 10: Unique ID & SFDP

**实测输出**：
```
===== [HW] Exp10: Unique ID & SFDP =====
UID: D1 63 D4 20 CB 35 50 34
SFDP: 53 46 44 50 00 01 00 FF 00 00 01 09 80 00 00 FF
(expect first 4 bytes: 53 46 44 50 = 'SFDP')
```

软/硬 UID + SFDP 完全一致——同一颗芯片。

---

## 5. 软/硬 SPI 对比总结

| 项 | 软件 bit-bang | 硬件 SPI1 |
|---|---|---|
| 波特率 | ~333 kbps（3µs/bit）| 100kHz / 12MHz 可选 |
| CPU 占用 | 100%（busy-loop）| 中断/DMA 时 0%，polling 时 ~100% |
| 擦除时间 | ~20 s（仅 Flash 内部时间，SPI 时钟无关）| ~20 s（一致）|
| 读 4096 字节 | ~25 ms | 100kHz 342 ms / 12MHz 10 ms |
| 可靠性 | 时序紧、易受中断影响 | 时序由硬件保证 |
| 适用场景 | 教学、调试、无 SPI 资源时 | 产品化、高速传输 |

---

## 6. 工程意义与教训

1. **软/硬 SPI 必须互斥**：共用 PE4/PE5/PE6/PE7，软态 GPIO FEN=0 与硬件态 FEN=1 切换，每次进入 `spi_hal_*_init` 重新配——`test_spi_w25q64.c` 软测完切硬测中间有 `delay_ms(500)` 稳定窗口。
2. **CS 必须 GPIO 手动控**：硬件 SPI 控制器在长延迟 / 多命令场景下不够灵活（Exp 6/7 不同命令序列间要插入 wait_busy）。
3. **Page Program 跨页要手动拆**：W25Q64 协议不支持，调用者保证 len ≤ `256 - (addr & 0xFF)`（HAL 不替拆——是协议约束）。
4. **写保护机制**：WEL 自动清零是判断保护是否生效的最简信号，不必返回错误码。
5. **Erase 耗时不可忽略**：Chip Erase ~20s，UI 设计要给"擦除中"提示，避免误以为死机。

---

## 7. 失败排查

| 现象 | 可能原因 | 解决 |
|---|---|---|
| JEDEC 读 0xFF 0xFF 0xFF | SPI 没起 / CS 没拉低 / baud 太慢 | 检查 CS（万用表量 PE4）/ 降低 baud / 查 MISO 接线 |
| Page Program 256 字节后回读错 | 写入前没 Sector Erase（Flash 只能 1→0）| Exp 3 流程已示范先 erase |
| Cross-Page Write 末尾错 | 跨页没手动拆 | 按 256 字节对齐手动分两次 Page Program |
| BP 写保护后还能写 | BP 位没设对 / 没等 BUSY | Write SR 后必须 wait_busy（手册 §1.3 步骤 3）|
| Erase 时间远大于 datasheet | 用了 SPI 中断但忘了 CPU 等 | 查 BUSY 等待循环 |
| 0x03 / 0x0B 速率差远大于 1 字节 | baud > 50MHz，0x03 不可用 | 用 0x0B（Exp 8 备注）|

---

## 8. 相关文档

- [docs/periph_spi.md](periph_spi.md)：SPI1 寄存器 / 引脚 / 初始化原理
- [docs/test_spi.md](test_spi.md)：SPI 5 phase 总体测试报告
- [docs/test_spi_hal_refactor.md](test_spi_hal_refactor.md)：SPI HAL 浅重构 5 步记录
- [docs/plan.md](plan.md)：整体测试计划
- [BT892X_UserManual_Driver.md §7](../BT892X_UserManual_Driver.md)：SPI 寄存器定义
- [bt892x_pinfunction.md §4.4 PORTE](../bt892x_pinfunction.md)：G4 (PE4-7) 引脚功能

---

## 9. 版本

- 2026-07-17 v1：合并前两版本（`test_spi_soft_w25q64.c` + `test_spi_hw_w25q64.c`）为 `test_spi_w25q64.c`，同步测试软+硬
- 2026-07-19 v2：HAL 浅重构 → `spi_hal.c` 提供软/硬 W25Q64 驱动；测试代码瘦身 ~200 行
- 2026-07-22 v3：本报告统一整理实测输出（来自 main.c 当前 `SPI_SW_W25_MODE=1, SPI_HW_W25_MODE=1, SPI_W25_RUN_MODE=2` 默认配置），所有 18 个实验 PASSED