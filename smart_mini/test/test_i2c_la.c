// Hardware I2C timing test (Logic Analyzer) —— HAL 重构版
//
// HAL 抽象在 [i2c_hal.c](i2c_hal.c)，本文件仅做 LA 用的 burst + 初始状态打印。
//
// 接线：CH1 -> PE6 (SCL), CH2 -> PE7 (SDA), GND -> GND
//       AT24C02 可断开（IIC 控制器照常驱动 SCL）
//
// 用途：每次 transaction = START + 8 data + 1 ACK + STOP = 9 SCL clocks
// 用户用 LA 抓 PE6 波形，量一个 SCL 周期，与公式 SCL = IICK/(POSDIV+1) 对照

#include "test_common.h"
#include "i2c_hal.h"

#define TX_PER_TEST    60

// 硬件 IIC 持续 burst（每次 START+addr+STOP，timeout 1ms）
static void la_hw_burst(void)
{
    for (u32 i = 0; i < TX_PER_TEST; i++) {
        i2c_hal_hw_probe_addr(AT24C02_ADDR, false, 1000);
    }
}

void test_i2c_la_run(void)
{
    printf("========================================\n");
    printf("Hardware I2C timing test (Logic Analyzer)\n");
    printf("========================================\n");
    printf("Wire: CH1 = PE6 (SCL), CH2 = PE7 (SDA), GND = GND\n");
    printf("AT24C02 may be disconnected (SCL still drives)\n");
    printf("Manual: Sec.8 IIC, registers IICCON0[POSDIV]/[IIC_EN]\n");
    printf("========================================\n");

    i2c_hal_hw_init();

    // Initial state
    printf("[Initial state]\n");
    printf("  CLKGAT1 = 0x%08x\n", CLKGAT1);
    printf("  CLKCON1 = 0x%08x\n", CLKCON1);
    printf("  CLKCON2 = 0x%08x\n", CLKCON2);
    printf("  IICCON0 = 0x%08x\n", IICCON0);
    printf("\n");

    printf("[Burst 1] emitting %d transactions...\n", TX_PER_TEST);
    printf("  >> Trigger LA now on PE6 rising edge <<\n");
    delay_ms(2000);
    la_hw_burst();
    printf("  Burst 1 done.\n");

    delay_ms(5000);

    printf("[Burst 2] verifying repeatability...\n");
    la_hw_burst();
    printf("  Burst 2 done.\n");

    printf("\n");
    printf("========================================\n");
    printf("Done.\n");
    printf("Measure SCL single pulse period on PE6:\n");
    printf("  Expected (per Sec.8.2 formula):\n");
    printf("    SCL = IICK / (POSDIV+1) = IICK / 20\n");
    printf("  Your LA reading is the ground-truth actual IICK.\n");
    printf("========================================\n");

    while (1);
}