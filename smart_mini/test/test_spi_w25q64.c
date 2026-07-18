/**
 * @file    test_spi_w25q64.c
 * @brief   BT892X W25Q64 Flash 软/硬 SPI 合一套件（合自 copilot w25q64_test.c + hw_spi_w25q64.c）
 *
 *  接线：CS=PE4(GPIO), CLK=PE6, MOSI=PE7, MISO=PE5 → W25Q64
 *  引脚编号（共享硬件 SPI1 G4）：CS=PE4, CLK=PE6, DI=PE7, DO=PE5
 *
 *  软件实验：exp1, 2, 3, 4, 5, 6, 7, 9（共 8 个，copilot 没有 exp8 软件版）
 *  硬件实验：exp1, 2, 3, 4, 5, 6, 7, 8 (Fast Read), 9 (100K↔12M对比), 10 (UID+SFDP)（共 10 个）
 *
 *  编译宏控制：
 *    - SPI_W25_RUN_MODE: 0=只软(默认) / 1=只硬 / 2=软硬全跑
 *    - SPI_SW_W25_MODE  (软件版): 0=单 exp / 1=全测（含 Chip Erase，会清空 Flash） / 2=仅读
 *    - SPI_HW_W25_MODE  (硬件版): 0=单 exp / 1=全测 / 2=仅读
 *    - SPI_*_W25_EXP: 单 exp 模式下选 1..10 (HW) 或 1..7,9 (SW)
 *
 * 参考手册: BT892X_UserManual_Driver.md §3.2 GPIO、§3.3 FUNCMCON1、§7 SPI
 * 数据手册: W25Q64 命令/时序见 Winbond W25Q64 datasheet
 */

#include "test_spi_common.h"

#ifndef SPI_W25_RUN_MODE
#define SPI_W25_RUN_MODE 2
#endif
#ifndef SPI_SW_W25_MODE
#define SPI_SW_W25_MODE 1
#endif
#ifndef SPI_HW_W25_MODE
#define SPI_HW_W25_MODE 1
#endif
#ifndef SPI_SW_W25_EXP
#define SPI_SW_W25_EXP 1
#endif
#ifndef SPI_HW_W25_EXP
#define SPI_HW_W25_EXP 1
#endif

/* ===================== 公共底层 ===================== */
#define SPI_CS      SPI_CS_PIN
#define SPI_CLK     SPI_CLK_PIN
#define SPI_MOSI    SPI_MOSI_PIN
#define SPI_MISO    SPI_MISO_PIN

/* ===================== 软件 bit-bang ===================== */
static void sw_spi_init(void)
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

static void sw_cs_low(void)  { GPIOECLR = SPI_CS; delay_us(1); }
static void sw_cs_high(void) { GPIOESET = SPI_CS; delay_us(1); }

static void sw_write_enable(void)       { sw_cs_low(); soft_spi_byte(0x06); sw_cs_high(); }
static u8   sw_read_status(u8 cmd)      { u8 s; sw_cs_low(); soft_spi_byte(cmd); s = soft_spi_byte(0xFF); sw_cs_high(); return s; }
static void sw_wait_busy(void)          { while (sw_read_status(0x05) & 0x01); }

static void sw_read_data(u32 addr, u8 *buf, u32 len)
{
    sw_cs_low();
    soft_spi_byte(0x03);
    soft_spi_byte((addr>>16)&0xFF); soft_spi_byte((addr>>8)&0xFF); soft_spi_byte(addr&0xFF);
    for (u32 i = 0; i < len; i++) buf[i] = soft_spi_byte(0xFF);
    sw_cs_high();
}

static void sw_page_program(u32 addr, u8 *buf, u32 len)
{
    sw_write_enable();
    sw_cs_low();
    soft_spi_byte(0x02);
    soft_spi_byte((addr>>16)&0xFF); soft_spi_byte((addr>>8)&0xFF); soft_spi_byte(addr&0xFF);
    for (u32 i = 0; i < len; i++) soft_spi_byte(buf[i]);
    sw_cs_high();
    sw_wait_busy();
}

static void sw_sector_erase(u32 addr)
{
    sw_write_enable();
    sw_cs_low();
    soft_spi_byte(0x20);
    soft_spi_byte((addr>>16)&0xFF); soft_spi_byte((addr>>8)&0xFF); soft_spi_byte(addr&0xFF);
    sw_cs_high();
    sw_wait_busy();
}

