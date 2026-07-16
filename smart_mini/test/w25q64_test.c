/**
 * @file    w25q64_test.c
 * @brief   BT892X 软件 SPI 驱动 W25Q64 Flash — 全套实验
 *
 *  引脚 (PORTE):  PE4=CS, PE5=CLK, PE6=MOSI, PE7=MISO
 *  SPI Mode 0, MSB first
 *
 *  实验列表 (main.c 中切换):
 *    w25q64_exp1_jedec_id()     - 读 JEDEC ID
 *    w25q64_exp2_status()       - 读 Status Register
 *    w25q64_exp3_page_rw()      - 页写入与读取
 *    w25q64_exp4_cross_page()   - 跨页连续写入
 *    w25q64_exp5_sector_erase() - 扇区擦除与验证
 *    w25q64_exp6_erase_timing() - 擦除耗时对比
 *    w25q64_exp7_write_protect()- 写保护配置
 *    w25q64_exp9_unique_id()    - Unique ID + SFDP
 */

#include "test.h"

#define SPI_CS      BIT(4)
#define SPI_CLK     BIT(5)
#define SPI_MOSI    BIT(6)
#define SPI_MISO    BIT(7)

/* ================================================================
 *  底层函数
 * ================================================================ */

static void w25q64_init(void)
{
    GPIOEFEN &= ~(SPI_CS | SPI_CLK | SPI_MOSI | SPI_MISO);
    GPIOEDE  |=  (SPI_CS | SPI_CLK | SPI_MOSI | SPI_MISO);
    GPIOEDIR &= ~(SPI_CS | SPI_CLK | SPI_MOSI);
    GPIOEDIR |=  SPI_MISO;
    GPIOESET  =  SPI_CS;
    GPIOECLR  =  SPI_CLK;
}

static u8 soft_spi_byte(u8 tx)
{
    u8 rx = 0;
    for (int i = 7; i >= 0; i--) {
        if (tx & (1 << i)) GPIOESET = SPI_MOSI;
        else               GPIOECLR = SPI_MOSI;
        delay_us(1);
        GPIOESET = SPI_CLK; delay_us(1);
        if (GPIOE & SPI_MISO) rx |= (1 << i);
        GPIOECLR = SPI_CLK; delay_us(1);
    }
    return rx;
}

static void w25q64_cs_low(void)  { GPIOECLR = SPI_CS; delay_us(1); }
static void w25q64_cs_high(void) { GPIOESET = SPI_CS; delay_us(1); }

static void w25q64_write_enable(void)
{
    w25q64_cs_low();
    soft_spi_byte(0x06);
    w25q64_cs_high();
}

static u8 w25q64_read_status(u8 cmd)
{
    u8 st;
    w25q64_cs_low();
    soft_spi_byte(cmd);
    st = soft_spi_byte(0xFF);
    w25q64_cs_high();
    return st;
}

static void w25q64_wait_busy(void)
{
    while (w25q64_read_status(0x05) & 0x01);
}

static void w25q64_read_data(u32 addr, u8 *buf, u32 len)
{
    w25q64_cs_low();
    soft_spi_byte(0x03);
    soft_spi_byte((addr >> 16) & 0xFF);
    soft_spi_byte((addr >> 8) & 0xFF);
    soft_spi_byte(addr & 0xFF);
    for (u32 i = 0; i < len; i++)
        buf[i] = soft_spi_byte(0xFF);
    w25q64_cs_high();
}

static void w25q64_page_program(u32 addr, u8 *buf, u32 len)
{
    w25q64_write_enable();
    w25q64_cs_low();
    soft_spi_byte(0x02);
    soft_spi_byte((addr >> 16) & 0xFF);
    soft_spi_byte((addr >> 8) & 0xFF);
    soft_spi_byte(addr & 0xFF);
    for (u32 i = 0; i < len; i++)
        soft_spi_byte(buf[i]);
    w25q64_cs_high();
    w25q64_wait_busy();
}

static void w25q64_sector_erase(u32 addr)
{
    w25q64_write_enable();
    w25q64_cs_low();
    soft_spi_byte(0x20);
    soft_spi_byte((addr >> 16) & 0xFF);
    soft_spi_byte((addr >> 8) & 0xFF);
    soft_spi_byte(addr & 0xFF);
    w25q64_cs_high();
    w25q64_wait_busy();
}

/* ================================================================
 *  实验 1: 读 JEDEC ID
 * ================================================================ */
void w25q64_exp1_jedec_id(void)
{
    printf("\n===== Exp1: JEDEC ID =====\n\n");
    w25q64_init();

    w25q64_cs_low();
    soft_spi_byte(0x9F);
    u8 id0 = soft_spi_byte(0xFF);
    u8 id1 = soft_spi_byte(0xFF);
    u8 id2 = soft_spi_byte(0xFF);
    w25q64_cs_high();

    printf("JEDEC ID: 0x%02X 0x%02X 0x%02X\n", id0, id1, id2);
    printf(id0 == 0xEF && id1 == 0x40 ? "MATCH!\n" : "FAILED\n");
    while (1);
}

