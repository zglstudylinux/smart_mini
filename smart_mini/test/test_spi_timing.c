/**
 * @file    test_spi_timing.c
 * @brief   BT892X SPI 性能对比测试（polling / interrupt / DMA）
 *
 *  接线：需要 W25Q64 Flash（CS=PE4/CLK=PE6/DI=PE7/DO=PE5）；不接也能跑
 *  测试内容：
 *    [A] 中断模式：JEDEC ID + 512B/4096B 读耗时
 *    [B] DMA 模式：100KHz vs 12MHz 下 polling vs DMA 时间对比 + DMA page program 校验
 *
 * 参考手册: BT892X_UserManual_Driver.md §7 SPI（DMA/中断）
 */

#include "test_spi_common.h"

#define HW_CS       SPI_CS_PIN
#define HW_CLK      SPI_CLK_PIN
#define HW_MOSI     SPI_MOSI_PIN
#define HW_MISO     SPI_MISO_PIN

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

static void hw_read_polling(u32 addr, u8 *buf, u32 len)
{
    hw_cs_low();
    hw_spi_byte(0x03);
    hw_spi_byte((addr>>16)&0xFF); hw_spi_byte((addr>>8)&0xFF); hw_spi_byte(addr&0xFF);
    for (u32 i = 0; i < len; i++) buf[i] = hw_spi_byte(0xFF);
    hw_cs_high();
}

/* ===================== 中断模式 ===================== */
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

static void run_interrupt(void)
{
    printf("\n##### [A/3] SPI Interrupt Mode #####\n");
    hw_spi_init(239);
    SPI1CON |= BIT(7);
    register_isr(IRQ_SPI_VECTOR, spi_isr);
    PICEN |= BIT(IRQ_SPI_VECTOR);

    printf("--- JEDEC ID ---\n");
    hw_cs_low(); int_spi_byte(0x9F);
    u8 a = int_spi_byte(0xFF), b = int_spi_byte(0xFF), c = int_spi_byte(0xFF);
    hw_cs_high();
    printf("JEDEC: 0x%02X 0x%02X 0x%02X %s\n", a, b, c, a==0xEF?"OK":"FAIL/MISSING");

    if (a != 0xEF) {
        printf("(no W25Q64 - interrupt read SKIPPED)\n");
    } else {
        u8 buf[512];
        u32 t0 = tick_get();
        int_spi_read(0, buf, 512);
        printf("Interrupt read 512B: %lu ticks\n", tick_get() - t0);

        u8 buf4[4096];
        t0 = tick_get();
        int_spi_read(0, buf4, 4096);
        printf("Interrupt read 4096B: %lu ticks\n", tick_get() - t0);
    }

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

static void run_dma_compare(void)
{
    printf("\n##### [B/3] SPI DMA Mode (Polling vs DMA) #####\n");
    #define BLK 4096
    u8 buf1[BLK], buf2[BLK];
    u32 t0, t1, t_poll, t_dma;

    hw_spi_init(239);

    printf("--- JEDEC ID ---\n");
    hw_cs_low(); hw_spi_byte(0x9F);
    u8 a = hw_spi_byte(0xFF), b = hw_spi_byte(0xFF), c = hw_spi_byte(0xFF);
    hw_cs_high();
    printf("JEDEC: 0x%02X 0x%02X 0x%02X %s\n", a, b, c, a==0xEF?"OK":"FAIL/MISSING");

    if (a != 0xEF) {
        printf("(no W25Q64 - DMA test SKIPPED)\n");
        return;
    }

    // ---- 100KHz Polling vs DMA ----
    printf("\n--- 100KHz Polling vs DMA (4096B) ---\n");
    hw_spi_init(239);
    t0 = tick_get(); hw_read_polling(0, buf1, BLK); t1 = tick_get();
    printf("Polling: %lu ticks\n", t1 - t0);
    t0 = tick_get(); read_dma(0, buf2, BLK); t1 = tick_get();
    printf("DMA:     %lu ticks\n", t1 - t0);
    int err = 0;
    for (int i = 0; i < BLK; i++) if (buf1[i] != buf2[i]) { err++; break; }
    printf("Data match: %s\n", err?"FAIL":"OK");

    // ---- 12MHz Polling vs DMA ----
    printf("\n--- 12MHz Polling vs DMA (4096B) ---\n");
    hw_spi_init(1);
    t0 = tick_get(); hw_read_polling(0, buf1, BLK); t_poll = tick_get() - t0;
    printf("Polling: %lu ticks\n", t_poll);
    t0 = tick_get(); read_dma(0, buf2, BLK); t_dma = tick_get() - t0;
    printf("DMA:     %lu ticks\n", t_dma);
    err = 0;
    for (int i = 0; i < BLK; i++) if (buf1[i] != buf2[i]) { err++; break; }
    printf("Data match: %s\n", err?"FAIL":"OK");

    printf("\n12MHz Polling: %lu ticks, DMA: %lu ticks\n", t_poll, t_dma);
    if (t_dma < t_poll)
        printf("DMA frees CPU during transfer; at 12MHz ~%ld%% faster\n",
               100*(t_poll-t_dma)/t_poll);

    // ---- DMA Page Program 256B ----
    printf("\n--- DMA Page Program (write 256B) ---\n");
    hw_spi_init(239);
    // Sector erase
    hw_cs_low(); hw_spi_byte(0x06); hw_cs_high();  // WE
    hw_cs_low();
    hw_spi_byte(0x20); hw_spi_byte(0); hw_spi_byte(0); hw_spi_byte(0);
    hw_cs_high();
    while (1) {  // wait busy
        hw_cs_low(); hw_spi_byte(0x05);
        u8 s = hw_spi_byte(0xFF); hw_cs_high();
        if (!(s & 1)) break;
    }
    // DMA write
    u8 wbuf[256], rbuf[256];
    for (int i = 0; i < 256; i++) wbuf[i] = (u8)i;
    hw_cs_low(); hw_spi_byte(0x06); hw_cs_high();  // WE
    hw_cs_low(); hw_spi_byte(0x02); hw_spi_byte(0); hw_spi_byte(0); hw_spi_byte(0);
    SPI1CON &= ~BIT(4);
    SPI1DMAADR = (u32)wbuf;
    SPI1DMACNT = 256;
    while (!(SPI1CON & BIT(16)));
    SPI1CPND = BIT(16);
    hw_cs_high();
    // wait busy
    while (1) {
        hw_cs_low(); hw_spi_byte(0x05);
        u8 s = hw_spi_byte(0xFF); hw_cs_high();
        if (!(s & 1)) break;
    }
    // verify
    read_dma(0, rbuf, 256);
    int e = 0;
    for (int i = 0; i < 256; i++) if (rbuf[i] != wbuf[i]) e++;
    printf("DMA write+read: %s (%d/256 errors)\n", e?"FAIL":"PASSED", e);
}

/* ===================== 入口 ===================== */
void test_spi_timing_run(void)
{
    printf("\n===== BT892X SPI Timing Test (Interrupt vs DMA) =====\n");
    printf("Wiring: W25Q64 CS=PE4/CLK=PE6/DI=PE7/DO=PE5\n");
    printf("(no Flash - tests still run, JEDEC read will be 0xFF and skipped)\n\n");

    run_interrupt();
    run_dma_compare();

    printf("\n===== SPI Timing DONE =====\n");
    while (1);
}
