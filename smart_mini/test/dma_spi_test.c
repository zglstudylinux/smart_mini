/**
 * @file    dma_spi_test.c
 * @brief   BT892X SPI1 DMA 操作 W25Q64 — 速度对比
 *
 *  硬件 SPI1 G4: CLK=PE6, MOSI=PE7, MISO=PE5, CS=PE4(GPIO)
 *  对比: 非 DMA (轮询) vs DMA 读 4096 字节
 *
 *  DMA 原理:
 *    SPI1DMAADR = RAM 地址
 *    SPI1DMACNT = 字节数 (写即启动)
 *    RXSEL: 0=发送, 1=接收
 */

#include "test.h"

#define D_CS    BIT(4)
#define D_CLK   BIT(6)
#define D_MOSI  BIT(7)
#define D_MISO  BIT(5)

static void dma_spi_init(u32 baud)
{
    FUNCMCON1 &= ~(0xF << 12);
    FUNCMCON1 |=  (0x4 << 12);

    GPIOEFEN |= D_CLK | D_MOSI | D_MISO;
    GPIOEDE  |= D_CLK | D_MOSI | D_MISO;
    GPIOEDIR &= ~(D_CLK | D_MOSI);
    GPIOEDIR |=  D_MISO;

    GPIOEFEN &= ~D_CS;  GPIOEDE |= D_CS;
    GPIOEDIR &= ~D_CS;  GPIOESET = D_CS;

    SPI1BAUD = baud;
    SPI1CON  = BIT(0);
}

static u8 dma_spi_byte(u8 tx)
{
    SPI1BUF = tx;
    while (!(SPI1CON & BIT(16)));
    SPI1CPND = BIT(16);
    return (u8)SPI1BUF;
}

/* ================================================================
 *  非 DMA 读: 命令+地址手动, 数据逐字节轮询
 * ================================================================ */
static void read_polling(u32 addr, u8 *buf, u32 len)
{
    GPIOECLR = D_CS; delay_us(1);
    dma_spi_byte(0x03);
    dma_spi_byte(addr>>16); dma_spi_byte(addr>>8); dma_spi_byte(addr);
    for (u32 i = 0; i < len; i++)
        buf[i] = dma_spi_byte(0xFF);
    GPIOESET = D_CS; delay_us(1);
}

/* ================================================================
 *  DMA 读: 命令+地址手动, 数据由 DMA 自动接收
 * ================================================================ */
static void read_dma(u32 addr, u8 *buf, u32 len)
{
    GPIOECLR = D_CS; delay_us(1);
    dma_spi_byte(0x03);
    dma_spi_byte(addr>>16); dma_spi_byte(addr>>8); dma_spi_byte(addr);

    // 切换到 DMA 接收
    SPI1CON |= BIT(4);              // RXSEL=1 (接收)
    SPI1DMAADR = (u32)buf;          // 缓冲区地址
    SPI1DMACNT = len;               // 写即启动 DMA
    while (!(SPI1CON & BIT(16)));   // 等 SPIPND
    SPI1CPND = BIT(16);
    SPI1CON &= ~BIT(4);             // 恢复

    GPIOESET = D_CS; delay_us(1);
}

/* ================================================================
 *  DMA 写: Page Program 数据部分用 DMA 发送
 * ================================================================ */
static void page_program_dma(u32 addr, u8 *buf, u32 len)
{
    // Write Enable
    GPIOECLR = D_CS; dma_spi_byte(0x06); GPIOESET = D_CS;

    // 命令+地址
    GPIOECLR = D_CS; delay_us(1);
    dma_spi_byte(0x02);
    dma_spi_byte(addr>>16); dma_spi_byte(addr>>8); dma_spi_byte(addr);

    // DMA 发送数据
    SPI1CON &= ~BIT(4);             // RXSEL=0 (发送)
    SPI1DMAADR = (u32)buf;
    SPI1DMACNT = len;
    while (!(SPI1CON & BIT(16)));
    SPI1CPND = BIT(16);

    GPIOESET = D_CS; delay_us(1);

    // 等 Flash 写完成
    while (1) {
        GPIOECLR = D_CS; dma_spi_byte(0x05);
        u8 sr = dma_spi_byte(0xFF); GPIOESET = D_CS;
        if (!(sr & 1)) break;
    }
}