/* ================================================================
 *  实验 2: 读 Status Register
 * ================================================================ */
void w25q64_exp2_status(void)
{
    printf("\n===== Exp2: Status Registers =====\n\n");
    w25q64_init();

    u8 sr1 = w25q64_read_status(0x05);
    u8 sr2 = w25q64_read_status(0x35);

    printf("SR1 (0x05): 0x%02X  BUSY=%d WEL=%d BP=%d\n",
           sr1, sr1 & 1, (sr1 >> 1) & 1, (sr1 >> 2) & 0xF);
    printf("SR2 (0x35): 0x%02X\n", sr2);

    printf("\nSend Write Enable (0x06), then re-read:\n");
    w25q64_write_enable();
    sr1 = w25q64_read_status(0x05);
    printf("SR1: 0x%02X  BUSY=%d WEL=%d (WEL should be 1 now)\n",
           sr1, sr1 & 1, (sr1 >> 1) & 1);

    while (1);
}

/* ================================================================
 *  实验 3: 页写入与读取
 * ================================================================ */
void w25q64_exp3_page_rw(void)
{
    printf("\n===== Exp3: Page Write & Read =====\n\n");
    w25q64_init();

    printf("Erasing Sector 0...\n");
    w25q64_sector_erase(0x000000);
    printf("Erase done.\n");

    u8 wbuf[256], rbuf[256];
    for (int i = 0; i < 256; i++) wbuf[i] = (u8)i;
    printf("Writing 256 bytes to Page 0...\n");
    w25q64_page_program(0x000000, wbuf, 256);

    printf("Reading back...\n");
    w25q64_read_data(0x000000, rbuf, 256);

    int err = 0;
    for (int i = 0; i < 256; i++)
        if (rbuf[i] != wbuf[i]) { err++; if (err <= 5) printf("ERR[%d]: w=0x%02X r=0x%02X\n", i, wbuf[i], rbuf[i]); }

    printf("Errors: %d / 256\n", err);
    printf(err == 0 ? "PASSED!\n" : "FAILED\n");
    while (1);
}

/* ================================================================
 *  实验 4: 跨页连续写入
 * ================================================================ */
void w25q64_exp4_cross_page(void)
{
    printf("\n===== Exp4: Cross-Page Write =====\n\n");
    w25q64_init();

    u32 addr = 0x0000F0;
    u32 len  = 100;

    w25q64_sector_erase(0x000000);

    u8 wbuf[100], rbuf[100];
    for (int i = 0; i < (int)len; i++) wbuf[i] = 0xAA + i;

    u32 page0_remain = 256 - (addr & 0xFF);
    printf("Addr 0x%06lX, len=%lu, page0_remain=%lu\n", addr, len, page0_remain);

    w25q64_page_program(addr, wbuf, page0_remain);
    w25q64_page_program(addr + page0_remain, wbuf + page0_remain, len - page0_remain);

    w25q64_read_data(addr, rbuf, len);

    int err = 0;
    for (int i = 0; i < (int)len; i++)
        if (rbuf[i] != wbuf[i]) { err++; if (err <= 5) printf("ERR[%d]: w=0x%02X r=0x%02X\n", i, wbuf[i], rbuf[i]); }

    printf("Errors: %d / %lu\n", err, len);
    printf(err == 0 ? "PASSED!\n" : "FAILED\n");
    while (1);
}

/* ================================================================
 *  实验 5: 扇区擦除与验证
 * ================================================================ */
void w25q64_exp5_sector_erase(void)
{
    printf("\n===== Exp5: Sector Erase & Verify =====\n\n");
    w25q64_init();

    u8 pat[256], buf[64];
    for (int i = 0; i < 256; i++) pat[i] = 0xA5;

    printf("Writing pattern to Sector 0...\n");
    w25q64_sector_erase(0x000000);
    w25q64_page_program(0x000000, pat, 256);

    w25q64_read_data(0x000000, buf, 64);
    printf("Before erase: buf[0]=0x%02X (expect 0xA5)\n", buf[0]);

    printf("Erasing Sector 0...\n");
    w25q64_sector_erase(0x000000);

    w25q64_read_data(0x000000, buf, 64);
    int all_ff = 1;
    for (int i = 0; i < 64; i++)
        if (buf[i] != 0xFF) { all_ff = 0; break; }

    printf("After erase: buf[0]=0x%02X, all_ff=%d\n", buf[0], all_ff);
    printf(all_ff ? "Erase PASSED!\n" : "FAILED\n");

    printf("\nRe-write and verify...\n");
    w25q64_page_program(0x000000, pat, 256);
    w25q64_read_data(0x000000, buf, 64);
    printf("After re-write: buf[0]=0x%02X (expect 0xA5)\n", buf[0]);
    while (1);
}

