// GPIO bit-bang I2C timing test (Logic Analyzer) —— HAL 重构版
//
// HAL 抽象在 [i2c_hal.c](i2c_hal.c)，本文件仅做 LA 用的 burst + 初始状态打印。
//
// 接线：CH1 -> PE6 (SCL), CH2 -> PE7 (SDA), GND -> GND
//       AT24C02 可断开（bit-bang 照常驱动 SCL）

#include "test_common.h"
#include "i2c_hal.h"

#define BURST_COUNT  60

// 软件 bit-bang burst：每次 START + addr + STOP（与原版同节奏）
// 直接用 i2c_hal_soft_* 函数，与 test_i2c_gpio.c 的 bit-bang 完全一致
static void la_soft_burst(void)
{
    for (u32 i = 0; i < BURST_COUNT; i++) {
        i2c_hal_soft_start();
        (void)i2c_hal_soft_write_byte((u8)((AT24C02_ADDR << 1) | 0u));   // addr+W（NAK 也无所谓）
        i2c_hal_soft_stop();
    }
}

void test_i2c_gpio_la_run(void)
{
    printf("========================================\n");
    printf("GPIO bit-bang I2C - LA timing test\n");
    printf("========================================\n");
    printf("Wire: CH1 = PE6 (SCL), CH2 = PE7 (SDA), GND = GND\n");
    printf("AT24C02 may be disconnected\n");
    printf("========================================\n");

    i2c_hal_soft_init();

    printf("[Initial state]\n");
    printf("  GPIOEPU    = 0x%08x (should have PE6|PE7 set)\n", GPIOEPU);
    printf("  GPIOEPD    = 0x%08x\n", GPIOEPD);
    printf("  GPIOEDIR   = 0x%08x (PE7=input=release)\n", GPIOEDIR);
    printf("  GPIOE      = 0x%08x (PE6|PE7 should be high)\n", GPIOE);
    printf("\n");

    printf("[Burst 1] emitting %d transactions...\n", BURST_COUNT);
    printf("  >> Trigger LA now on PE6 falling edge <<\n");
    delay_ms(2000);
    la_soft_burst();
    printf("  Burst 1 done.\n");

    delay_ms(5000);

    printf("[Burst 2] verifying repeatability...\n");
    la_soft_burst();
    printf("  Burst 2 done.\n");

    printf("\n");
    printf("========================================\n");
    printf("Measure on PE6:\n");
    printf("  Expected: SCL period ~12 us = ~83 kHz\n");
    printf("  (SCL_low=5us + data_set=1us + SCL_high=5us)\n");
    printf("========================================\n");

    while (1);
}