/* ================================================================
 *  主测试
 * ================================================================ */
void dma_spi_test(void)
{
    printf("\n===== SPI1 DMA vs Polling: Read 4096 bytes =====\n\n");

    #define BLK 4096
    u8 buf1[BLK], buf2[BLK];
    u32 t0, t1;

    // ====== 低速 100KHz ======
    dma_spi_init(239);
    printf("--- 100KHz ---\n");

    t0 = tick_get();
    read_polling(0, buf1, BLK);
    t1 = tick_get();
    printf("Polling: %lu ticks\n", t1 - t0);

    t0 = tick_get();
    read_dma(0, buf2, BLK);
    t1 = tick_get();
    printf("DMA:     %lu ticks\n", t1 - t0);

    // 数据正确性
    int err = 0;
    for (int i = 0; i < BLK; i++)
        if (buf1[i] != buf2[i]) { err++; break; }
    printf("Data match: %s\n\n", err ? "FAIL" : "OK");

    // ====== 高速 12MHz ======
    dma_spi_init(1);
    printf("--- 12MHz ---\n");

    t0 = tick_get();
    read_polling(0, buf1, BLK);
    t1 = tick_get();
    u32 t_poll = t1 - t0;
    printf("Polling: %lu ticks\n", t_poll);

    t0 = tick_get();
    read_dma(0, buf2, BLK);
    t1 = tick_get();
    u32 t_dma = t1 - t0;
    printf("DMA:     %lu ticks\n", t_dma);

    err = 0;
    for (int i = 0; i < BLK; i++)
        if (buf1[i] != buf2[i]) { err++; break; }
    printf("Data match: %s\n\n", err ? "FAIL" : "OK");

    // ====== DMA 页写入测试 ======
    printf("--- DMA Page Program: write 256 bytes to Page 0 ---\n");
    dma_spi_init(239);

    // 擦除
    GPIOECLR = D_CS; dma_spi_byte(0x06); GPIOESET = D_CS;  // WE
    GPIOECLR = D_CS;
    dma_spi_byte(0x20); dma_spi_byte(0); dma_spi_byte(0); dma_spi_byte(0);
    GPIOESET = D_CS;
    while (1) {
        GPIOECLR = D_CS; dma_spi_byte(0x05);
        u8 s = dma_spi_byte(0xFF); GPIOESET = D_CS;
        if (!(s & 1)) break;
    }

    // DMA 写 256 字节
    u8 wbuf[256], rbuf[256];
    for (int i = 0; i < 256; i++) wbuf[i] = (u8)i;
    page_program_dma(0, wbuf, 256);

    // 读回校验
    read_dma(0, rbuf, 256);
    int e = 0;
    for (int i = 0; i < 256; i++) if (rbuf[i] != wbuf[i]) e++;
    printf("DMA write+read: %s (%d/256 errors)\n", e ? "FAIL" : "PASSED", e);

    // 恢复低速
    dma_spi_init(239);

    printf("\n=== Analysis ===\n");
    printf("12MHz Polling: %lu ticks, DMA: %lu ticks\n", t_poll, t_dma);
    printf("DMA frees CPU during transfer; at 12MHz difference is ");
    if (t_dma < t_poll) printf("~%ld%% faster\n", 100*(t_poll-t_dma)/t_poll);
    else printf("similar (both limited by SPI clock speed)\n");
    printf("DMA advantage: zero CPU overhead during transfer,\n");
    printf("  critical for real-time audio/streaming applications.\n");

    while (1);
}