/* ===================== 软件 实验 (8 个: 1-7 + 9) ===================== */
static void sw_exp1(void)
{
    printf("\n===== [Soft] Exp1: JEDEC ID =====\n");
    sw_spi_init();
    sw_cs_low(); soft_spi_byte(0x9F);
    u8 a = soft_spi_byte(0xFF), b = soft_spi_byte(0xFF), c = soft_spi_byte(0xFF);
    sw_cs_high();
    printf("JEDEC: 0x%02X 0x%02X 0x%02X %s\n", a, b, c,
           (a==0xEF && b==0x40) ? "MATCH" : "FAIL");
}

static void sw_exp2(void)
{
    printf("\n===== [Soft] Exp2: Status Registers =====\n");
    sw_spi_init();
    u8 sr1 = sw_read_status(0x05);
    u8 sr2 = sw_read_status(0x35);
    printf("SR1 (0x05): 0x%02X  BUSY=%d WEL=%d BP=%d\n",
           sr1, sr1 & 1, (sr1 >> 1) & 1, (sr1 >> 2) & 0xF);
    printf("SR2 (0x35): 0x%02X\n", sr2);
    printf("\nSend Write Enable (0x06), then re-read:\n");
    sw_write_enable();
    sr1 = sw_read_status(0x05);
    printf("SR1: 0x%02X  BUSY=%d WEL=%d (WEL should be 1)\n",
           sr1, sr1 & 1, (sr1 >> 1) & 1);
}

static void sw_exp3(void)
{
    printf("\n===== [Soft] Exp3: Page Write & Read =====\n");
    sw_spi_init();
    printf("Erasing Sector 0...\n");
    sw_sector_erase(0x000000);
    printf("Erase done.\n");
    u8 w[256], r[256]; for (int i = 0; i < 256; i++) w[i] = (u8)i;
    printf("Writing 256 bytes to Page 0...\n");
    sw_page_program(0x000000, w, 256);
    printf("Reading back...\n");
    sw_read_data(0x000000, r, 256);
    int err = 0;
    for (int i = 0; i < 256; i++)
        if (r[i] != w[i]) { err++; if (err <= 5) printf("ERR[%d]: w=0x%02X r=0x%02X\n", i, w[i], r[i]); }
    printf("Errors: %d / 256  %s\n", err, err ? "FAILED" : "PASSED");
}

static void sw_exp4(void)
{
    printf("\n===== [Soft] Exp4: Cross-Page Write =====\n");
    sw_spi_init();
    u32 addr = 0x0000F0;
    u32 len  = 100;
    sw_sector_erase(0x000000);
    u8 wbuf[100], rbuf[100];
    for (int i = 0; i < (int)len; i++) wbuf[i] = 0xAA + i;
    u32 page0_remain = 256 - (addr & 0xFF);
    printf("Addr 0x%06lX, len=%lu, page0_remain=%lu\n", addr, len, page0_remain);
    sw_page_program(addr, wbuf, page0_remain);
    sw_page_program(addr + page0_remain, wbuf + page0_remain, len - page0_remain);
    sw_read_data(addr, rbuf, len);
    int err = 0;
    for (int i = 0; i < (int)len; i++)
        if (rbuf[i] != wbuf[i]) { err++; if (err <= 5) printf("ERR[%d]: w=0x%02X r=0x%02X\n", i, wbuf[i], rbuf[i]); }
    printf("Errors: %d / %lu  %s\n", err, len, err ? "FAILED" : "PASSED");
}

static void sw_exp5(void)
{
    printf("\n===== [Soft] Exp5: Sector Erase & Verify =====\n");
    sw_spi_init();
    u8 pat[256], buf[64];
    for (int i = 0; i < 256; i++) pat[i] = 0xA5;
    printf("Writing pattern 0xA5 to Sector 0...\n");
    sw_sector_erase(0x000000);
    sw_page_program(0x000000, pat, 256);
    sw_read_data(0x000000, buf, 64);
    printf("Before erase: buf[0]=0x%02X (expect 0xA5)\n", buf[0]);
    printf("Erasing Sector 0...\n");
    sw_sector_erase(0x000000);
    sw_read_data(0x000000, buf, 64);
    int all_ff = 1;
    for (int i = 0; i < 64; i++)
        if (buf[i] != 0xFF) { all_ff = 0; break; }
    printf("After erase: buf[0]=0x%02X, all_ff=%d  %s\n",
           buf[0], all_ff, all_ff ? "PASSED" : "FAILED");
    printf("\nRe-write pattern...\n");
    sw_page_program(0x000000, pat, 256);
    sw_read_data(0x000000, buf, 64);
    printf("After re-write: buf[0]=0x%02X (expect 0xA5)\n", buf[0]);
}