/* ================================================================
 *  实验 6: 擦除耗时对比
 * ================================================================ */
void w25q64_exp6_erase_timing(void)
{
    printf("\n===== Exp6: Erase Timing =====\n\n");
    w25q64_init();

    u32 t0, t1;

    printf("Sector Erase (0x20, 4KB)...\n");
    t0 = tick_get();
    w25q64_sector_erase(0x000000);
    t1 = tick_get();
    printf("  Time: %lu ms\n", (t1 - t0) / 1000);

    printf("Block Erase 32KB (0x52)...\n");
    w25q64_write_enable();
    w25q64_cs_low();
    soft_spi_byte(0x52); soft_spi_byte(0x00); soft_spi_byte(0x00); soft_spi_byte(0x00);
    w25q64_cs_high();
    t0 = tick_get(); w25q64_wait_busy(); t1 = tick_get();
    printf("  Time: %lu ms\n", (t1 - t0) / 1000);

    printf("Block Erase 64KB (0xD8)...\n");
    w25q64_write_enable();
    w25q64_cs_low();
    soft_spi_byte(0xD8); soft_spi_byte(0x00); soft_spi_byte(0x00); soft_spi_byte(0x00);
    w25q64_cs_high();
    t0 = tick_get(); w25q64_wait_busy(); t1 = tick_get();
    printf("  Time: %lu ms\n", (t1 - t0) / 1000);

    printf("Chip Erase (0xC7) — ~20 seconds, please wait...\n");
    w25q64_write_enable();
    w25q64_cs_low();
    soft_spi_byte(0xC7);
    w25q64_cs_high();
    t0 = tick_get(); w25q64_wait_busy(); t1 = tick_get();
    printf("  Time: %lu ms (~%lu s)\n", (t1 - t0) / 1000, (t1 - t0) / 1000000);

    printf("\nAll timing tests done.\n");
    while (1);
}

/* ================================================================
 *  实验 7: 写保护配置
 * ================================================================ */
void w25q64_exp7_write_protect(void)
{
    printf("\n===== Exp7: Write Protection =====\n\n");
    w25q64_init();

    u8 sr1 = w25q64_read_status(0x05);
    printf("Initial SR1: 0x%02X (BP=%d)\n", sr1, (sr1 >> 2) & 0xF);

    u8 new_sr1 = sr1 | (1 << 4);
    printf("Setting BP2=1...\n");
    w25q64_write_enable();
    w25q64_cs_low();
    soft_spi_byte(0x01);
    soft_spi_byte(new_sr1);
    soft_spi_byte(0x00);
    w25q64_cs_high();
    w25q64_wait_busy();

    sr1 = w25q64_read_status(0x05);
    printf("New SR1: 0x%02X (BP=%d)\n", sr1, (sr1 >> 2) & 0xF);

    printf("\nAttempting write to protected area (0x400000)...\n");
    w25q64_write_enable();
    w25q64_cs_low();
    soft_spi_byte(0x02);
    soft_spi_byte(0x40); soft_spi_byte(0x00); soft_spi_byte(0x00);
    soft_spi_byte(0x55);
    w25q64_cs_high();
    w25q64_wait_busy();

    sr1 = w25q64_read_status(0x05);
    printf("SR1 after write attempt: 0x%02X WEL=%d\n", sr1, (sr1 >> 1) & 1);

    printf("\nRemoving protection (BP=0)...\n");
    w25q64_write_enable();
    w25q64_cs_low();
    soft_spi_byte(0x01);
    soft_spi_byte(sr1 & ~0x3C);
    soft_spi_byte(0x00);
    w25q64_cs_high();
    w25q64_wait_busy();

    sr1 = w25q64_read_status(0x05);
    printf("Final SR1: 0x%02X (BP=%d)\n", sr1, (sr1 >> 2) & 0xF);
    while (1);
}

/* ================================================================
 *  实验 9: Unique ID + SFDP
 * ================================================================ */
void w25q64_exp9_unique_id(void)
{
    printf("\n===== Exp9: Unique ID & SFDP =====\n\n");
    w25q64_init();

    printf("Unique ID (0x4B): ");
    w25q64_cs_low();
    soft_spi_byte(0x4B);
    soft_spi_byte(0xFF); soft_spi_byte(0xFF);
    soft_spi_byte(0xFF); soft_spi_byte(0xFF);
    for (int i = 0; i < 8; i++)
        printf("%02X ", soft_spi_byte(0xFF));
    w25q64_cs_high();
    printf("\n");

    printf("SFDP Header (0x5A): ");
    w25q64_cs_low();
    soft_spi_byte(0x5A);
    soft_spi_byte(0x00); soft_spi_byte(0x00); soft_spi_byte(0x00);
    soft_spi_byte(0xFF);
    for (int i = 0; i < 16; i++)
        printf("%02X ", soft_spi_byte(0xFF));
    w25q64_cs_high();
    printf("\n(expect 53 46 44 50 = 'SFDP')\n");

    while (1);
}
