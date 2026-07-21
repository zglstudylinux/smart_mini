/**
 * @file    test_spi_wave.c
 * @brief   BT892X SPI 逻辑分析仪波形测试（软件 bit-bang + 硬件 SPI1）
 *         —— 重构版：调用 spi_hal_* 公共原语，无重复 init 代码
 *
 *  接线：LA 接 PE6(CLK) + PE7(MOSI)；不接跳线不接 Flash
 *  编译宏 TEST_SPI_WAVE_PHASE 选择：
 *    0 = 全跑（软波形 + 硬波形，默认）
 *    1 = 只软波形
 *    2 = 只硬波形
 *
 *  LA 配置：SCLK=CH0 接 PE6，MOSI=CH1 接 PE7，CPOL=0/CPHA=0, MSB-first
 *  软/硬 SPI 都发单一字节 0x55 持续循环，方便看 clk+mosi 波形
 *
 * 参考手册: BT892X_UserManual_Driver.md §3.2 GPIO、§3.3 FUNCMCON1、§7 SPI
 */

#include "spi_hal.h"

#ifndef TEST_SPI_WAVE_PHASE
#define TEST_SPI_WAVE_PHASE 1    /* 默认软件 SPI（可改为 2 跑硬件 SPI） */
#endif

#define WAVE_BYTE   0x55

/* ===================== 子阶段 ===================== */
__attribute__((unused))
static void run_soft_waveform(void)
{
    printf("\n##### Phase 1: Software SPI Waveform #####\n");
    printf("CS=PE4, CLK=PE6, MOSI=PE7 (same pins as HW G4)\n");
    printf("LA: CH0=PE6(CLK), CH1=PE7(MOSI)\n");
    printf("Sending 0x55 continuously on PE7...\n\n");
    spi_hal_soft_init();

    /* 持续发 0x55 直到 LA 抓完（Mode 0, MSB-first） */
    while (1) {
        spi_hal_cs_low();          /* CS LOW */
        spi_hal_soft_byte(WAVE_BYTE);
        spi_hal_cs_high();         /* CS HIGH */
        delay_us(100);
    }
}

__attribute__((unused))
static void run_hw_waveform(void)
{
    printf("\n##### Phase 2: Hardware SPI1 Waveform #####\n");
    printf("CS=PE4(GPIO), CLK=PE6, MOSI=PE7 (HW SPI1 G4)\n");
    printf("LA: CH0=PE6(CLK), CH1=PE7(MOSI)\n");
    printf("Sending 0x55 continuously on PE7...\n\n");
    spi_hal_hw_init(239);          /* 100kHz */

    /* 持续发 0x55 直到 LA 抓完（Mode 0, MSB-first） */
    while (1) {
        spi_hal_cs_low();
        spi_hal_hw_byte(WAVE_BYTE);
        spi_hal_cs_high();
        delay_us(10);
    }
}

/* ===================== 入口 ===================== */
void test_spi_wave_run(void)
{
    printf("\n===== BT892X SPI Waveform Test =====\n");
    printf("LA: CH0=PE6(CLK), CH1=PE7(MOSI)\n");
    printf("(NOT with jumper, NOT with W25Q64 Flash)\n\n");
    printf("TEST_SPI_WAVE_PHASE=%d: %s\n\n",
           TEST_SPI_WAVE_PHASE,
           (TEST_SPI_WAVE_PHASE == 1) ? "soft bit-bang" :
           (TEST_SPI_WAVE_PHASE == 2) ? "HW SPI1 G4" :
           "???");

#if TEST_SPI_WAVE_PHASE == 1
    run_soft_waveform();
#elif TEST_SPI_WAVE_PHASE == 2
    run_hw_waveform();
#else
#error "TEST_SPI_WAVE_PHASE must be 1 (soft) or 2 (HW). Set via build options -DTEST_SPI_WAVE_PHASE=N"
#endif

    printf("===== SPI Waveform DONE =====\n");
    while (1);
}