static void sw_exp6(void)
{
    printf("\n===== [Soft] Exp6: Erase Timing =====\n");
    sw_spi_init();
    u32 t0, t1;
    printf("Sector 4KB (0x20):\n");
    t0 = tick_get(); sw_sector_erase(0x000000); t1 = tick_get();
    printf("  Time: %lu ms\n", (t1 - t0) / 1000);
    printf("Block 32KB (0x52):\n");
    sw_write_enable();
    sw_cs_low();
    soft_spi_byte(0x52); soft_spi_byte(0x00); soft_spi_byte(0x00); soft_spi_byte(0x00);
    sw_cs_high();
    t0 = tick_get(); sw_wait_busy(); t1 = tick_get();
    printf("  Time: %lu ms\n", (t1 - t0) / 1000);
    printf("Block 64KB (0xD8):\n");
    sw_write_enable();
    sw_cs_low();
    soft_spi_byte(0xD8); soft_spi_byte(0x00); soft_spi_byte(0x00); soft_spi_byte(0x00);
    sw_cs_high();
    t0 = tick_get(); sw_wait_busy(); t1 = tick_get();
    printf("  Time: %lu ms\n", (t1 - t0) / 1000);
    printf("Chip Erase (0xC7) -- ~20 seconds, please wait...\n");
    sw_write_enable();
    sw_cs_low();
    soft_spi_byte(0xC7);
    sw_cs_high();
    t0 = tick_get(); sw_wait_busy(); t1 = tick_get();
    printf("  Time: %lu ms (~%lu s)\n", (t1 - t0) / 1000, (t1 - t0) / 1000000);
}

static void sw_exp7(void)
{
    printf("\n===== [Soft] Exp7: Write Protection =====\n");
    sw_spi_init();
    u8 sr1 = sw_read_status(0x05);
    printf("Initial SR1: 0x%02X (BP=%d)\n", sr1, (sr1 >> 2) & 0xF);

    u8 new_sr1 = sr1 | (1 << 4);
    printf("Setting BP2=1 (Write Status Reg 0x01)...\n");
    sw_write_enable();
    sw_cs_low();
    soft_spi_byte(0x01);
    soft_spi_byte(new_sr1);
    soft_spi_byte(0x00);
    sw_cs_high();
    sw_wait_busy();
    sr1 = sw_read_status(0x05);
    printf("New SR1: 0x%02X (BP=%d)\n", sr1, (sr1 >> 2) & 0xF);

    printf("\nAttempting write to protected area 0x400000...\n");
    sw_write_enable();
    sw_cs_low();
    soft_spi_byte(0x02);
    soft_spi_byte(0x40); soft_spi_byte(0x00); soft_spi_byte(0x00);
    soft_spi_byte(0x55);
    sw_cs_high();
    sw_wait_busy();
    sr1 = sw_read_status(0x05);
    printf("SR1 after write attempt: 0x%02X WEL=%d\n", sr1, (sr1 >> 1) & 1);
    printf("(WEL=0 means write was rejected -- protection works)\n");

    printf("\nRemoving protection (BP=0)...\n");
    sw_write_enable();
    sw_cs_low();
    soft_spi_byte(0x01);
    soft_spi_byte(sr1 & ~0x3C);
    soft_spi_byte(0x00);
    sw_cs_high();
    sw_wait_busy();
    sr1 = sw_read_status(0x05);
    printf("Final SR1: 0x%02X (BP=%d)\n", sr1, (sr1 >> 2) & 0xF);
}

