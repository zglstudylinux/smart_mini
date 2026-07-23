#ifndef _SPI_HAL_H_
#define _SPI_HAL_H_

// =====================================================================
//  SPI HAL —— 抽象 SPI 公共原语，供 test_spi_*.c 共享
//
//  设计原则（用户决策 2026-07-17）：
//    * 浅重构：只抽底层原语，不引入运行时多态
//    * 测试代码仍区分 soft / hw 两种 bus（template-style）
//    * 仅抽象 BT892X 当前 SPI1，未来换芯片再独立处理
//
//  引脚约定（与硬件 SPI1 G4 对齐，PE 端口）：
//    PE4 = CS   (软件做 GPIO 输出，硬件由 GPIO 手动控制)
//    PE6 = CLK  (硬件 SPI1 G4 的 CLK)
//    PE7 = MOSI (硬件 SPI1 G4 的 DO/DI)
//    PE5 = MISO (硬件 SPI1 G4 的 DI/DO)
//
//  软件 bit-bang 与硬件 SPI 共用 PE4/PE5/PE6/PE7，只是 bit-bang 角色
//  在软件态会把 CLK/MOSI/DIR 重新分配为通用 GPIO（通过 FEN=0）。
// =====================================================================

#include "test_common.h"   // GPIO 宏、delay_us、tick_get 等

// ===================== 引脚宏（与原 test_spi_common.h 一致） =====================
#define SPI_CS_PIN    BIT(4)   // PE4
#define SPI_CLK_PIN   BIT(6)   // PE6
#define SPI_MOSI_PIN  BIT(7)   // PE7
#define SPI_MISO_PIN  BIT(5)   // PE5

// ===================== 软件 bit-bang 原语 =====================
// 初始化：CS/CLK/MOSI 输出，MISO 输入；CLK 空闲低，CS 空闲高
void spi_hal_soft_init(void);
// 收发一字节（MSB-first, Mode 0），返回 RX
u8   spi_hal_soft_byte(u8 tx);

// ===================== 硬件 SPI1 原语（G4 = PE6/PE7/PE5） =====================
// 初始化：FUNCMCON1 G4 映射 + SPI1BAUD + SPI1CON = SPIEN (Mode 0)
//   baud 100k = 239, 12M = 1
void spi_hal_hw_init(u32 baud);
// 收发一字节（轮询），返回 RX
u8   spi_hal_hw_byte(u8 tx);

// ===================== CS 控制（共享原语，软/硬共用） =====================
// 拉低 CS（选中从设备），含 delay_us(1) 满足时序
void spi_hal_cs_low(void);
// 拉高 CS（释放从设备），含 delay_us(1)
void spi_hal_cs_high(void);

// ===================== W25Q64 Flash 驱动层（按 bus 分两套，软/硬共用 CS/CS_low/high 原语） =====================
// 命令时序参考 Winbond W25Q64 datasheet

/* --- 软件 bit-bang 版（用 spi_hal_soft_byte 收发） --- */
void spi_hal_w25_sw_write_enable(void);                                  /* 0x06 */
u8   spi_hal_w25_sw_read_status(u8 cmd);                               /* 0x05/0x35 */
void spi_hal_w25_sw_wait_busy(void);                                    /* 轮询 SR1[0]==0 */
void spi_hal_w25_sw_read_data(u32 addr, u8 *buf, u32 len);              /* 0x03 + 3 字节地址 */
void spi_hal_w25_sw_page_program(u32 addr, const u8 *buf, u32 len);      /* 0x02 + 地址 + 数据 */
void spi_hal_w25_sw_sector_erase(u32 addr);                             /* 0x20 + 3 字节地址 */

/* --- 硬件 SPI1 版（用 spi_hal_hw_byte 收发） --- */
void spi_hal_w25_hw_write_enable(void);
u8   spi_hal_w25_hw_read_status(u8 cmd);
void spi_hal_w25_hw_wait_busy(void);
void spi_hal_w25_hw_read_data(u32 addr, u8 *buf, u32 len);
void spi_hal_w25_hw_page_program(u32 addr, const u8 *buf, u32 len);
void spi_hal_w25_hw_sector_erase(u32 addr);

/* ===================== 硬件 SPI1：三种传输模式 =====================
 *  Polling 模式已由 spi_hal_hw_byte 提供
 *  Interrupt / DMA 模式额外提供：
 */
/* 中断模式：SPIIE + ISR + 阻塞等 spi_done */
void spi_hal_hw_it_setup(void);                /* 开 SPIIE + register_isr + PICEN */
void spi_hal_hw_it_teardown(void);             /* 关 PICEN + 关 SPIIE */
u8   spi_hal_hw_byte_it(u8 tx);                /* 阻塞式等 ISR 完成 */

/* DMA 模式（手册 §7.2 SPI1DMA*，§7.3 DMA 流程） */
/* 读：CS low + 命令/地址走 polling + RXSEL=1 + 启动 DMA */
void spi_hal_hw_read_dma(u32 addr, u8 *buf, u32 len);
/* 写：CS low + 命令/地址走 polling + RXSEL=0 + 启动 DMA */
void spi_hal_hw_write_dma(u32 addr, const u8 *buf, u32 len);

#endif // _SPI_HAL_H_
