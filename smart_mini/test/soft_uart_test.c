/**
 * @file    soft_uart_test.c
 * @brief   BT892X 软件 GPIO 模拟串口 (Bit-Bang UART)
 *
 *   PE4 → 软件 TX (GPIO 输出)
 *   PE5 → 软件 RX (GPIO 输入)
 *   跳线 PE4↔PE5 回环测试 256 字节
 *
 *   关键设计: 收发同时进行 (TX设电平 → 等半bit → RX采样)
 *   波特率 9600, 8N1, bit=104µs @ 1MHz timer tick
 */

#include "test.h"

#define BIT_TIME_US     104     // 1/9600 ≈ 104.17µs (取整)
#define HALF_BIT_US     52      // 半位周期, 中心采样

#define TX_PIN          BIT(4)  // PE4
#define RX_PIN          BIT(5)  // PE5

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
    GPIOECLR = TX_PIN;              // TX 拉低
    delay_us(HALF_BIT_US);          // 到中心
    // RX 应看到 LOW, 不做数据采样
    delay_us(HALF_BIT_US);          // 完成后半 bit

    // --- 8 个数据位 (LSB first) ---
    for (int i = 0; i < 8; i++) {
        // TX: 设数据位电平
        if (tx_byte & (1 << i))
            GPIOESET = TX_PIN;
        else
            GPIOECLR = TX_PIN;

        delay_us(HALF_BIT_US);      // 到中心点

        // RX: 在中心采样
        if (GPIOE & RX_PIN)
            rx_byte |= (1 << i);

        delay_us(HALF_BIT_US);      // 完成后半 bit
    }

    // --- 停止位 ---
    GPIOESET = TX_PIN;              // TX 拉高
    delay_us(BIT_TIME_US);          // 1 个完整 bit

    return rx_byte;
}

/* ================================================================
 *  初始化: PE4 输出(TX, 空闲高), PE5 输入(RX, 上拉)
 * ================================================================ */
static void soft_uart_init(void)
{
    GPIOEFEN &= ~TX_PIN;
    GPIOEDE  |=  TX_PIN;
    GPIOEDIR &= ~TX_PIN;
    GPIOESET  =  TX_PIN;            // 空闲高

    GPIOEFEN &= ~RX_PIN;
    GPIOEDE  |=  RX_PIN;
    GPIOEDIR |=  RX_PIN;
    GPIOEPU  |=  RX_PIN;            // 上拉
}

/* ================================================================
 *  主测试: 收发一体回环 0x00~0xFF
 * ================================================================ */
void soft_uart_test(void)
{
    printf("\n===== BT892X Software UART (9600bps) =====\n\n");

    soft_uart_init();

    printf("TX: PE4 (GPIO out)  RX: PE5 (GPIO in)\n");
    printf("Baud: 9600, 8N1, bit=%dus\n", BIT_TIME_US);
    printf("Mode: TX+RX simultaneous (loopback)\n");
    printf("Jumper: PE4 <-> PE5\n\n");

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
    printf("\nSending test pattern on PE4 for logic analyzer...\n");
    while (1) {
        soft_uart_txrx(0x55);   // 交替 01010101
        delay_ms(1);
    }
}