static void sw_exp9(void)
{
    printf("\n===== [Soft] Exp9: Unique ID & SFDP =====\n");
    sw_spi_init();
    printf("Unique ID (0x4B): ");
    sw_cs_low();
    soft_spi_byte(0x4B);
    soft_spi_byte(0xFF); soft_spi_byte(0xFF);
    soft_spi_byte(0xFF); soft_spi_byte(0xFF);
    for (int i = 0; i < 8; i++) printf("%02X ", soft_spi_byte(0xFF));
    sw_cs_high();
    printf("\n");
    printf("SFDP Header (0x5A): ");
    sw_cs_low();
    soft_spi_byte(0x5A);
    soft_spi_byte(0x00); soft_spi_byte(0x00); soft_spi_byte(0x00);
    soft_spi_byte(0xFF);
    for (int i = 0; i < 16; i++) printf("%02X ", soft_spi_byte(0xFF));
    sw_cs_high();
    printf("\n(expect first 4 bytes: 53 46 44 50 = 'SFDP')\n");
}

/* ===================== 软件 suite dispatcher ===================== */
static void run_soft_suite(void)
{
    printf("\n##### [Soft SPI] MODE=%d #####\n", SPI_SW_W25_MODE);
#if SPI_SW_W25_MODE == 1
    sw_exp1(); delay_ms(200);
    sw_exp2(); delay_ms(200);
    sw_exp3(); delay_ms(200);
    sw_exp4(); delay_ms(200);
    sw_exp5(); delay_ms(200);
    sw_exp6(); delay_ms(200);   // 含 Chip Erase ~20s
    sw_exp7(); delay_ms(200);
    sw_exp9();
#elif SPI_SW_W25_MODE == 2
    sw_exp1(); delay_ms(200);
    sw_exp9();
#else
    switch (SPI_SW_W25_EXP) {
        case 1: sw_exp1(); break;
        case 2: sw_exp2(); break;
        case 3: sw_exp3(); break;
        case 4: sw_exp4(); break;
        case 5: sw_exp5(); break;
        case 6: sw_exp6(); break;
        case 7: sw_exp7(); break;
        case 9: sw_exp9(); break;
    }
#endif
}

