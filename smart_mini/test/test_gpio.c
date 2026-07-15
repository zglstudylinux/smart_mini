// GPIO 测试 — PE4 输出 1Hz 方波
// 手册：BT892X_UserManual_Driver.md §3.2 GPIO 寄存器说明
// SFR：见 header/sfr.h 第 421-462 行（Group6 GPIO A/B/E/F）
//
// 引脚选择理由：
//   - PA3/PA4 留给后续 UART1 G2 测试
//   - PB3/PB4 是 USB 下载口，禁止触碰
//   - PG1~PG5 是外挂 SPI-Flash，禁止触碰
//   - PE4 完全空闲，是最简单的测试起点
//
// 验证方法：
//   1. 万用表打到直流电压档，红表笔接 PE4，黑表笔接 GND
//   2. 观察电压应在 0V 和 ~3.3V 之间切换，周期约 1 秒
//   3. 串口（PB3）应同时看到 PE4=1 / PE4=0 的打印

#include "test_common.h"

#define TEST_PIN_PORT   E
#define TEST_PIN_NUM    4
#define TEST_PIN_MASK   BIT(TEST_PIN_NUM)

void test_gpio_run(void)
{
    // 1. 配置 PE4 为普通 GPIO 输出
    //    GPIOEDE  bit 4 = 1   → 数字 IO 使能
    //    GPIOEFEN bit 4 = 0   → 关闭外设功能映射
    //    GPIOEDIR bit 4 = 0   → 输出方向（0=输出）
    TEST_GPIO_OUT(TEST_PIN_PORT, TEST_PIN_NUM);

    TEST_LOG("GPIO test start: PE4 toggle @ 1Hz");
    TEST_LOG("Use multimeter on PE4, expect ~1s square wave (0V / 3.3V)");

    // 2. 初始为低
    TEST_GPIO_LOW(TEST_PIN_PORT, TEST_PIN_NUM);

    // 3. 无限翻转循环
    while (1) {
        TEST_GPIO_HIGH(TEST_PIN_PORT, TEST_PIN_NUM);
        TEST_LOG("PE4 = 1 (HIGH ~3.3V)");
        delay_ms(500);

        TEST_GPIO_LOW(TEST_PIN_PORT, TEST_PIN_NUM);
        TEST_LOG("PE4 = 0 (LOW 0V)");
        delay_ms(500);
    }
}