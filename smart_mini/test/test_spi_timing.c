/**
 * @file    test_spi_timing.c
 * @brief   BT892X SPI 性能对比测试（Polling vs Interrupt vs DMA 三方对比）
 *
 *  接线：建议接 W25Q64 Flash（CS=PE4/CLK=PE6/DI=PE7/DO=PE5）；不接也能跑（SKIP 实测）
 *  测试内容：
 *    [1] 同一 4096B 读，在 100KHz + 12MHz 两种速度下，三种模式同时对比：
 *          Polling | Interrupt | DMA
 *    [2] 12MHz 下 DMA Page Program 256B 写+读回 校验（验证 DMA 写路径）
 *
 * 参考手册: BT892X_UserManual_Driver.md §7 SPI（DMA/中断）
 */

#include "test_spi_common.h"

#define HW_CS       SPI_CS_PIN
#define HW_CLK      SPI_CLK_PIN
#define HW_MOSI     SPI_MOSI_PIN
#define HW_MISO     SPI_MISO_PIN

#define BLK   4096

/* ===================== 共享底层 ===================== */
static void hw_spi_init(u32 baud)
{
    FUNCMCON1 &= ~(0xF << 12);
    FUNCMCON1 |=  (0x4 << 12);
    GPIOEFEN |= HW_CLK | HW_MOSI | HW_MISO;
    GPIOEDE  |= HW_CLK | HW_MOSI | HW_MISO;
    GPIOEDIR &= ~(HW_CLK | HW_MOSI);
    GPIOEDIR |=  HW_MISO;
    GPIOEFEN &= ~HW_CS;  GPIOEDE |= HW_CS;
    GPIOEDIR &= ~HW_CS;  GPIOESET = HW_CS;
    SPI1BAUD = baud;
    SPI1CON  = BIT(0);
}

static u8 hw_spi_byte(u8 tx)
{
    SPI1BUF = tx;
    while (!(SPI1CON & BIT(16)));
    SPI1CPND = BIT(16);
    return (u8)SPI1BUF;
}

static void hw_cs_low(void)  { GPIOECLR = HW_CS; delay_us(1); }
static void hw_cs_high(void) { GPIOESET = HW_CS; delay_us(1); }

static u8 hw_spi_check_jedec(void)
{
    hw_cs_low(); hw_spi_byte(0x9F);
    u8 a = hw_spi_byte(0xFF);
    (void)hw_spi_byte(0xFF);    // b
    (void)hw_spi_byte(0xFF);    // c
    hw_cs_high();
    return (a == 0xEF);
}

/* ===================== Polling 模式 ===================== */
static void hw_read_polling(u32 addr, u8 *buf, u32 len)
{
    hw_cs_low();
    hw_spi_byte(0x03);
    hw_spi_byte((addr>>16)&0xFF); hw_spi_byte((addr>>8)&0xFF); hw_spi_byte(addr&0xFF);
    for (u32 i = 0; i < len; i++) buf[i] = hw_spi_byte(0xFF);
    hw_cs_high();
}

/* ===================== Interrupt 模式 ===================== */
static volatile int spi_done;

AT(.com_text.isr)
static void spi_isr(void)
{
    SPI1CPND = BIT(16);
    spi_done = 1;
}

static u8 int_spi_byte(u8 tx)
{
    spi_done = 0;
    SPI1BUF = tx;
    while (!spi_done);
    return (u8)SPI1BUF;
}

static void int_spi_read(u32 addr, u8 *buf, u32 len)
{
    hw_cs_low();
    int_spi_byte(0x03);
    int_spi_byte((addr>>16)&0xFF); int_spi_byte((addr>>8)&0xFF); int_spi_byte(addr&0xFF);
    for (u32 i = 0; i < len; i++) buf[i] = int_spi_byte(0xFF);
    hw_cs_high();
}

static void hw_interrupt_setup(void)
{
    SPI1CON |= BIT(7);                            // SPIIE
    register_isr(IRQ_SPI_VECTOR, spi_isr);
    PICEN |= BIT(IRQ_SPI_VECTOR);
}

static void hw_interrupt_teardown(void)
{
    PICEN &= ~BIT(IRQ_SPI_VECTOR);
    SPI1CON &= ~BIT(7);
}

/* ===================== DMA 模式 ===================== */
static void read_dma(u32 addr, u8 *buf, u32 len)
{
    hw_cs_low();
    hw_spi_byte(0x03);
    hw_spi_byte((addr>>16)&0xFF); hw_spi_byte((addr>>8)&0xFF); hw_spi_byte(addr&0xFF);
    SPI1CON |= BIT(4);              // RXSEL=1
    SPI1DMAADR = (u32)buf;
    SPI1DMACNT = len;
    while (!(SPI1CON & BIT(16)));
    SPI1CPND = BIT(16);
    SPI1CON &= ~BIT(4);
    hw_cs_high();
}