/* ===================== 硬件 SPI1 ===================== */
static void hw_spi_init(u32 baud)
{
    FUNCMCON1 &= ~(0xF << 12);
    FUNCMCON1 |=  (0x4 << 12);
    GPIOEFEN |= SPI_CLK | SPI_MOSI | SPI_MISO;
    GPIOEDE  |= SPI_CLK | SPI_MOSI | SPI_MISO;
    GPIOEDIR &= ~(SPI_CLK | SPI_MOSI);
    GPIOEDIR |=  SPI_MISO;
    GPIOEFEN &= ~SPI_CS;  GPIOEDE |= SPI_CS;
    GPIOEDIR &= ~SPI_CS;  GPIOESET = SPI_CS;
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

static void hw_cs_low(void)  { GPIOECLR = SPI_CS; delay_us(1); }
static void hw_cs_high(void) { GPIOESET = SPI_CS; delay_us(1); }

static void hw_write_enable(void)       { hw_cs_low(); hw_spi_byte(0x06); hw_cs_high(); }
static u8   hw_read_status(u8 cmd)      { u8 s; hw_cs_low(); hw_spi_byte(cmd); s = hw_spi_byte(0xFF); hw_cs_high(); return s; }
static void hw_wait_busy(void)          { while (hw_read_status(0x05) & 1); }

static void hw_read_data(u32 addr, u8 *buf, u32 len)
{
    hw_cs_low(); hw_spi_byte(0x03);
    hw_spi_byte((addr>>16)&0xFF); hw_spi_byte((addr>>8)&0xFF); hw_spi_byte(addr&0xFF);
    for (u32 i = 0; i < len; i++) buf[i] = hw_spi_byte(0xFF);
    hw_cs_high();
}

static void hw_page_program(u32 addr, u8 *buf, u32 len)
{
    hw_write_enable(); hw_cs_low(); hw_spi_byte(0x02);
    hw_spi_byte((addr>>16)&0xFF); hw_spi_byte((addr>>8)&0xFF); hw_spi_byte(addr&0xFF);
    for (u32 i = 0; i < len; i++) hw_spi_byte(buf[i]);
    hw_cs_high(); hw_wait_busy();
}

static void hw_sector_erase(u32 addr)
{
    hw_write_enable(); hw_cs_low(); hw_spi_byte(0x20);
    hw_spi_byte((addr>>16)&0xFF); hw_spi_byte((addr>>8)&0xFF); hw_spi_byte(addr&0xFF);
    hw_cs_high(); hw_wait_busy();
}

/* ===================== 硬件 实验 (10 个: 1-10) ===================== */
static void hw_exp1(void)
{
    printf("\n===== [HW] Exp1: JEDEC ID =====\n");
    hw_spi_init(239);
    hw_cs_low(); hw_spi_byte(0x9F);
    u8 a = hw_spi_byte(0xFF), b = hw_spi_byte(0xFF), c = hw_spi_byte(0xFF);
    hw_cs_high();
    printf("JEDEC: 0x%02X 0x%02X 0x%02X %s\n", a, b, c, a==0xEF ? "OK" : "FAIL");
}

static void hw_exp2(void)
{
    printf("\n===== [HW] Exp2: Status Registers =====\n");
    hw_spi_init(239);
    u8 s1 = hw_read_status(0x05), s2 = hw_read_status(0x35);
    printf("SR1: 0x%02X (BUSY=%d WEL=%d BP=%d)  SR2: 0x%02X\n",
           s1, s1&1, (s1>>1)&1, (s1>>2)&0xF, s2);
    hw_write_enable();
    s1 = hw_read_status(0x05);
    printf("After WE: SR1=0x%02X WEL=%d\n", s1, (s1>>1)&1);
}

static void hw_exp3(void)
{
    printf("\n===== [HW] Exp3: Page Write & Read =====\n");
    hw_spi_init(239);
    hw_sector_erase(0);
    u8 w[256], r[256]; for (int i=0;i<256;i++) w[i]=i;
    hw_page_program(0, w, 256);
    hw_read_data(0, r, 256);
    int e=0; for (int i=0;i<256;i++) if(r[i]!=w[i]) e++;
    printf("Errors: %d/256  %s\n", e, e?"FAIL":"PASSED");
}

static void hw_exp4(void)
{
    printf("\n===== [HW] Exp4: Cross-Page Write =====\n");
    hw_spi_init(239);
    u32 addr=0xF0, len=100, r=256-(addr&0xFF);
    hw_sector_erase(0);
    u8 w[100], b[100]; for (int i=0;i<100;i++) w[i]=0xAA+i;
    hw_page_program(addr, w, r);
    hw_page_program(addr+r, w+r, len-r);
    hw_read_data(addr, b, len);
    int e=0; for (int i=0;i<100;i++) if(b[i]!=w[i]) e++;
    printf("Errors: %d/100  %s\n", e, e?"FAIL":"PASSED");
}

static void hw_exp5(void)
{
    printf("\n===== [HW] Exp5: Sector Erase & Verify =====\n");
    hw_spi_init(239);
    u8 p[256], b[64]; for (int i=0;i<256;i++) p[i]=0xA5;
    hw_sector_erase(0); hw_page_program(0, p, 256);
    hw_read_data(0, b, 64); printf("Before: buf[0]=0x%02X (expect A5)\n", b[0]);
    hw_sector_erase(0);
    hw_read_data(0, b, 64);
    int f=1; for (int i=0;i<64;i++) if(b[i]!=0xFF) f=0;
    printf("After:  buf[0]=0x%02X all_ff=%d  %s\n", b[0], f, f?"PASSED":"FAIL");
    hw_page_program(0, p, 256);
    hw_read_data(0, b, 64); printf("Re-write: buf[0]=0x%02X\n", b[0]);
}

static void hw_exp6(void)
{
    printf("\n===== [HW] Exp6: Erase Timing =====\n");
    hw_spi_init(239); u32 t0,t1;
    printf("Sector 4KB (0x20): "); t0=tick_get(); hw_sector_erase(0); t1=tick_get();
    printf("%lu ms\n", (t1-t0)/1000);
    printf("Block 32KB (0x52): "); hw_write_enable(); hw_cs_low();
    hw_spi_byte(0x52); hw_spi_byte(0);hw_spi_byte(0);hw_spi_byte(0); hw_cs_high();
    t0=tick_get(); hw_wait_busy(); t1=tick_get(); printf("%lu ms\n", (t1-t0)/1000);
    printf("Block 64KB (0xD8): "); hw_write_enable(); hw_cs_low();
    hw_spi_byte(0xD8); hw_spi_byte(0);hw_spi_byte(0);hw_spi_byte(0); hw_cs_high();
    t0=tick_get(); hw_wait_busy(); t1=tick_get(); printf("%lu ms\n", (t1-t0)/1000);
    printf("Chip Erase (0xC7) -- ~20s, please wait...\n");
    hw_write_enable(); hw_cs_low();
    hw_spi_byte(0xC7); hw_cs_high();
    t0=tick_get(); hw_wait_busy(); t1=tick_get();
    printf("%lu ms (~%lus)\n", (t1-t0)/1000, (t1-t0)/1000000);
}

static void hw_exp7(void)
{
    printf("\n===== [HW] Exp7: Write Protection =====\n");
    hw_spi_init(239);
    u8 s=hw_read_status(0x05);
    printf("Init SR1: 0x%02X BP=%d\n", s, (s>>2)&0xF);
    hw_write_enable(); hw_cs_low(); hw_spi_byte(0x01);
    hw_spi_byte(s|0x10); hw_spi_byte(0); hw_cs_high(); hw_wait_busy();
    s=hw_read_status(0x05); printf("Set BP2: 0x%02X BP=%d\n", s, (s>>2)&0xF);
    printf("\nAttempting write to protected area (0x400000)...\n");
    hw_write_enable(); hw_cs_low(); hw_spi_byte(0x02);
    hw_spi_byte(0x40);hw_spi_byte(0);hw_spi_byte(0); hw_spi_byte(0x55);
    hw_cs_high(); hw_wait_busy();
    s=hw_read_status(0x05); printf("Write protected area: SR1=0x%02X WEL=%d\n", s, (s>>1)&1);
    printf("(WEL=0 means write rejected - protection works)\n");
    printf("\nRemoving protection (BP=0)...\n");
    hw_write_enable(); hw_cs_low(); hw_spi_byte(0x01);
    hw_spi_byte(s&~0x3C); hw_spi_byte(0); hw_cs_high(); hw_wait_busy();
    s=hw_read_status(0x05); printf("Removed: SR1=0x%02X BP=%d\n", s, (s>>2)&0xF);
}

/* Exp 8: Fast Read 0x0B vs Standard Read 0x03 速度对比 */
static void hw_exp8(void)
{
    printf("\n===== [HW] Exp8: Fast Read Speed (0x03 vs 0x0B) =====\n");
    hw_spi_init(239);
    #define FRB 2048
    u8 buf[FRB];
    u32 t0;
    (void)buf;     // 仅做时间测量，不读 buffer

    t0 = tick_get();
    hw_cs_low(); hw_spi_byte(0x03);
    hw_spi_byte(0); hw_spi_byte(0); hw_spi_byte(0);
    for (int i = 0; i < FRB; i++) (void)hw_spi_byte(0xFF);
    hw_cs_high();
    u32 t_std = tick_get() - t0;

    t0 = tick_get();
    hw_cs_low(); hw_spi_byte(0x0B);
    hw_spi_byte(0); hw_spi_byte(0); hw_spi_byte(0);
    hw_spi_byte(0xFF);    // dummy byte (Fast Read 要求)
    for (int i = 0; i < FRB; i++) (void)hw_spi_byte(0xFF);
    hw_cs_high();
    u32 t_fast = tick_get() - t0;

    printf("Standard Read (0x03): %lu ticks\n", t_std);
    printf("Fast Read    (0x0B): %lu ticks (with 1 dummy byte)\n", t_fast);
    printf("Diff: %ld ticks (Fast-Standard)\n", (s32)(t_fast - t_std));
    printf("(At 100kHz speeds similar; at high clock Fast Read wins)\n");
    #undef FRB
}

/* Exp 9: 100 kHz vs 12 MHz 读 4096B 对比 */
static void hw_exp9(void)
{
    printf("\n===== [HW] Exp9: 100K vs 12MHz Read Speed =====\n");
    hw_spi_init(239);
    #define BLK 4096
    u8 buf[BLK];
    u32 t0, t_poll;
    (void)buf;     // 仅做时间测量

    // 100 kHz
    SPI1BAUD = 239;
    t0 = tick_get();
    hw_cs_low(); hw_spi_byte(0x03);
    hw_spi_byte(0); hw_spi_byte(0); hw_spi_byte(0);
    for (int i=0;i<BLK;i++) buf[i]=hw_spi_byte(0xFF);
    hw_cs_high();
    printf("100kHz Standard Read: %lu ticks\n", tick_get()-t0);

    // 12 MHz
    SPI1BAUD = 1;
    t0 = tick_get();
    hw_cs_low(); hw_spi_byte(0x03);
    hw_spi_byte(0); hw_spi_byte(0); hw_spi_byte(0);
    for (int i=0;i<BLK;i++) (void)hw_spi_byte(0xFF);
    hw_cs_high();
    t_poll = tick_get() - t0;
    printf("12MHz Standard Read: %lu ticks\n", t_poll);

    printf("12MHz bit period = 2/24M = 83ns; 4096 bytes = %lu us\n", (u32)(((u64)BLK * 8 * 2) / 24));
    SPI1BAUD = 239;  // 恢复低速
    #undef BLK
}

/* Exp 10: Unique ID + SFDP */
static void hw_exp10(void)
{
    printf("\n===== [HW] Exp10: Unique ID & SFDP =====\n");
    hw_spi_init(239);
    printf("UID: "); hw_cs_low(); hw_spi_byte(0x4B);
    hw_spi_byte(0xFF); hw_spi_byte(0xFF); hw_spi_byte(0xFF); hw_spi_byte(0xFF);
    for (int i=0;i<8;i++) printf("%02X ", hw_spi_byte(0xFF));
    hw_cs_high();
    printf("\nSFDP: "); hw_cs_low(); hw_spi_byte(0x5A);
    hw_spi_byte(0); hw_spi_byte(0); hw_spi_byte(0);
    hw_spi_byte(0xFF);
    for (int i=0;i<16;i++) printf("%02X ", hw_spi_byte(0xFF));
    hw_cs_high();
    printf("\n(expect first 4 bytes: 53 46 44 50 = 'SFDP')\n");
}

/* ===================== 硬件 suite dispatcher ===================== */
static void run_hw_suite(void)
{
    printf("\n##### [HW SPI] MODE=%d #####\n", SPI_HW_W25_MODE);
#if SPI_HW_W25_MODE == 1
    hw_exp1(); delay_ms(200);
    hw_exp2(); delay_ms(200);
    hw_exp3(); delay_ms(200);
    hw_exp4(); delay_ms(200);
    hw_exp5(); delay_ms(200);
    hw_exp6(); delay_ms(200);   // 含 Chip Erase ~20s
    hw_exp7(); delay_ms(200);
    hw_exp8(); delay_ms(200);
    hw_exp9(); delay_ms(200);
    hw_exp10();
#elif SPI_HW_W25_MODE == 2
    hw_exp1(); delay_ms(200);
    hw_exp10();
#else
    switch (SPI_HW_W25_EXP) {
        case 1:  hw_exp1();  break;
        case 2:  hw_exp2();  break;
        case 3:  hw_exp3();  break;
        case 4:  hw_exp4();  break;
        case 5:  hw_exp5();  break;
        case 6:  hw_exp6();  break;
        case 7:  hw_exp7();  break;
        case 8:  hw_exp8();  break;
        case 9:  hw_exp9();  break;
        case 10: hw_exp10(); break;
    }
#endif
}

/* ===================== 入口 ===================== */
void test_spi_w25q64_run(void)
{
    printf("\n===== BT892X W25Q64 Test (Soft + HW SPI) =====\n");
    printf("Wiring: CS=PE4, CLK=PE6, DI=PE7, DO=PE5 -> W25Q64\n\n");
    printf("NOTE: SPI_SW_W25_MODE=%d, SPI_HW_W25_MODE=%d, SPI_W25_RUN_MODE=%d\n",
           SPI_SW_W25_MODE, SPI_HW_W25_MODE, SPI_W25_RUN_MODE);

#if SPI_W25_RUN_MODE == 1
    run_hw_suite();
#elif SPI_W25_RUN_MODE == 2
    run_soft_suite();
    delay_ms(500);
    printf("\n##### Switching to HW SPI #####\n");
    run_hw_suite();
#else
    run_soft_suite();
#endif

    printf("\n===== W25Q64 Test DONE =====\n");
    while (1);
}
