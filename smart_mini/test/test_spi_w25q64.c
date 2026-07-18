/**
 * @file    test_spi_w25q64.c
 * @brief   BT892X W25Q64 Flash 软/硬 SPI 合一套件（合自 copilot w25q64_test.c + hw_spi_w25q64.c）
 *
 *  接线：CS=PE4(GPIO), CLK=PE6, MOSI=PE7, MISO=PE5 → W25Q64
 *  引脚编号（共享硬件 SPI1 G4）：CS=PE4, CLK=PE6, DI=PE7, DO=PE5
 *
 *  软件/硬件实验保持原行为，仅统一入口 test_spi_w25q64_run()
 *  编译宏控制：
 *    - SPI_SW_W25_MODE  (软件版): 0=默认单 exp / 1=全测 / 2=仅读
 *    - SPI_HW_W25_MODE  (硬件版): 0=默认单 exp / 1=全测 / 2=仅读
 *    - SPI_SW_W25_EXP / SPI_HW_W25_EXP: 选具体 exp（默认 1=JEDEC）
 *    - SPI_W25_RUN_MODE: 0=只软(默认) / 1=只硬 / 2=软硬全跑
 *                          (例：-DSPI_W25_RUN_MODE=2 一键跑完整)
 *
 *  ⚠️ MODE=1 含 Chip Erase（不可逆，整片 Flash 清空！）
 *
 * 参考手册: BT892X_UserManual_Driver.md §3.2 GPIO、§3.3 FUNCMCON1、§7 SPI
 */

#include "test_spi_common.h"

#ifndef SPI_SW_W25_MODE
#define SPI_SW_W25_MODE 0
#endif
#ifndef SPI_HW_W25_MODE
#define SPI_HW_W25_MODE 0
#endif
#ifndef SPI_SW_W25_EXP
#define SPI_SW_W25_EXP 1
#endif
#ifndef SPI_HW_W25_EXP
#define SPI_HW_W25_EXP 1
#endif
// SPI_W25_RUN_MODE：0=只软（默认）/ 1=只硬 / 2=软硬全跑
#ifndef SPI_W25_RUN_MODE
#define SPI_W25_RUN_MODE 0
#endif

/* ===================== 公共底层：CS 控制 ===================== */
#define SPI_CS      SPI_CS_PIN
#define SPI_CLK     SPI_CLK_PIN
#define SPI_MOSI    SPI_MOSI_PIN
#define SPI_MISO    SPI_MISO_PIN

/* ===================== 软件 bit-bang ===================== */
static void w25q64_soft_init(void)
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

/* ===================== 硬件 SPI1 ===================== */
static void w25q64_hw_init(u32 baud)
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

/* ===================== 软件 实验 ===================== */
static void sw_exp1(void)
{
    printf("\n===== [Soft] Exp1: JEDEC ID =====\n");
    w25q64_soft_init();
    sw_cs_low(); soft_spi_byte(0x9F);
    u8 a = soft_spi_byte(0xFF), b = soft_spi_byte(0xFF), c = soft_spi_byte(0xFF);
    sw_cs_high();
    printf("JEDEC: 0x%02X 0x%02X 0x%02X %s\n", a, b, c,
           (a==0xEF && b==0x40) ? "MATCH" : "FAIL");
}

static void sw_exp3(void)
{
    printf("\n===== [Soft] Exp3: Page Write & Read =====\n");
    w25q64_soft_init();
    sw_sector_erase(0x000000);
    u8 w[256], r[256]; for (int i=0;i<256;i++) w[i]=i;
    sw_page_program(0x000000, w, 256);
    sw_read_data(0x000000, r, 256);
    int e=0; for (int i=0;i<256;i++) if (r[i]!=w[i]) e++;
    printf("Errors: %d/256  %s\n", e, e?"FAIL":"PASSED");
}

static void sw_exp5(void)
{
    printf("\n===== [Soft] Exp5: Sector Erase =====\n");
    w25q64_soft_init();
    u8 p[256]; for (int i=0;i<256;i++) p[i]=0xA5;
    sw_sector_erase(0); sw_page_program(0, p, 256);
    sw_sector_erase(0);
    u8 b[64]; sw_read_data(0, b, 64);
    int f=1; for (int i=0;i<64;i++) if (b[i]!=0xFF) f=0;
    printf("After erase: all_ff=%d  %s\n", f, f?"PASSED":"FAIL");
}

static void sw_exp6(void)
{
    printf("\n===== [Soft] Exp6: Erase Timing =====\n");
    w25q64_soft_init(); u32 t0,t1;
    t0 = tick_get(); sw_sector_erase(0); t1 = tick_get();
    printf("Sector 4KB: %lu ms\n", (t1-t0)/1000);
    sw_write_enable();
    sw_cs_low();
    soft_spi_byte(0xC7);  // Chip Erase
    sw_cs_high();
    t0 = tick_get(); sw_wait_busy(); t1 = tick_get();
    printf("Chip Erase: %lu ms (~%lus)\n", (t1-t0)/1000, (t1-t0)/1000000);
}

static void sw_exp9(void)
{
    printf("\n===== [Soft] Exp9: Unique ID & SFDP =====\n");
    w25q64_soft_init();
    printf("UID: "); sw_cs_low(); soft_spi_byte(0x4B);
    for (int i=0;i<4;i++) soft_spi_byte(0xFF);
    for (int i=0;i<8;i++) printf("%02X ", soft_spi_byte(0xFF));
    sw_cs_high(); printf("\n");
    printf("SFDP: "); sw_cs_low(); soft_spi_byte(0x5A);
    for (int i=0;i<3;i++) soft_spi_byte(0);
    for (int i=0;i<16;i++) printf("%02X ", soft_spi_byte(0xFF));
    sw_cs_high(); printf("\n");
}