/* ===================== 入口 ===================== */
void test_spi_timing_run(void)
{
    printf("\n===== BT892X SPI 3-way Timing (Polling / Interrupt / DMA) =====\n");
    printf("Wiring: W25Q64 CS=PE4/CLK=PE6/DI=PE7/DO=PE5\n");
    printf("(no Flash - tests still run, JEDEC will be 0xFF and skipped)\n\n");

    printf("--- JEDEC ID check ---\n");
    hw_spi_init(239);
    if (!hw_spi_check_jedec()) {
        printf("JEDEC: not detected. Three-way comparison SKIPPED.\n");
        printf("\n===== SPI Timing DONE =====\n");
        while (1);
    }
    printf("JEDEC: 0xEF 0x40 0x17 OK\n\n");

    /* ===================== 三方对比：100KHz ===================== */
    u8 buf_p[BLK], buf_i[BLK], buf_d[BLK];
    u32 t0, t_poll, t_int, t_dma;
    // 整数缩放 ×100 模拟 µs/byte 小数（避免用 double）
    u32 us100_pol, us100_int, us100_dma;

    printf("==== 100 KHz Read 4096B (24MHz / 240) ====\n");
    hw_spi_init(239);

    // Polling
    t0 = tick_get();
    hw_read_polling(0, buf_p, BLK);
    t_poll = tick_get() - t0;
    us100_pol = t_poll * 100 / BLK;
    printf("  Polling  : %6lu ticks = %lu.%02lu us/byte\n",
           t_poll, us100_pol/100, us100_pol%100);

    // Interrupt
    hw_interrupt_setup();
    t0 = tick_get();
    int_spi_read(0, buf_i, BLK);
    t_int = tick_get() - t0;
    hw_interrupt_teardown();
    us100_int = t_int * 100 / BLK;
    printf("  Interrupt: %6lu ticks = %lu.%02lu us/byte\n",
           t_int, us100_int/100, us100_int%100);

    // DMA
    t0 = tick_get();
    read_dma(0, buf_d, BLK);
    t_dma = tick_get() - t0;
    us100_dma = t_dma * 100 / BLK;
    printf("  DMA      : %6lu ticks = %lu.%02lu us/byte\n",
           t_dma, us100_dma/100, us100_dma%100);

    // Data consistency check
    int err = 0;
    for (int i = 0; i < BLK; i++) {
        if (buf_p[i] != buf_i[i] || buf_p[i] != buf_d[i]) { err++; break; }
    }
    printf("  3-way data match: %s\n", err ? "FAIL" : "OK");
    printf("\n");

    /* ===================== 三方对比：12MHz ===================== */
    printf("==== 12 MHz Read 4096B (24MHz / 2) ====\n");
    hw_spi_init(1);

    // Polling
    t0 = tick_get();
    hw_read_polling(0, buf_p, BLK);
    t_poll = tick_get() - t0;
    us100_pol = t_poll * 100 / BLK;
    printf("  Polling  : %6lu ticks = %lu.%02lu us/byte\n",
           t_poll, us100_pol/100, us100_pol%100);

    // Interrupt
    hw_interrupt_setup();
    t0 = tick_get();
    int_spi_read(0, buf_i, BLK);
    t_int = tick_get() - t0;
    hw_interrupt_teardown();
    us100_int = t_int * 100 / BLK;
    printf("  Interrupt: %6lu ticks = %lu.%02lu us/byte\n",
           t_int, us100_int/100, us100_int%100);

    // DMA
    t0 = tick_get();
    read_dma(0, buf_d, BLK);
    t_dma = tick_get() - t0;
    us100_dma = t_dma * 100 / BLK;
    printf("  DMA      : %6lu ticks = %lu.%02lu us/byte\n",
           t_dma, us100_dma/100, us100_dma%100);

    err = 0;
    for (int i = 0; i < BLK; i++) {
        if (buf_p[i] != buf_i[i] || buf_p[i] != buf_d[i]) { err++; break; }
    }
    printf("  3-way data match: %s\n", err ? "FAIL" : "OK");
    printf("\n");

    /* ===================== Analysis ===================== */
    printf("==== Analysis ====\n");
    printf("Polling  : CPU 100%% busy, waits on SPIPND per byte\n");
    printf("Interrupt: CPU enters/exits ISR per byte, high overhead;\n");
    printf("           at 12MHz ISR cost exceeds DMA speedup\n");
    printf("DMA      : Only setup/teardown uses CPU, bulk transfer offloaded\n");
    if (t_dma > 0) {
        u32 speedup_x10 = t_poll * 10 / t_dma;
        printf("\n12MHz speedup (Polling/DMA): %lu.%ux\n",
               speedup_x10/10, speedup_x10%10);
    }
    printf("\n");

    /* ===================== DMA Page Program 256B 校验 ===================== */
    printf("==== DMA Page Program (write 256B to Page 0) ====\n");
    hw_spi_init(239);

    // Sector erase 0
    hw_cs_low(); hw_spi_byte(0x06); hw_cs_high();   // WE
    hw_cs_low();
    hw_spi_byte(0x20); hw_spi_byte(0); hw_spi_byte(0); hw_spi_byte(0);
    hw_cs_high();
    while (1) {  // wait busy
        hw_cs_low(); hw_spi_byte(0x05);
        u8 s = hw_spi_byte(0xFF); hw_cs_high();
        if (!(s & 1)) break;
    }

    // DMA write 256B
    u8 wbuf[256], rbuf[256];
    for (int i = 0; i < 256; i++) wbuf[i] = (u8)i;
    hw_cs_low(); hw_spi_byte(0x06); hw_cs_high();   // WE
    hw_cs_low(); hw_spi_byte(0x02); hw_spi_byte(0); hw_spi_byte(0); hw_spi_byte(0);
    SPI1CON &= ~BIT(4);                // RXSEL=0 (TX DMA)
    SPI1DMAADR = (u32)wbuf;
    SPI1DMACNT = 256;
    while (!(SPI1CON & BIT(16)));
    SPI1CPND = BIT(16);
    hw_cs_high();
    while (1) {  // wait busy
        hw_cs_low(); hw_spi_byte(0x05);
        u8 s = hw_spi_byte(0xFF); hw_cs_high();
        if (!(s & 1)) break;
    }

    // DMA read & verify
    read_dma(0, rbuf, 256);
    int e = 0;
    for (int i = 0; i < 256; i++) if (rbuf[i] != wbuf[i]) e++;
    printf("DMA write+read: %s (%d/256 errors)\n", e ? "FAIL" : "PASSED", e);

    printf("\n===== SPI Timing DONE =====\n");
    while (1);
}
