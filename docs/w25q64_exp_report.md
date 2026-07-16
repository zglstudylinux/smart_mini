# W25Q64 Flash 完整实验报告 (软件 SPI + 硬件 SPI1)

> **日期**: 2026-07-16  
> **芯片**: BT892X + W25Q64JV (8MB SPI NOR Flash)  
> **测试结果**: ✅ Exp1~10 全部通过

---

## 硬件接线

### 软件 SPI (Exp1~9)

| BT892X | W25Q64 | 功能 |
|:---:|:---:|------|
| PE4 | CS | GPIO |
| PE5 | CLK | GPIO |
| PE6 | MOSI | GPIO |
| PE7 | MISO | GPIO |

### 硬件 SPI1 G4 (HW Exp1~10)

| BT892X | W25Q64 | 功能 |
|:---:|:---:|------|
| PE4 | CS | GPIO |
| PE6 | CLK | SPI1 CLK |
| PE7 | MOSI | SPI1 MOSI |
| PE5 | MISO | SPI1 MISO |

---

## 底层 API

```c
hw_w25q64_init()                // SPI1 G4 + GPIO 初始化, 100KHz Mode 0
hw_spi_byte(tx) → rx            // 硬件 SPI1 全双工收发一字节
hw_write_enable()               // 0x06
hw_read_status(cmd) → u8        // 0x05 / 0x35
hw_wait_busy()                  // 轮询 SR1 BUSY 位
hw_read_data(addr, buf, len)    // 0x03 读数据
hw_page_program(addr, buf, len) // 0x02 页编程 (≤256B)
hw_sector_erase(addr)           // 0x20 扇区擦除 (4KB)
```

---

## 实验结果汇总

### Exp1: JEDEC ID

| 方式 | 结果 |
|------|------|
| 软件 SPI | `0xEF 0x40 0x17` ✅ |
| 硬件 SPI | `0xEF 0x40 0x17` ✅ |

### Exp2: Status Register

| 方式 | SR1 初始 | SR1 WE 后 | SR2 |
|------|----------|----------|------|
| 软件 SPI | `0x02` WEL=1 | `0x02` | `0x00` |
| 硬件 SPI | `0x00` WEL=0 | `0x02` ✅ | `0x00` |

### Exp3: 页写入与读取

| 方式 | 结果 |
|------|------|
| 软件 SPI | 0/256 errors ✅ |
| 硬件 SPI | 0/256 errors ✅ |

**流程**: 擦除 Sector 0 → 写 0~255 → 读回校验

### Exp4: 跨页连续写入

| 方式 | 地址 | 长度 | 结果 |
|------|------|------|------|
| 软件 SPI | 0xF0 | 100B | 0/100 ✅ |
| 硬件 SPI | 0xF0 | 100B | 0/100 ✅ |

**关键**: 页边界 256B，从 0xF0 写 100 字节 = Page0 剩 16B + Page1 前 84B

### Exp5: 扇区擦除与验证

| 方式 | 擦除前 | 擦除后 | 重新写入 |
|------|--------|--------|----------|
| 软件 SPI | 0xA5 ✅ | 全 0xFF ✅ | 0xA5 ✅ |
| 硬件 SPI | 0xA5 ✅ | 全 0xFF ✅ | 0xA5 ✅ |

### Exp6: 擦除耗时对比

| 擦除类型 | 命令 | 大小 | 软件 SPI | 硬件 SPI |
|----------|:---:|------|----------|----------|
| Sector | `0x20` | 4KB | 47ms | 48ms |
| Block 32KB | `0x52` | 32KB | 101ms | 105ms |
| Block 64KB | `0xD8` | 64KB | 162ms | 168ms |
| Chip | `0xC7` | 8MB | ~18s | ~18s |

> 软硬件耗时一致，瓶颈是 Flash 内部擦除时间，非 SPI 传输速度。

### Exp7: 写保护配置

| 方式 | 初始 BP | 设置 BP2 | 保护区写入 | 解除保护 |
|------|---------|----------|------------|----------|
| 软件 SPI | 0 | BP=4 ✅ | WEL=0 拒绝 ✅ | BP=0 ✅ |
| 硬件 SPI | 0 | BP=4 ✅ | WEL=0 拒绝 ✅ | BP=0 ✅ |

### Exp8: Fast Read vs Standard Read

| 读模式 | 命令 | 100KHz 耗时 | 数据一致性 |
|--------|:---:|------|:---:|
| Standard | `0x03` | 170983 ticks | ✅ |
| Fast | `0x0B` | 171053 ticks | ✅ OK |

> 100KHz 下 Standard 略快（Fast 多 1 字节 dummy）。

### Exp9: 高低速反超演示 ⭐

| 时钟 | Standard (0x03) | Fast (0x0B) | 结果 |
|------|------:|------:|------|
| 100KHz | 341,651 | 341,764 | Standard 快 (+113) |
| **12MHz** | 10,195 | **8,058** | **Fast 快 2137 ticks (20.9%)!** |

> **结论**: 低速下 dummy byte 是负担；高速下 Fast Read 反超。W25Q64 Fast Read 支持最高 133MHz，生产代码中始终使用 Fast Read。

### Exp10: Unique ID + SFDP

| 方式 | Unique ID | SFDP |
|------|-----------|:---:|
| 软件 SPI | `D1 63 D4 20 CB 35 50 34` | `53 46 44 50` ✅ |
| 硬件 SPI | `D1 63 D4 20 CB 35 50 34` | `53 46 44 50` ✅ |

---

## 软件 SPI vs 硬件 SPI 对比

| 维度 | 软件 SPI | 硬件 SPI1 |
|------|----------|-----------|
| CPU 占用 | 高（每 bit 需翻转 GPIO） | 低（硬件移位） |
| 最高速度 | ~500KHz（受 delay_us 限制） | 12MHz（BAUD=1） |
| 引脚 | 任意 GPIO | 固定映射组 |
| 代码量 | 多 | 少 |

---

## 总结

| Exp | 内容 | 软 SPI | 硬 SPI |
|:--:|------|:--:|:--:|
| 1 | JEDEC ID | ✅ | ✅ |
| 2 | Status Register | ✅ | ✅ |
| 3 | 页写入+读取 | ✅ | ✅ |
| 4 | 跨页写入 | ✅ | ✅ |
| 5 | 扇区擦除+验证 | ✅ | ✅ |
| 6 | 擦除耗时 | ✅ | ✅ |
| 7 | 写保护 | ✅ | ✅ |
| 8 | Fast Read 对比 | — | ✅ |
| 9 | 高低速反超演示 | — | ✅ 12MHz Fast 快 21% |
| 10 | Unique ID+SFDP | ✅ | ✅ |