static void run_soft_suite(void)
{
    printf("\n##### [Soft SPI] MODE=%d #####\n", SPI_SW_W25_MODE);
#if SPI_SW_W25_MODE == 1
    sw_exp1(); delay_ms(200);
    sw_exp3(); delay_ms(200);
    sw_exp5(); delay_ms(200);
    sw_exp6(); delay_ms(200);   // chip erase ~20s
    sw_exp9();
#elif SPI_SW_W25_MODE == 2
    sw_exp1(); delay_ms(200);
    sw_exp9();
#else
    switch (SPI_SW_W25_EXP) {
        case 1: sw_exp1(); break;
        case 3: sw_exp3(); break;
        case 5: sw_exp5(); break;
        case 6: sw_exp6(); break;
        case 9: sw_exp9(); break;
    }
#endif
}

/* ===================== 硬件 实验 ===================== */
static void hw_exp1(void)
{
    printf("\n===== [HW] Exp1: JEDEC ID =====\n");
    w25q64_hw_init(239);
    hw_cs_low(); hw_spi_byte(0x9F);
    u8 a = hw_spi_byte(0xFF), b = hw_spi_byte(0xFF), c = hw_spi_byte(0xFF);
    hw_cs_high();
    printf("JEDEC: 0x%02X 0x%02X 0x%02X %s\n", a, b, c, a==0xEF?"OK":"FAIL");
}

static void hw_exp3(void)
{
    printf("\n===== [HW] Exp3: Page Write & Read =====\n");
    w25q64_hw_init(239);
    hw_sector_erase(0);
    u8 w[256], r[256]; for (int i=0;i<256;i++) w[i]=i;
    hw_page_program(0, w, 256);
    hw_read_data(0, r, 256);
    int e=0; for (int i=0;i<256;i++) if (r[i]!=w[i]) e++;
    printf("Errors: %d/256  %s\n", e, e?"FAIL":"PASSED");
}

static void hw_exp5(void)
{
    printf("\n===== [HW] Exp5: Sector Erase =====\n");
    w25q64_hw_init(239);
    u8 p[256]; for (int i=0;i<256;i++) p[i]=0xA5;
    hw_sector_erase(0); hw_page_program(0, p, 256);
    hw_sector_erase(0);
    u8 b[64]; hw_read_data(0, b, 64);
    int f=1; for (int i=0;i<64;i++) if (b[i]!=0xFF) f=0;
    printf("After erase: all_ff=%d  %s\n", f, f?"PASSED":"FAIL");
}

static void hw_exp6(void)
{
    printf("\n===== [HW] Exp6: Erase Timing =====\n");
    w25q64_hw_init(239); u32 t0,t1;
    t0 = tick_get(); hw_sector_erase(0); t1 = tick_get();
    printf("Sector 4KB: %lu ms\n", (t1-t0)/1000);
    hw_write_enable();
    hw_cs_low();
    hw_spi_byte(0xC7);
    hw_cs_high();
    t0 = tick_get(); hw_wait_busy(); t1 = tick_get();
    printf("Chip Erase: %lu ms (~%lus)\n", (t1-t0)/1000, (t1-t0)/1000000);
}

static void hw_exp9(void)
{
    printf("\n===== [HW] Exp9: Unique ID & SFDP =====\n");
    w25q64_hw_init(239);
    printf("UID: "); hw_cs_low(); hw_spi_byte(0x4B);
    for (int i=0;i<4;i++) hw_spi_byte(0xFF);
    for (int i=0;i<8;i++) printf("%02X ", hw_spi_byte(0xFF));
    hw_cs_high(); printf("\n");
    printf("SFDP: "); hw_cs_low(); hw_spi_byte(0x5A);
    for (int i=0;i<3;i++) hw_spi_byte(0);
    for (int i=0;i<16;i++) printf("%02X ", hw_spi_byte(0xFF));
    hw_cs_high(); printf("\n");
}

static void run_hw_suite(void)
{
    printf("\n##### [HW SPI] MODE=%d #####\n", SPI_HW_W25_MODE);
#if SPI_HW_W25_MODE == 1
    hw_exp1(); delay_ms(200);
    hw_exp3(); delay_ms(200);
    hw_exp5(); delay_ms(200);
    hw_exp6(); delay_ms(200);
    hw_exp9();
#elif SPI_HW_W25_MODE == 2
    hw_exp1(); delay_ms(200);
    hw_exp9();
#else
    switch (SPI_HW_W25_EXP) {
        case 1: hw_exp1(); break;
        case 3: hw_exp3(); break;
        case 5: hw_exp5(); break;
        case 6: hw_exp6(); break;
        case 9: hw_exp9(); break;
    }
#endif
}

/* ===================== 入口 ===================== */
void test_spi_w25q64_run(void)
{
    printf("\n===== BT892X W25Q64 Test (Soft + HW SPI) =====\n");
    printf("W25Q64: CS=PE4, CLK=PE6, DI=PE7, DO=PE5\n\n");
    printf("NOTE: SPI_SW_W25_MODE=%d, SPI_HW_W25_MODE=%d\n",
           SPI_SW_W25_MODE, SPI_HW_W25_MODE);

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
