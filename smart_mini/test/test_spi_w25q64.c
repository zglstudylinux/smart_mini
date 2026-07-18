/**
 * @file    test_spi_w25q64.c
 * @brief   BT892X W25Q64 Flash 软/硬 SPI 合一套件 —— 重构版
 *         调用 spi_hal.h 的 W25Q64 驱动 + 原语，消除重复代码
 *
 *  接线：CS=PE4(GPIO), CLK=PE6, MOSI=PE7, MISO=PE5 → W25Q64
 *
 *  软件实验：exp1, 2, 3, 4, 5, 6, 7, 9（8 个；copilot 没有 exp8 软件版）
 *  硬件实验：exp1, 2, 3, 4, 5, 6, 7, 8 (Fast Read), 9 (100K↔12M对比), 10 (UID+SFDP)（10 个）
 *
 *  编译宏（保持原行为）：
 *    - SPI_W25_RUN_MODE: 0=只软(默认) / 1=只硬 / 2=软硬全跑
 *    - SPI_SW_W25_MODE  (软件版): 0=默认单 exp / 1=全测（含 Chip Erase） / 2=仅读
 *    - SPI_HW_W25_MODE  (硬件版): 0=默认单 exp / 1=全测 / 2=仅读
 *    - SPI_*_W25_EXP: 单 exp 模式下选 1..10 (HW) 或 1..7,9 (SW)
 *
 * 参考手册: BT892X_UserManual_Driver.md §3.2 GPIO、§3.3 FUNCMCON1、§7 SPI
 * 数据手册: W25Q64 命令/时序见 Winbond datasheet
 */

#include "spi_hal.h"

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

/* ===================== 软件 实验 (8 个: 1-7 + 9) ===================== */
static void sw_exp1(void)
{
    u8 a, b, c;
    printf("\n===== [Soft] Exp1: JEDEC ID =====\n");
    spi_hal_soft_init();
    spi_hal_cs_low(); spi_hal_soft_byte(0x9F);
    a = spi_hal_soft_byte(0xFF);
    b = spi_hal_soft_byte(0xFF);
    c = spi_hal_soft_byte(0xFF);
    spi_hal_cs_high();
    printf("JEDEC: 0x%02X 0x%02X 0x%02X %s\n", a, b, c,
           (a==0xEF && b==0x40) ? "MATCH" : "FAIL");
}

static void sw_exp2(void)
{
    u8 sr1, sr2;
    printf("\n===== [Soft] Exp2: Status Registers =====\n");
    spi_hal_soft_init();
    sr1 = spi_hal_w25_sw_read_status(0x05);
    sr2 = spi_hal_w25_sw_read_status(0x35);
    printf("SR1 (0x05): 0x%02X  BUSY=%d WEL=%d BP=%d\n",
           sr1, sr1 & 1, (sr1 >> 1) & 1, (sr1 >> 2) & 0xF);
    printf("SR2 (0x35): 0x%02X\n", sr2);
    printf("\nSend Write Enable (0x06), then re-read:\n");
    spi_hal_w25_sw_write_enable();
    sr1 = spi_hal_w25_sw_read_status(0x05);
    printf("SR1: 0x%02X  BUSY=%d WEL=%d (WEL should be 1)\n",
           sr1, sr1 & 1, (sr1 >> 1) & 1);
}

static void sw_exp3(void)
{
    u8 w[256], r[256];
    int err;
    printf("\n===== [Soft] Exp3: Page Write & Read =====\n");
    spi_hal_soft_init();
    printf("Erasing Sector 0...\n");
    spi_hal_w25_sw_sector_erase(0x000000);
    printf("Erase done.\n");
    {
        int i;
        for (i = 0; i < 256; i++) w[i] = (u8)i;
    }
    printf("Writing 256 bytes to Page 0...\n");
    spi_hal_w25_sw_page_program(0x000000, w, 256);
    printf("Reading back...\n");
    spi_hal_w25_sw_read_data(0x000000, r, 256);
    err = 0;
    {
        int i;
        for (i = 0; i < 256; i++)
            if (r[i] != w[i]) { err++; if (err <= 5) printf("ERR[%d]: w=0x%02X r=0x%02X\n", i, w[i], r[i]); }
    }
    printf("Errors: %d / 256  %s\n", err, err ? "FAILED" : "PASSED");
}

