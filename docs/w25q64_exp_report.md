# W25Q64 Flash 完整实验报告 (软件 SPI + 硬件 SPI)

> **日期**: 2026-07-16  
> **芯片**: BT892X + W25Q64JV (8MB SPI NOR Flash)  
> **测试结果**: ✅ Exp1~9 全部通过

---

## 硬件接线

### 软件 SPI 接线 (Exp1~9)

| BT892X (PORTE) | W25Q64 | 功能 |
|:---:|:---:|------|
| PE4 | CS | GPIO 片选 |
| PE5 | CLK | GPIO 时钟 |
| PE6 | MOSI | GPIO 数据输出 |
| PE7 | MISO | GPIO 数据输入 |

### 硬件 SPI1 接线 (HW Exp1~8)

| BT892X | W25Q64 | 功能 |
|:---:|:---:|------|
| PE4 | CS | GPIO 片选 |
| PE6 | CLK | SPI1 时钟 (G4) |
| PE7 | MOSI | SPI1 MOSI (G4) |
| PE5 | MISO | SPI1 MISO (G4) |

---

## 底层 API (硬件 SPI 版本)

```c
hw_w25q64_init()                // SPI1 G4 映射 + GPIO 初始化
hw_spi_byte(tx) → rx            // 硬件 SPI1 全双工收发一字节
hw_cs_low() / hw_cs_high()      // CS 控制
hw_write_enable()               // 0x06
hw_read_status(cmd) → u8        // 0x05 / 0x35
hw_wait_busy()                  // 轮询 SR1 BUSY 位
hw_read_data(addr, buf, len)    // 0x03 读数据
hw_page_program(addr, buf, len) // 0x02 页编程
hw_sector_erase(addr)           // 0x20 扇区擦除
```

---

## 实验 1: JEDEC ID

| 方式 | 结果 |
|------|------|
| 软件 SPI | `0xEF 0x40 0x17` ✅ |
| 硬件 SPI | `0xEF 0x40 0x17` ✅ |

---

## 实验 2: Status Register

| 方式 | SR1 初始 | SR1 写使能后 | SR2 |
|------|----------|-------------|------|
| 软件 SPI | `0x02` WEL=1 | `0x02` | `0x00` |
| 硬件 SPI | `0x00` WEL=0 | `0x02` ✅ | `0x00` |

---

## 实验 3: 页写入与读取

| 方式 | 结果 |
|------|------|
| 软件 SPI | 0 errors / 256 ✅ |
| 硬件 SPI | 0 errors / 256 ✅ |

---

## 实验 4: 跨页连续写入

| 方式 | 地址 | 长度 | 结果 |
|------|------|------|------|
| 软件 SPI | 0xF0 | 100B | 0/100 ✅ |
| 硬件 SPI | 0xF0 | 100B | 0/100 ✅ |

---

## 实验 5: 扇区擦除与验证

| 方式 | 擦除前 | 擦除后 | 重新写入 |
|------|--------|--------|----------|
| 软件 SPI | 0xA5 ✅ | 全 0xFF ✅ | 0xA5 ✅ |
| 硬件 SPI | 0xA5 ✅ | 全 0xFF ✅ | 0xA5 ✅ |

---

## 实验 6: 擦除耗时对比

| 擦除类型 | 命令 | 大小 | 软件 SPI | 硬件 SPI |
|----------|:---:|------|----------|----------|
| Sector | 0x20 | 4KB | 47ms | 48ms |
| Block 32KB | 0x52 | 32KB | 101ms | 105ms |
| Block 64KB | 0xD8 | 64KB | 162ms | 168ms |
| Chip | 0xC7 | 8MB | ~18s | ~18s |

> 软硬件方式耗时基本一致（误差在测量精度内），因为瓶颈是 Flash 内部擦除时间。

---

## 实验 7: 写保护

| 方式 | 初始 BP | 设置 BP2 | 保护区写入 | 解除保护 |
|------|---------|----------|------------|----------|
| 软件 SPI | 0 | BP=4 ✅ | WEL=0 (拒绝) ✅ | BP=0 ✅ |
| 硬件 SPI | 0 | BP=4 ✅ | WEL=0 (拒绝) ✅ | BP=0 ✅ |

---

## 实验 8: Fast Read 速度对比 (仅硬件 SPI, 100KHz, 2048 字节)

| 读模式 | 命令 | 耗时 | 数据一致性 |
|--------|:---:|------|:---:|
| Standard Read | 0x03 | 170983 ticks | ✅ |
| Fast Read | 0x0B | 171053 ticks (+70) | ✅ OK |

> 100KHz 下 Standard 和 Fast 速度接近（Fast 多 1 字节 dummy）。更高时钟下 Fast Read 优势明显。Dual/Quad 留待高级 SPI 阶段。

---

## 实验 9: Unique ID + SFDP

| 方式 | Unique ID | SFDP Signature |
|------|-----------|:---:|
| 软件 SPI | `D1 63 D4 20 CB 35 50 34` | `53 46 44 50` ✅ |
| 硬件 SPI | `D1 63 D4 20 CB 35 50 34` | `53 46 44 50` ✅ |

---

## 软件 SPI vs 硬件 SPI 对比

| 维度 | 软件 SPI | 硬件 SPI1 |
|------|----------|-----------|
| CPU 占用 | 高（每个 bit 都需要 CPU 翻转） | 低（硬件自动移位） |
| 速度 | 受 delay_us() 限制 | 最高 12MHz（SPI1 模块） |
| 引脚灵活性 | 任意 GPIO | 固定映射组 |
| 代码量 | 多（手动时序） | 少（寄存器操作） |
| 适用场景 | 调试/无硬件 SPI 时 | 正式项目 |

---

## 总结

| 实验 | 内容 | 软 SPI | 硬 SPI |
|:--:|------|:--:|:--:|
| 1 | JEDEC ID | ✅ | ✅ |
| 2 | Status Register | ✅ | ✅ |
| 3 | 页写入+读取 | ✅ | ✅ |
| 4 | 跨页写入 | ✅ | ✅ |
| 5 | 扇区擦除+验证 | ✅ | ✅ |
| 6 | 擦除耗时 | ✅ | ✅ |
| 7 | 写保护 | ✅ | ✅ |
| 8 | Fast Read 对比 | — | ✅ |
| 9 | Unique ID+SFDP | ✅ | ✅ |
