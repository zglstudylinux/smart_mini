/**
 * @file    test_spi_loop.c
 * @brief   BT892X SPI 跳线回环测试（软件 bit-bang + 硬件 SPI1）
 *         —— 重构版：调用 spi_hal_* 公共原语，无重复 init 代码
 *
 *  接线：跳线 PE7↔PE5；不接 Flash 不接 LA
 *  编译宏 TEST_SPI_LOOP_PHASE 选择：
 *    0 = 全跑（软回环 + 硬回环，默认）
 *    1 = 只软回环
 *    2 = 只硬回环
 *
 * 参考手册: BT892X_UserManual_Driver.md §3.2 GPIO、§3.3 FUNCMCON1、§7 SPI
 * 引脚手册: bt892x_pinfunction.md §4.3 PORTE / §5.2 SPI1
 */

#include "spi_hal.h"

#ifndef TEST_SPI_LOOP_PHASE
#define TEST_SPI_LOOP_PHASE 0
#endif

/* ===================== 子阶段 ===================== */
static int run_soft_loopback(void)
{
    int err;

    printf("\n##### Phase 1: Software SPI Loopback (bit-bang) #####\n");
    printf("PE4=CS, PE5=CLK, PE6=MOSI, PE7=MISO\n");
    printf("Wiring: jumper PE7 <-> PE5\n\n");
    spi_hal_soft_init();

    err = 0;
    for (int v = 0; v <= 0xFF; v++) {
        u8 rx = spi_hal_soft_byte((u8)v);
        if (rx != (u8)v) { err++; if (err <= 3) TEST_LOG("ERR: s=0x%02X r=0x%02X", v, rx); }
    }
    printf("Soft Loopback: %s (%d/256 errors)\n\n", err ? "FAILED" : "PASSED", err);
    return err;
}

static int run_hw_loopback(void)
{
    int err;

    printf("\n##### Phase 2: Hardware SPI1 Loopback #####\n");
    printf("PE4=CS(GPIO), PE6=CLK, PE7=MOSI, PE5=MISO (G4)\n");
    printf("Wiring: jumper PE7 <-> PE5\n\n");
    spi_hal_hw_init(239);   /* 100kHz, manual §7.2 BAUD = 239 */

    err = 0;
    for (int v = 0; v <= 0xFF; v++) {
        u8 rx = spi_hal_hw_byte((u8)v);
        if (rx != (u8)v) { err++; if (err <= 3) TEST_LOG("ERR: s=0x%02X r=0x%02X", v, rx); }
    }
    printf("HW Loopback: %s (%d/256 errors)\n\n", err ? "FAILED" : "PASSED", err);
    return err;
}

/* ===================== 入口 ===================== */
void test_spi_loop_run(void)
{
    int total_err;

    printf("\n===== BT892X SPI Loopback Test (Software + Hardware) =====\n");
    printf("Wiring: jumper PE7 <-> PE5\n");
    printf("(NOT with W25Q64 Flash, NOT with Logic Analyzer)\n\n");

    total_err = 0;

#if TEST_SPI_LOOP_PHASE == 1
    total_err += run_soft_loopback();
#elif TEST_SPI_LOOP_PHASE == 2
    total_err += run_hw_loopback();
#else
    /* Phase 0: 全跑 */
    total_err += run_soft_loopback();
    total_err += run_hw_loopback();
#endif

    printf("===== SPI Loopback Test DONE =====\n");
    printf("Summary: %s (total errors=%d)\n",
           total_err ? "FAILED" : "ALL PASSED", total_err);
    while (1);
}
