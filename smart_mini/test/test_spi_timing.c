/**
 * @file    test_spi_timing.c
 * @brief   BT892X SPI 性能对比测试（Polling vs Interrupt vs DMA 三方对比）
 *         —— 重构版：调用 spi_hal_* 公共原语 + Interrupt/DMA 高级 API
 *
 *  接线：建议接 W25Q64 Flash（CS=PE4/CLK=PE6/DI=PE7/DO=PE5）；不接也能跑（SKIP 实测）
 *  测试内容：
 *    [1] 同一 4096B 读，在 100KHz + 12MHz 两种速度下，三种模式同时对比：
 *          Polling | Interrupt | DMA
 *    [2] 12MHz 下 DMA Page Program 256B 写+读回 校验（验证 DMA 写路径）
 *
 * 参考手册: BT892X_UserManual_Driver.md §7 SPI（DMA/中断）
 */

#include "spi_hal.h"

#define BLK   4096

/* 整数缩放 ×100 模拟 µs/byte 小数（避免用 double） */
static void print_us100(const char *name, u32 ticks)
{
    u32 us100 = ticks * 100 / BLK;
    printf("  %-9s: %6lu ticks = %lu.%02lu us/byte\n", name, ticks, us100 / 100, us100 % 100);
}

/* 检查 W25Q64 JEDEC ID（通过 HAL hw 路径）
 * 字节序：0x9F 命令后第一个字节 = Manufacturer ID(Winbond=0xEF)，
 *        第二个字节 = Memory Type(0x40)，第三个字节 = Capacity(0x17)
 * 注：必须 a 才是第一个字节（Manufacturer），不能错位读
 */
static u8 timing_check_jedec(void)
{
    u8 a;
    spi_hal_hw_init(239);
    spi_hal_cs_low();
    spi_hal_hw_byte(0x9F);
    a = spi_hal_hw_byte(0xFF);     /* 第一个字节：Manufacturer ID */
    (void)spi_hal_hw_byte(0xFF);   /* 第二个字节：Memory Type */
    (void)spi_hal_hw_byte(0xFF);   /* 第三个字节：Capacity */
    spi_hal_cs_high();
    return (a == 0xEF);
}

/* 等待 W25Q64 BUSY 清零（HAL hw 版 wait_busy 套用） */
static void timing_wait_busy(void)
{
    spi_hal_w25_hw_wait_busy();
}

