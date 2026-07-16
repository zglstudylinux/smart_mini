# W25Q64 Flash 全套实验报告

> **日期**: 2026-07-16  
> **芯片**: BT892X + W25Q64JV (8MB SPI NOR Flash)  
> **通信方式**: 软件 GPIO 模拟 SPI Mode 0  
> **引脚**: PE4=CS, PE5=CLK, PE6=MOSI, PE7=MISO  
> **测试结果**: ✅ 全部通过 (Exp1~7, Exp9)

---

## 硬件接线

| BT892X (PORTE) | W25Q64 模块 | 功能 |
|:---:|:---:|------|
| PE4 | CS | 片选 (低有效) |
| PE5 | CLK | SPI 时钟 |
| PE6 | MOSI | 主机输出 → DI |
| PE7 | MISO | 主机输入 ← DO |
| 3.3V | VCC, HOLD#, WP# | 电源 |
| GND | GND | 地 |

---

## 底层 API

```c
void w25q64_init(void)           // 初始化 GPIO
u8   soft_spi_byte(u8 tx)        // 软件 SPI 全双工收发 1 字节
void w25q64_cs_low/high(void)    // 片选控制
void w25q64_write_enable(void)   // 发送 0x06
u8   w25q64_read_status(u8 cmd)  // 读 SR1(0x05) / SR2(0x35)
void w25q64_wait_busy(void)      // 轮询 BUSY 位
void w25q64_read_data(addr,buf,len)      // 读数据 (0x03)
void w25q64_page_program(addr,buf,len)   // 页编程 (0x02)
void w25q64_sector_erase(addr)   // 扇区擦除 (0x20)
```

---

## 实验 1: 读 JEDEC ID — ✅

**原理**: 发送 `0x9F` 命令，Flash 返回 3 字节制造商/型号/容量。

**结果**: `JEDEC ID: 0xEF 0x40 0x17` — Winbond W25Q64JV 64Mbit ✓

---

## 实验 2: 读 Status Register — ✅

**原理**: `0x05` 读 SR1 (BUSY/WEL/BP), `0x35` 读 SR2。

**结果**: SR1=0x02 (WEL=1), SR2=0x00。发 Write Enable 后 WEL 保持 ✓

---

## 实验 3: 页写入与读取 — ✅

**原理**: Write Enable `0x06` → Page Program `0x02` (最多 256 字节) → 等 BUSY → Read `0x03`。

**结果**: 256 字节写入 Page 0 后读回，Errors: 0/256 ✓

---

## 实验 4: 跨页连续写入 — ✅

**原理**: Page Program 不能跨 256 字节边界，需软件分页。

**结果**: 从 0xF0 写 100 字节 (跨 Page 0→1)，Errors: 0/100 ✓

---

## 实验 5: 扇区擦除与验证 — ✅

**原理**: `0x20` Sector Erase 将 4KB 擦除为全 `0xFF`。

**结果**: 擦除后全 `0xFF`，重新写入校验正确 ✓

---

## 实验 6: 擦除耗时对比 — ✅

| 擦除类型 | 命令 | 大小 | 实测耗时 |
|----------|:---:|------|----------|
| Sector Erase | 0x20 | 4KB | **47ms** |
| Block Erase 32KB | 0x52 | 32KB | **101ms** |
| Block Erase 64KB | 0xD8 | 64KB | **162ms** |
| Chip Erase | 0xC7/0x60 | 8MB | **~18s** |

---

## 实验 7: 写保护配置 — ✅

**原理**: 修改 SR1 的 BP 位实现硬件写保护。`0x01` 写 Status Register。

**结果**: BP2=1 后向保护区写入被拒绝 (WEL=0)，解除保护后恢复正常 ✓

---

## 实验 9: Unique ID + SFDP — ✅

**原理**: `0x4B` 读 64-bit 唯一 ID，`0x5A` 读 SFDP 参数表。

**结果**: 
- Unique ID: `D1 63 D4 20 CB 35 50 34`
- SFDP Signature: `53 46 44 50` = "SFDP" ✓

---

## 总结

| 实验 | 内容 | 结果 |
|:--:|------|:--:|
| 1 | JEDEC ID | ✅ 0xEF 0x40 0x17 |
| 2 | Status Register | ✅ 位操作正常 |
| 3 | 页写入+读取 | ✅ 256/256 |
| 4 | 跨页写入 | ✅ 100/100 |
| 5 | 扇区擦除+验证 | ✅ 全 0xFF |
| 6 | 擦除耗时 | ✅ 47/101/162/18466ms |
| 7 | 写保护 | ✅ BP 生效/解除 |
| 9 | Unique ID+SFDP | ✅ |

> 实验 8 (Fast Read / Dual / Quad SPI) 留待硬件 SPI 阶段实现。
