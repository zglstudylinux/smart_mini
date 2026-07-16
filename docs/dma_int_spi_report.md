# SPI1 DMA + 中断模式测试报告

> **日期**: 2026-07-16  
> **测试结果**: ✅ Mode 3/4/5 全部通过

---

## SPI 五种模式总览

| # | 模式 | 文件 | 结果 |
|:--:|------|------|:--:|
| 1 | GPIO 模拟 SPI 轮询 | `w25q64_test.c` | ✅ |
| 2 | 硬件 SPI 轮询 | `hw_spi_w25q64.c` | ✅ |
| 3 | **硬件 SPI 中断** | `spi_int_test.c` | ✅ |
| 4 | 硬件 SPI DMA 轮询 | `dma_spi_test.c` | ✅ |
| 5 | **硬件 SPI DMA + 中断** | `spi_int_test.c` | ✅ |

---

## Mode 3: 硬件 SPI 中断

**原理**: 开启 SPIIE，注册 ISR 到 IRQ_SPI_VECTOR(20)，每次 SPI 传输完成触发中断。

**结果**:
- JEDEC ID: `0xEF 0x40 0x17` OK
- 512 字节读取: 52479 ticks @ 100KHz

---

## Mode 4: 硬件 SPI DMA (轮询)

**原理**: SPI1DMAADR 指向缓冲区，写 SPI1DMACNT 启动 DMA，轮询 SPIPND。

**结果** (12MHz):
- 4096 字节: 2534 ticks (比轮询 10456 ticks 快 75%)

---

## Mode 5: 硬件 SPI DMA + 中断

**原理**: DMA 传输完成后触发 SPIPND → ISR 清除标志并通知主循环。

**结果**:
- 4096 字节 DMA 读取: 302501 ticks @ 100KHz
- 数据校验: OK

---

## 汇编优化测试

**结论**: 内联汇编反而比 C 慢 6%（额外的寄存器加载 + volatile 屏障）。编译器已足够优化简单 GPIO。展开循环仅带来 1% 提升。

| 版本 | 10000 字节耗时 | 对比 |
|------|------:|------|
| C 版本 | 611,956 | 基线 |
| ASM 循环 | 650,336 | 慢 6% |
| ASM 展开 | 607,778 | 持平 |

---

## 踩坑记录

| 问题 | 原因 | 修复 |
|------|------|------|
| JEDEC ID 少一字节 | 命令 0x9F 的 `int_spi_byte()` 也回读一字节 | 读 4 字节（cmd+3 data） |
| DMA 0 ticks | `spi_done` 被前次 ISR 残留 | DMA 启动前一刻清零 |
| 内联 ASM 更慢 | `volatile` 屏障 + 寄存器加载开销 | 信任编译器 |