static void sw_exp4(void)
{
    u32 addr = 0x0000F0;
    u32 len  = 100;
    u8 wbuf[100], rbuf[100];
    int err;
    printf("\n===== [Soft] Exp4: Cross-Page Write =====\n");
    spi_hal_soft_init();
    spi_hal_w25_sw_sector_erase(0x000000);
    {
        int i;
        for (i = 0; i < (int)len; i++) wbuf[i] = 0xAA + i;
    }
    {
        u32 page0_remain = 256 - (addr & 0xFF);
        printf("Addr 0x%06lX, len=%lu, page0_remain=%lu\n", addr, len, page0_remain);
        spi_hal_w25_sw_page_program(addr, wbuf, page0_remain);
        spi_hal_w25_sw_page_program(addr + page0_remain,
                                   wbuf + page0_remain, len - page0_remain);
    }
    spi_hal_w25_sw_read_data(addr, rbuf, len);
    err = 0;
    {
        int i;
        for (i = 0; i < (int)len; i++)
            if (rbuf[i] != wbuf[i]) { err++; if (err <= 5) printf("ERR[%d]: w=0x%02X r=0x%02X\n", i, wbuf[i], rbuf[i]); }
    }
    printf("Errors: %d / %lu  %s\n", err, len, err ? "FAILED" : "PASSED");
}

static void sw_exp5(void)
{
    u8 pat[256], buf[64];
    int all_ff;
    printf("\n===== [Soft] Exp5: Sector Erase & Verify =====\n");
    spi_hal_soft_init();
    {
        int i;
        for (i = 0; i < 256; i++) pat[i] = 0xA5;
    }
    printf("Writing pattern 0xA5 to Sector 0...\n");
    spi_hal_w25_sw_sector_erase(0x000000);
    spi_hal_w25_sw_page_program(0x000000, pat, 256);
    spi_hal_w25_sw_read_data(0x000000, buf, 64);
    printf("Before erase: buf[0]=0x%02X (expect 0xA5)\n", buf[0]);
    printf("Erasing Sector 0...\n");
    spi_hal_w25_sw_sector_erase(0x000000);
    spi_hal_w25_sw_read_data(0x000000, buf, 64);
    all_ff = 1;
    {
        int i;
        for (i = 0; i < 64; i++)
            if (buf[i] != 0xFF) { all_ff = 0; break; }
    }
    printf("After erase: buf[0]=0x%02X, all_ff=%d  %s\n",
           buf[0], all_ff, all_ff ? "PASSED" : "FAILED");
    printf("\nRe-write pattern...\n");
    spi_hal_w25_sw_page_program(0x000000, pat, 256);
    spi_hal_w25_sw_read_data(0x000000, buf, 64);
    printf("After re-write: buf[0]=0x%02X (expect 0xA5)\n", buf[0]);
}

static void sw_exp6(void)
{
    u32 t0, t1;
    printf("\n===== [Soft] Exp6: Erase Timing =====\n");
    spi_hal_soft_init();
    printf("Sector 4KB (0x20):\n");
    t0 = tick_get(); spi_hal_w25_sw_sector_erase(0x000000); t1 = tick_get();
    printf("  Time: %lu ms\n", (t1 - t0) / 1000);
    printf("Block 32KB (0x52):\n");
    spi_hal_w25_sw_write_enable();
    spi_hal_cs_low();
    spi_hal_soft_byte(0x52);
    spi_hal_soft_byte(0x00); spi_hal_soft_byte(0x00); spi_hal_soft_byte(0x00);
    spi_hal_cs_high();
    t0 = tick_get(); spi_hal_w25_sw_wait_busy(); t1 = tick_get();
    printf("  Time: %lu ms\n", (t1 - t0) / 1000);
    printf("Block 64KB (0xD8):\n");
    spi_hal_w25_sw_write_enable();
    spi_hal_cs_low();
    spi_hal_soft_byte(0xD8);
    spi_hal_soft_byte(0x00); spi_hal_soft_byte(0x00); spi_hal_soft_byte(0x00);
    spi_hal_cs_high();
    t0 = tick_get(); spi_hal_w25_sw_wait_busy(); t1 = tick_get();
    printf("  Time: %lu ms\n", (t1 - t0) / 1000);
    printf("Chip Erase (0xC7) -- ~20 seconds, please wait...\n");
    spi_hal_w25_sw_write_enable();
    spi_hal_cs_low();
    spi_hal_soft_byte(0xC7);
    spi_hal_cs_high();
    t0 = tick_get(); spi_hal_w25_sw_wait_busy(); t1 = tick_get();
    printf("  Time: %lu ms (~%lu s)\n", (t1 - t0) / 1000, (t1 - t0) / 1000000);
}

