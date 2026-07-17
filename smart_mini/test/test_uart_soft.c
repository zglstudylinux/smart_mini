/**
 * @file    test_uart_soft.c
 * @brief   BT892X 软件 GPIO 模拟串口 (Bit-Bang UART) —— 自 smart_mini_copilot 移植
 *
 *   PB2 → 软件 TX (GPIO 输出)   ← 与硬件 UART2 TX 同脚
 *   PB1 → 软件 RX (GPIO 输入)   ← 与硬件 UART2 RX 同脚
 *   跳线 PB2↔PB1 回环测试 256 字节
 *
 *   原 copilot 版本用 PE4/PE5，本移植改为 PB2/PB1 与硬件 UART 保持一致：
 *   软/硬 UART 共用同一对引脚和同一根跳线，不同时启用即无冲突。
 *
 *   关键设计: 收发同时进行 (TX设电平 → 等半bit → RX采样)
 *   波特率 9600, 8N1, bit=104µs @ tmr_inc=1MHz (delay_us 基准)
 *
 * 参考手册: BT892X_UserManual_Driver.md §3.2 GPIO（纯 GPIO bit-bang，不用 UART 控制器）
 * 引脚手册: bt892x_pinfunction.md §4.2 PORTB
 */

#include "test_common.h"

#define BIT_TIME_US     104     // 1/9600 ≈ 104.17µs (取整)
#define HALF_BIT_US     52      // 半位周期, 中心采样

#define TX_PIN          BIT(2)  // PB2
#define RX_PIN          BIT(1)  // PB1

/* ================================================================
 *  软件 UART 同时收发一个字节 (TX+RX 同步, 用于回环测试)
 *
 *  帧格式: 起始位(LOW) + D0~D7(LSB) + 停止位(HIGH)
 *  每 bit: TX 设电平 → 等半bit → RX 中心采样 → 等半bit
 * ================================================================ */
static u8 soft_uart_txrx(u8 tx_byte)
{
    u8 rx_byte = 0;

    // --- 起始位 ---
    GPIOBCLR = TX_PIN;              // TX 拉低 (§3.2 写 CLR=1 输出低)
    delay_us(HALF_BIT_US);          // 到中心
    // RX 应看到 LOW, 不做数据采样
    delay_us(HALF_BIT_US);          // 完成后半 bit

    // --- 8 个数据位 (LSB first) ---
    for (int i = 0; i < 8; i++) {
        // TX: 设数据位电平
        if (tx_byte & (1 << i))
            GPIOBSET = TX_PIN;      // §3.2 写 SET=1 输出高
        else
            GPIOBCLR = TX_PIN;      // §3.2 写 CLR=1 输出低

        delay_us(HALF_BIT_US);      // 到中心点

        // RX: 在中心采样 (§3.2 读 GPIOB 数据寄存器)
        if (GPIOB & RX_PIN)
            rx_byte |= (1 << i);

        delay_us(HALF_BIT_US);      // 完成后半 bit
    }

    // --- 停止位 ---
    GPIOBSET = TX_PIN;              // TX 拉高
    delay_us(BIT_TIME_US);          // 1 个完整 bit

    return rx_byte;
}

/* ================================================================
 *  初始化: PB2 输出(TX, 空闲高), PB1 输入(RX, 上拉)
 * ================================================================ */
static void soft_uart_init(void)
{
    GPIOBFEN &= ~TX_PIN;            // §3.2 FEN=0 用作 GPIO
    GPIOBDE  |=  TX_PIN;            // §3.2 DE=1 数字 IO
    GPIOBDIR &= ~TX_PIN;            // §3.2 DIR=0 输出
    GPIOBSET  =  TX_PIN;            // 空闲高

    GPIOBFEN &= ~RX_PIN;           // §3.2 FEN=0 用作 GPIO
    GPIOBDE  |=  RX_PIN;           // §3.2 DE=1 数字 IO
    GPIOBDIR |=  RX_PIN;           // §3.2 DIR=1 输入
    GPIOBPU  |=  RX_PIN;           // §3.2 上拉
}

/* ================================================================
 *  主测试: 收发一体回环 0x00~0xFF
 * ================================================================ */
void test_uart_soft_run(void)
{
    printf("\n===== BT892X Software UART (9600bps) =====\n\n");

    soft_uart_init();

    printf("TX: PB2 (GPIO out)  RX: PB1 (GPIO in)\n");
    printf("Baud: 9600, 8N1, bit=%dus\n", BIT_TIME_US);
    printf("Mode: TX+RX simultaneous (loopback)\n");
    printf("Jumper: PB2 <-> PB1\n\n");

    int errors = 0, total = 0;
    for (int val = 0; val <= 0xFF; val++) {
        u8 rx = soft_uart_txrx((u8)val);    // 同时收发
        total++;
        if (rx != (u8)val) {
            errors++;
            if (errors <= 5)
                printf("ERR: sent=0x%02X, rx=0x%02X\n", val, rx);
        }
    }

    printf("\n===== Result =====\n");
    printf("Total: %d, Errors: %d\n", total, errors);
    printf(errors == 0 ? "ALL PASSED!\n" : "FAILED\n");

    // 再用逻辑分析仪看一下波形
    printf("\nSending test pattern on PB2 for logic analyzer...\n");
    while (1) {
        soft_uart_txrx(0x55);   // 交替 01010101
        delay_ms(1);
    }
}
