#ifndef _UART_HAL_H_
#define _UART_HAL_H_

// =====================================================================
//  UART HAL —— 抽象 BT892X UART 测试原语（软硬共用 + Console 层）
//
//  设计原则（用户决策 2026-07-17）：
//    * 彻底重构：原语 + Console/printf 抽象层
//    * 测试代码区分 soft/hw（template-style），不强行统一
//    * 仅抽象当前 BT892X UART2，未来换芯片再独立处理
//
//  引脚约定（软/硬共用 PB1/PB2）：
//    PB2 = TX (软做 GPIO 输出，硬件由 UART2 TX 接管)
//    PB1 = RX (软做 GPIO 输入，硬件由 UART2 RX 接管)
//
//  注意事项：
//    * 硬件 UART2 读 UART2DATA **不自动清 RXPND** —— 必须显式
//      `UART2CPND = BIT(9)` 清挂起，否则下次读会拿到同一字节
//      （这是 2026-07-17 测试发现的关键 bug）
//    * 软件 bit-bang 必须 FEN=0（外设功能关闭）才能用 GPIO
// =====================================================================

#include "test_common.h"   // 拿到 GPIO 宏、delay_us、tick_get 等

// ===================== 引脚宏 =====================
#define UART_TX_PIN    BIT(2)   // PB2
#define UART_RX_PIN    BIT(1)   // PB1

// ===================== 软件 bit-bang 原语 =====================
// 初始化：TX 输出空闲高，RX 输入；FEN=0（强制 GPIO 模式）
//   baud 参数化（默认 9600），bit 周期 = 1e6 / baud 微秒
void uart_hal_soft_init(u32 baud);
// 发送一个字节（9600 8N1，不采样 RX）
void uart_hal_soft_putc(u8 tx);
// 接收一个字节（先等 start bit 下降沿，8 bit 中心采样）
u8   uart_hal_soft_getc(void);
// 同步发送+接收一个字节（loopback 专用：边发边采样，TX 与 RX 同时进行）
// 不用此函数做 loopback 会出现"putc 完后线停在 HIGH，getc 等不到 start bit"死锁
u8   uart_hal_soft_txrx(u8 tx);

// ===================== 硬件 UART2 原语 =====================
// 初始化：FUNCMCON1 G2 映射 + PB1/PB2 GPIO + UART2BAUD + UART2CON
//   baud 参数化（默认 115200）
void uart_hal_hw_init(u32 baud);
// 发送一个字节（阻塞等 TXPND 翻转）
void uart_hal_hw_putc(u8 tx);
// 接收一个字节（阻塞等 RXPND=1 后显式清挂起）
u8   uart_hal_hw_getc(void);

// ===================== Console / printf 重定向层 =====================
// 初始化：调 uart_hal_hw_init + my_printf_init 注册 console_putchar
void uart_hal_console_init(u32 baud);
// my_printf 回调：把一个字符经硬件 UART2 发出
void uart_hal_console_putchar(char ch);
// 一体化接收：调 uart_hal_hw_getc
u8   uart_hal_console_getc(void);

#endif // _UART_HAL_H_