static void sw_exp7(void)
{
    u8 sr1, new_sr1;
    printf("\n===== [Soft] Exp7: Write Protection =====\n");
    spi_hal_soft_init();
    sr1 = spi_hal_w25_sw_read_status(0x05);
    printf("Initial SR1: 0x%02X (BP=%d)\n", sr1, (sr1 >> 2) & 0xF);
    new_sr1 = sr1 | (1 << 4);
    printf("Setting BP2=1 (Write Status Reg 0x01)...\n");
    spi_hal_w25_sw_write_enable();
    spi_hal_cs_low();
    spi_hal_soft_byte(0x01);
    spi_hal_soft_byte(new_sr1);
    spi_hal_soft_byte(0x00);
    spi_hal_cs_high();
    spi_hal_w25_sw_wait_busy();
    sr1 = spi_hal_w25_sw_read_status(0x05);
    printf("New SR1: 0x%02X (BP=%d)\n", sr1, (sr1 >> 2) & 0xF);

    printf("\nAttempting write to protected area 0x400000...\n");
    spi_hal_w25_sw_write_enable();
    spi_hal_cs_low();
    spi_hal_soft_byte(0x02);
    spi_hal_soft_byte(0x40); spi_hal_soft_byte(0x00); spi_hal_soft_byte(0x00);
    spi_hal_soft_byte(0x55);
    spi_hal_cs_high();
    spi_hal_w25_sw_wait_busy();
    sr1 = spi_hal_w25_sw_read_status(0x05);
    printf("SR1 after write attempt: 0x%02X WEL=%d\n", sr1, (sr1 >> 1) & 1);
    printf("(WEL=0 means write was rejected -- protection works)\n");

    printf("\nRemoving protection (BP=0)...\n");
    spi_hal_w25_sw_write_enable();
    spi_hal_cs_low();
    spi_hal_soft_byte(0x01);
    spi_hal_soft_byte(sr1 & ~0x3C);
    spi_hal_soft_byte(0x00);
    spi_hal_cs_high();
    spi_hal_w25_sw_wait_busy();
    sr1 = spi_hal_w25_sw_read_status(0x05);
    printf("Final SR1: 0x%02X (BP=%d)\n", sr1, (sr1 >> 2) & 0xF);
}

static void sw_exp9(void)
{
    int i;
    printf("\n===== [Soft] Exp9: Unique ID & SFDP =====\n");
    spi_hal_soft_init();
    printf("Unique ID (0x4B): ");
    spi_hal_cs_low();
    spi_hal_soft_byte(0x4B);
    spi_hal_soft_byte(0xFF); spi_hal_soft_byte(0xFF);
    spi_hal_soft_byte(0xFF); spi_hal_soft_byte(0xFF);
    for (i = 0; i < 8; i++) printf("%02X ", spi_hal_soft_byte(0xFF));
    spi_hal_cs_high();
    printf("\n");
    printf("SFDP Header (0x5A): ");
    spi_hal_cs_low();
    spi_hal_soft_byte(0x5A);
    spi_hal_soft_byte(0x00); spi_hal_soft_byte(0x00); spi_hal_soft_byte(0x00);
    spi_hal_soft_byte(0xFF);
    for (i = 0; i < 16; i++) printf("%02X ", spi_hal_soft_byte(0xFF));
    spi_hal_cs_high();
    printf("\n(expect first 4 bytes: 53 46 44 50 = 'SFDP')\n");
}