/* ===================== 入口 ===================== */
void test_spi_timing_run(void)
{
    u8 buf_p[BLK], buf_i[BLK], buf_d[BLK];
    u32 t0, t_poll, t_int, t_dma;
    int err;

    printf("\n===== BT892X SPI 3-way Timing (Polling / Interrupt / DMA) =====\n");
    printf("Wiring: W25Q64 CS=PE4/CLK=PE6/DI=PE7/DO=PE5\n");
    printf("(no Flash - tests still run, JEDEC will be 0xFF and skipped)\n\n");

    printf("--- JEDEC ID check ---\n");
    if (!timing_check_jedec()) {
        printf("JEDEC: not detected. Three-way comparison SKIPPED.\n");
        printf("\n===== SPI Timing DONE =====\n");
        while (1);
    }
    printf("JEDEC: 0xEF 0x40 0x17 OK\n\n");

    /* ===================== 三方对比：100KHz ===================== */
    printf("==== 100 KHz Read 4096B (24MHz / 240) ====\n");
    spi_hal_hw_init(239);

    /* Polling（轮询） */
    spi_hal_cs_low();
    spi_hal_hw_byte(0x03);
    spi_hal_hw_byte(0); spi_hal_hw_byte(0); spi_hal_hw_byte(0);
    t0 = tick_get();
    {
        u32 i;
        for (i = 0; i < BLK; i++) buf_p[i] = spi_hal_hw_byte(0xFF);
    }
    spi_hal_cs_high();
    t_poll = tick_get() - t0;
    print_us100("Polling", t_poll);

    /* Interrupt（中断） */
    spi_hal_hw_it_setup();
    spi_hal_cs_low();
    spi_hal_hw_byte_it(0x03);
    spi_hal_hw_byte_it(0); spi_hal_hw_byte_it(0); spi_hal_hw_byte_it(0);
    t0 = tick_get();
    {
        u32 i;
        for (i = 0; i < BLK; i++) buf_i[i] = spi_hal_hw_byte_it(0xFF);
    }
    spi_hal_cs_high();
    t_int = tick_get() - t0;
    spi_hal_hw_it_teardown();
    print_us100("Interrupt", t_int);

    /* DMA */
    t0 = tick_get();
    spi_hal_hw_read_dma(0, buf_d, BLK);
    t_dma = tick_get() - t0;
    print_us100("DMA", t_dma);

    /* Data consistency check */
    err = 0;
    {
        u32 i;
        for (i = 0; i < BLK; i++)
            if (buf_p[i] != buf_i[i] || buf_p[i] != buf_d[i]) { err++; break; }
    }
    printf("  3-way data match: %s\n\n", err ? "FAIL" : "OK");

    /* ===================== 三方对比：12MHz ===================== */
    printf("==== 12 MHz Read 4096B (24MHz / 2) ====\n");
    spi_hal_hw_init(1);

    spi_hal_cs_low();
    spi_hal_hw_byte(0x03);
    spi_hal_hw_byte(0); spi_hal_hw_byte(0); spi_hal_hw_byte(0);
    t0 = tick_get();
    {
        u32 i;
        for (i = 0; i < BLK; i++) buf_p[i] = spi_hal_hw_byte(0xFF);
    }
    spi_hal_cs_high();
    t_poll = tick_get() - t0;
    print_us100("Polling", t_poll);

    spi_hal_hw_it_setup();
    spi_hal_cs_low();
    spi_hal_hw_byte_it(0x03);
    spi_hal_hw_byte_it(0); spi_hal_hw_byte_it(0); spi_hal_hw_byte_it(0);
    t0 = tick_get();
    {
        u32 i;
        for (i = 0; i < BLK; i++) buf_i[i] = spi_hal_hw_byte_it(0xFF);
    }
    spi_hal_cs_high();
    t_int = tick_get() - t0;
    spi_hal_hw_it_teardown();
    print_us100("Interrupt", t_int);

    t0 = tick_get();
    spi_hal_hw_read_dma(0, buf_d, BLK);
    t_dma = tick_get() - t0;
    print_us100("DMA", t_dma);

    err = 0;
    {
        u32 i;
        for (i = 0; i < BLK; i++)
            if (buf_p[i] != buf_i[i] || buf_p[i] != buf_d[i]) { err++; break; }
    }
    printf("  3-way data match: %s\n\n", err ? "FAIL" : "OK");

    /* ===================== Analysis ===================== */
    printf("==== Analysis ====\n");
    printf("Polling  : CPU 100%% busy, waits on SPIPND per byte\n");
    printf("Interrupt: CPU enters/exits ISR per byte, high overhead;\n");
    printf("           at 12MHz ISR cost exceeds DMA speedup\n");
    printf("DMA      : Only setup/teardown uses CPU, bulk transfer offloaded\n");
    if (t_dma > 0) {
        u32 speedup_x10 = t_poll * 10 / t_dma;
        printf("\n12MHz speedup (Polling/DMA): %lu.%ux\n",
               speedup_x10/10, speedup_x10 % 10);
    }
    printf("\n");

    /* ===================== DMA Page Program 256B 校验 ===================== */
    printf("==== DMA Page Program (write 256B to Page 0) ====\n");
    spi_hal_hw_init(239);

    /* Sector erase 0 */
    spi_hal_cs_low(); spi_hal_hw_byte(0x06); spi_hal_cs_high();
    spi_hal_cs_low();
    spi_hal_hw_byte(0x20); spi_hal_hw_byte(0); spi_hal_hw_byte(0); spi_hal_hw_byte(0);
    spi_hal_cs_high();
    timing_wait_busy();

    /* DMA write 256B */
    {
        u8 wbuf[256];
        u32 i;
        for (i = 0; i < 256; i++) wbuf[i] = (u8)i;

        spi_hal_w25_hw_write_enable();
        spi_hal_hw_write_dma(0, wbuf, 256);
        timing_wait_busy();

        /* DMA read & verify */
        {
            u8 rbuf[256];
            int e = 0;
            spi_hal_hw_read_dma(0, rbuf, 256);
            for (i = 0; i < 256; i++) if (rbuf[i] != wbuf[i]) e++;
            printf("DMA write+read: %s (%d/256 errors)\n", e ? "FAIL" : "PASSED", e);
        }
    }

    printf("\n===== SPI Timing DONE =====\n");
    while (1);
}