/* ===================== 软件 suite dispatcher ===================== */
static void run_soft_suite(void)
{
    printf("\n##### [Soft SPI] MODE=%d #####\n", SPI_SW_W25_MODE);
    spi_hal_soft_init();
#if SPI_SW_W25_MODE == 1
    sw_exp1(); delay_ms(200);
    sw_exp2(); delay_ms(200);
    sw_exp3(); delay_ms(200);
    sw_exp4(); delay_ms(200);
    sw_exp5(); delay_ms(200);
    sw_exp6(); delay_ms(200);
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

/* ===================== 硬件 实验 (10 个: 1-10) ===================== */
static void hw_exp1(void)
{
    u8 a, b, c;
    printf("\n===== [HW] Exp1: JEDEC ID =====\n");
    spi_hal_hw_init(239);
    spi_hal_cs_low(); spi_hal_hw_byte(0x9F);
    a = spi_hal_hw_byte(0xFF);
    b = spi_hal_hw_byte(0xFF);
    c = spi_hal_hw_byte(0xFF);
    spi_hal_cs_high();
    printf("JEDEC: 0x%02X 0x%02X 0x%02X %s\n", a, b, c, a==0xEF ? "OK" : "FAIL");
}

static void hw_exp2(void)
{
    u8 s1, s2;
    printf("\n===== [HW] Exp2: Status Registers =====\n");
    spi_hal_hw_init(239);
    s1 = spi_hal_w25_hw_read_status(0x05);
    s2 = spi_hal_w25_hw_read_status(0x35);
    printf("SR1: 0x%02X (BUSY=%d WEL=%d BP=%d)  SR2: 0x%02X\n",
           s1, s1&1, (s1>>1)&1, (s1>>2)&0xF, s2);
    spi_hal_w25_hw_write_enable();
    s1 = spi_hal_w25_hw_read_status(0x05);
    printf("After WE: SR1=0x%02X WEL=%d\n", s1, (s1>>1)&1);
}

static void hw_exp3(void)
{
    u8 w[256], r[256];
    int e;
    printf("\n===== [HW] Exp3: Page Write & Read =====\n");
    spi_hal_hw_init(239);
    spi_hal_w25_hw_sector_erase(0);
    {
        int i;
        for (i = 0; i < 256; i++) w[i] = i;
    }
    spi_hal_w25_hw_page_program(0, w, 256);
    spi_hal_w25_hw_read_data(0, r, 256);
    e = 0;
    {
        int i;
        for (i = 0; i < 256; i++) if (r[i] != w[i]) e++;
    }
    printf("Errors: %d/256  %s\n", e, e?"FAIL":"PASSED");
}

static void hw_exp4(void)
{
    u32 addr = 0xF0, len = 100, r = 256 - (addr & 0xFF);
    u8 w[100], b[100];
    int e, i;
    printf("\n===== [HW] Exp4: Cross-Page Write =====\n");
    spi_hal_hw_init(239);
    spi_hal_w25_hw_sector_erase(0);
    for (i = 0; i < 100; i++) w[i] = 0xAA + i;
    spi_hal_w25_hw_page_program(addr, w, r);
    spi_hal_w25_hw_page_program(addr + r, w + r, len - r);
    spi_hal_w25_hw_read_data(addr, b, len);
    e = 0;
    for (i = 0; i < 100; i++) if (b[i] != w[i]) e++;
    printf("Errors: %d/100  %s\n", e, e?"FAIL":"PASSED");
}

static void hw_exp5(void)
{
    u8 p[256], b[64];
    int i, f;
    printf("\n===== [HW] Exp5: Sector Erase & Verify =====\n");
    spi_hal_hw_init(239);
    for (i = 0; i < 256; i++) p[i] = 0xA5;
    spi_hal_w25_hw_sector_erase(0);
    spi_hal_w25_hw_page_program(0, p, 256);
    spi_hal_w25_hw_read_data(0, b, 64);
    printf("Before: buf[0]=0x%02X (expect A5)\n", b[0]);
    spi_hal_w25_hw_sector_erase(0);
    spi_hal_w25_hw_read_data(0, b, 64);
    f = 1;
    for (i = 0; i < 64; i++) if (b[i] != 0xFF) f = 0;
    printf("After:  buf[0]=0x%02X all_ff=%d  %s\n", b[0], f, f?"PASSED":"FAIL");
    spi_hal_w25_hw_page_program(0, p, 256);
    spi_hal_w25_hw_read_data(0, b, 64);
    printf("Re-write: buf[0]=0x%02X\n", b[0]);
}

static void hw_exp6(void)
{
    u32 t0, t1;
    printf("\n===== [HW] Exp6: Erase Timing =====\n");
    spi_hal_hw_init(239);
    printf("Sector 4KB (0x20): "); t0 = tick_get(); spi_hal_w25_hw_sector_erase(0); t1 = tick_get();
    printf("%lu ms\n", (t1 - t0) / 1000);
    printf("Block 32KB (0x52): "); spi_hal_w25_hw_write_enable(); spi_hal_cs_low();
    spi_hal_hw_byte(0x52); spi_hal_hw_byte(0); spi_hal_hw_byte(0); spi_hal_hw_byte(0); spi_hal_cs_high();
    t0 = tick_get(); spi_hal_w25_hw_wait_busy(); t1 = tick_get(); printf("%lu ms\n", (t1 - t0) / 1000);
    printf("Block 64KB (0xD8): "); spi_hal_w25_hw_write_enable(); spi_hal_cs_low();
    spi_hal_hw_byte(0xD8); spi_hal_hw_byte(0); spi_hal_hw_byte(0); spi_hal_hw_byte(0); spi_hal_cs_high();
    t0 = tick_get(); spi_hal_w25_hw_wait_busy(); t1 = tick_get(); printf("%lu ms\n", (t1 - t0) / 1000);
    printf("Chip Erase (0xC7) -- ~20s, please wait...\n");
    spi_hal_w25_hw_write_enable();
    spi_hal_cs_low();
    spi_hal_hw_byte(0xC7); spi_hal_cs_high();
    t0 = tick_get(); spi_hal_w25_hw_wait_busy(); t1 = tick_get();
    printf("%lu ms (~%lus)\n", (t1 - t0) / 1000, (t1 - t0) / 1000000);
}

static void hw_exp7(void)
{
    u8 s;
    printf("\n===== [HW] Exp7: Write Protection =====\n");
    spi_hal_hw_init(239);
    s = spi_hal_w25_hw_read_status(0x05);
    printf("Init SR1: 0x%02X BP=%d\n", s, (s >> 2) & 0xF);
    spi_hal_w25_hw_write_enable(); spi_hal_cs_low(); spi_hal_hw_byte(0x01);
    spi_hal_hw_byte(s | 0x10); spi_hal_hw_byte(0); spi_hal_cs_high(); spi_hal_w25_hw_wait_busy();
    s = spi_hal_w25_hw_read_status(0x05);
    printf("Set BP2: 0x%02X BP=%d\n", s, (s >> 2) & 0xF);
    printf("\nAttempting write to protected area (0x400000)...\n");
    spi_hal_w25_hw_write_enable(); spi_hal_cs_low(); spi_hal_hw_byte(0x02);
    spi_hal_hw_byte(0x40); spi_hal_hw_byte(0); spi_hal_hw_byte(0); spi_hal_hw_byte(0x55);
    spi_hal_cs_high(); spi_hal_w25_hw_wait_busy();
    s = spi_hal_w25_hw_read_status(0x05);
    printf("Write protected area: SR1=0x%02X WEL=%d\n", s, (s >> 1) & 1);
    printf("(WEL=0 means write rejected - protection works)\n");
    printf("\nRemoving protection (BP=0)...\n");
    spi_hal_w25_hw_write_enable(); spi_hal_cs_low(); spi_hal_hw_byte(0x01);
    spi_hal_hw_byte(s & ~0x3C); spi_hal_hw_byte(0); spi_hal_cs_high(); spi_hal_w25_hw_wait_busy();
    s = spi_hal_w25_hw_read_status(0x05);
    printf("Removed: SR1=0x%02X BP=%d\n", s, (s >> 2) & 0xF);
}

/* Exp 8: Fast Read 0x0B vs Standard Read 0x03 速度对比 */
static void hw_exp8(void)
{
    #define FRB 2048
    u8 buf[FRB];
    u32 t0, t_std, t_fast;
    int s32_diff;
    printf("\n===== [HW] Exp8: Fast Read Speed (0x03 vs 0x0B) =====\n");
    spi_hal_hw_init(239);
    (void)buf;

    t0 = tick_get();
    spi_hal_cs_low();
    spi_hal_hw_byte(0x03);
    spi_hal_hw_byte(0); spi_hal_hw_byte(0); spi_hal_hw_byte(0);
    {
        int i;
        for (i = 0; i < FRB; i++) (void)spi_hal_hw_byte(0xFF);
    }
    spi_hal_cs_high();
    t_std = tick_get() - t0;

    t0 = tick_get();
    spi_hal_cs_low();
    spi_hal_hw_byte(0x0B);
    spi_hal_hw_byte(0); spi_hal_hw_byte(0); spi_hal_hw_byte(0);
    spi_hal_hw_byte(0xFF);    /* Fast Read 要求的 1 字节 dummy */
    {
        int i;
        for (i = 0; i < FRB; i++) (void)spi_hal_hw_byte(0xFF);
    }
    spi_hal_cs_high();
    t_fast = tick_get() - t0;

    printf("Standard Read (0x03): %lu ticks\n", t_std);
    printf("Fast Read    (0x0B): %lu ticks (with 1 dummy byte)\n", t_fast);
    s32_diff = (s32)(t_fast - t_std);
    printf("Diff: %ld ticks (Fast-Standard)\n", s32_diff);
    printf("(At 100kHz speeds similar; at high clock Fast Read wins)\n");
    #undef FRB
}

/* Exp 9: 100 kHz vs 12 MHz 读 4096B 对比 */
static void hw_exp9(void)
{
    #define BLK 4096
    u8 buf[BLK];
    u32 t0, t_poll;
    printf("\n===== [HW] Exp9: 100K vs 12MHz Read Speed =====\n");
    spi_hal_hw_init(239);
    (void)buf;

    SPI1BAUD = 239;
    t0 = tick_get();
    spi_hal_cs_low();
    spi_hal_hw_byte(0x03);
    spi_hal_hw_byte(0); spi_hal_hw_byte(0); spi_hal_hw_byte(0);
    {
        int i;
        for (i = 0; i < BLK; i++) (void)spi_hal_hw_byte(0xFF);
    }
    spi_hal_cs_high();
    printf("100kHz Standard Read: %lu ticks\n", tick_get() - t0);

    SPI1BAUD = 1;
    t0 = tick_get();
    spi_hal_cs_low();
    spi_hal_hw_byte(0x03);
    spi_hal_hw_byte(0); spi_hal_hw_byte(0); spi_hal_hw_byte(0);
    {
        int i;
        for (i = 0; i < BLK; i++) (void)spi_hal_hw_byte(0xFF);
    }
    spi_hal_cs_high();
    t_poll = tick_get() - t0;
    printf("12MHz Standard Read: %lu ticks\n", t_poll);

    printf("12MHz bit period = 2/24M = 83ns; 4096 bytes = %lu us\n",
           (u32)(((u64)BLK * 8 * 2) / 24));
    SPI1BAUD = 239;
    #undef BLK
}

/* Exp 10: Unique ID + SFDP */
static void hw_exp10(void)
{
    int i;
    printf("\n===== [HW] Exp10: Unique ID & SFDP =====\n");
    spi_hal_hw_init(239);
    printf("UID: ");
    spi_hal_cs_low();
    spi_hal_hw_byte(0x4B);
    for (i = 0; i < 4; i++) spi_hal_hw_byte(0xFF);
    for (i = 0; i < 8; i++) printf("%02X ", spi_hal_hw_byte(0xFF));
    spi_hal_cs_high();
    printf("\nSFDP: ");
    spi_hal_cs_low();
    spi_hal_hw_byte(0x5A);
    for (i = 0; i < 3; i++) spi_hal_hw_byte(0);
    spi_hal_hw_byte(0xFF);
    for (i = 0; i < 16; i++) printf("%02X ", spi_hal_hw_byte(0xFF));
    spi_hal_cs_high();
    printf("\n(expect first 4 bytes: 53 46 44 50 = 'SFDP')\n");
}

/* ===================== 硬件 suite dispatcher ===================== */
static void run_hw_suite(void)
{
    printf("\n##### [HW SPI] MODE=%d #####\n", SPI_HW_W25_MODE);
    spi_hal_hw_init(239);
#if SPI_HW_W25_MODE == 1
    hw_exp1(); delay_ms(200);
    hw_exp2(); delay_ms(200);
    hw_exp3(); delay_ms(200);
    hw_exp4(); delay_ms(200);
    hw_exp5(); delay_ms(200);
    hw_exp6(); delay_ms(200);
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
