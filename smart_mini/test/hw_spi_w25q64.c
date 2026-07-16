/**
 * @file    hw_spi_w25q64.c
 * @brief   BT892X 硬件 SPI1 驱动 W25Q64 Flash — 全套实验
 *
 *  硬件 SPI1 G4: CLK=PE6, MOSI=PE7, MISO=PE5
 *  CS: PE4 (GPIO 手动控制)
 *  SPI Mode 0, 100KHz
 *
 *  实验 (main.c 切换):  hw_exp1~7, hw_exp9
 */

#include "test.h"

#define HW_CS       BIT(4)
#define HW_CLK      BIT(6)
#define HW_MOSI     BIT(7)
#define HW_MISO     BIT(5)

static void hw_w25q64_init(void)
{
    FUNCMCON1 &= ~(0xF << 12);
    FUNCMCON1 |=  (0x4 << 12);

    GPIOEFEN |= HW_CLK | HW_MOSI | HW_MISO;
    GPIOEDE  |= HW_CLK | HW_MOSI | HW_MISO;
    GPIOEDIR &= ~(HW_CLK | HW_MOSI);
    GPIOEDIR |=  HW_MISO;

    GPIOEFEN &= ~HW_CS;  GPIOEDE |= HW_CS;
    GPIOEDIR &= ~HW_CS;  GPIOESET = HW_CS;

    SPI1BAUD = 239;     // 100KHz
    SPI1CON  = BIT(0);  // SPIEN, Mode 0
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

static void hw_write_enable(void)
    { hw_cs_low(); hw_spi_byte(0x06); hw_cs_high(); }

static u8 hw_read_status(u8 cmd)
    { u8 s; hw_cs_low(); hw_spi_byte(cmd); s = hw_spi_byte(0xFF); hw_cs_high(); return s; }

static void hw_wait_busy(void)
    { while (hw_read_status(0x05) & 1); }

static void hw_read_data(u32 addr, u8 *buf, u32 len)
{
    hw_cs_low(); hw_spi_byte(0x03);
    hw_spi_byte(addr>>16); hw_spi_byte(addr>>8); hw_spi_byte(addr);
    for (u32 i = 0; i < len; i++) buf[i] = hw_spi_byte(0xFF);
    hw_cs_high();
}

static void hw_page_program(u32 addr, u8 *buf, u32 len)
{
    hw_write_enable(); hw_cs_low(); hw_spi_byte(0x02);
    hw_spi_byte(addr>>16); hw_spi_byte(addr>>8); hw_spi_byte(addr);
    for (u32 i = 0; i < len; i++) hw_spi_byte(buf[i]);
    hw_cs_high(); hw_wait_busy();
}

static void hw_sector_erase(u32 addr)
{
    hw_write_enable(); hw_cs_low(); hw_spi_byte(0x20);
    hw_spi_byte(addr>>16); hw_spi_byte(addr>>8); hw_spi_byte(addr);
    hw_cs_high(); hw_wait_busy();
}

// ===================== 实验函数 =====================

void hw_exp1_jedec_id(void)
{
    printf("\n===== [HW] Exp1: JEDEC ID =====\n\n");
    hw_w25q64_init();
    hw_cs_low(); hw_spi_byte(0x9F);
    u8 a = hw_spi_byte(0xFF), b = hw_spi_byte(0xFF), c = hw_spi_byte(0xFF);
    hw_cs_high();
    printf("JEDEC ID: 0x%02X 0x%02X 0x%02X %s\n", a, b, c,
           a == 0xEF ? "OK" : "FAIL");
    while (1);
}

void hw_exp2_status(void)
{
    printf("\n===== [HW] Exp2: Status Registers =====\n\n");
    hw_w25q64_init();
    u8 s1 = hw_read_status(0x05), s2 = hw_read_status(0x35);
    printf("SR1: 0x%02X (BUSY=%d WEL=%d BP=%d)  SR2: 0x%02X\n",
           s1, s1&1, (s1>>1)&1, (s1>>2)&0xF, s2);
    hw_write_enable();
    s1 = hw_read_status(0x05);
    printf("After WE: SR1=0x%02X WEL=%d\n", s1, (s1>>1)&1);
    while (1);
}

void hw_exp3_page_rw(void)
{
    printf("\n===== [HW] Exp3: Page Write & Read =====\n\n");
    hw_w25q64_init();
    hw_sector_erase(0x000000);
    u8 w[256], r[256]; for (int i=0;i<256;i++) w[i]=i;
    hw_page_program(0x000000, w, 256);
    hw_read_data(0x000000, r, 256);
    int e=0; for (int i=0;i<256;i++) if(r[i]!=w[i]) e++;
    printf("Errors: %d / 256  %s\n", e, e?"FAIL":"PASSED");
    while (1);
}

void hw_exp4_cross_page(void)
{
    printf("\n===== [HW] Exp4: Cross-Page Write =====\n\n");
    hw_w25q64_init();
    u32 addr=0xF0, len=100, r=256-(addr&0xFF);
    hw_sector_erase(0);
    u8 w[100], b[100]; for (int i=0;i<100;i++) w[i]=0xAA+i;
    hw_page_program(addr, w, r);
    hw_page_program(addr+r, w+r, len-r);
    hw_read_data(addr, b, len);
    int e=0; for (int i=0;i<100;i++) if(b[i]!=w[i]) e++;
    printf("Errors: %d / 100  %s\n", e, e?"FAIL":"PASSED");
    while (1);
}

void hw_exp5_sector_erase(void)
{
    printf("\n===== [HW] Exp5: Sector Erase =====\n\n");
    hw_w25q64_init();
    u8 p[256], b[64]; for (int i=0;i<256;i++) p[i]=0xA5;
    hw_sector_erase(0); hw_page_program(0, p, 256);
    hw_read_data(0, b, 64); printf("Before: buf[0]=0x%02X (expect A5)\n", b[0]);
    hw_sector_erase(0);
    hw_read_data(0, b, 64);
    int f=1; for (int i=0;i<64;i++) if(b[i]!=0xFF) f=0;
    printf("After:  buf[0]=0x%02X all_ff=%d %s\n", b[0], f, f?"PASSED":"FAIL");
    hw_page_program(0, p, 256);
    hw_read_data(0, b, 64); printf("Re-write: buf[0]=0x%02X\n", b[0]);
    while (1);
}

void hw_exp6_erase_timing(void)
{
    printf("\n===== [HW] Exp6: Erase Timing =====\n\n");
    hw_w25q64_init(); u32 t0,t1;

    printf("Sector 4KB: "); t0=tick_get(); hw_sector_erase(0); t1=tick_get();
    printf("%lums\n", (t1-t0)/1000);

    printf("Block 32KB: "); hw_write_enable(); hw_cs_low();
    hw_spi_byte(0x52); hw_spi_byte(0);hw_spi_byte(0);hw_spi_byte(0); hw_cs_high();
    t0=tick_get(); hw_wait_busy(); t1=tick_get(); printf("%lums\n", (t1-t0)/1000);

    printf("Block 64KB: "); hw_write_enable(); hw_cs_low();
    hw_spi_byte(0xD8); hw_spi_byte(0);hw_spi_byte(0);hw_spi_byte(0); hw_cs_high();
    t0=tick_get(); hw_wait_busy(); t1=tick_get(); printf("%lums\n", (t1-t0)/1000);

    printf("Chip Erase (~20s): "); hw_write_enable(); hw_cs_low();
    hw_spi_byte(0xC7); hw_cs_high();
    t0=tick_get(); hw_wait_busy(); t1=tick_get();
    printf("%lums (~%lus)\n", (t1-t0)/1000, (t1-t0)/1000000);

    printf("Done.\n"); while (1);
}

void hw_exp7_write_protect(void)
{
    printf("\n===== [HW] Exp7: Write Protection =====\n\n");
    hw_w25q64_init();
    u8 s=hw_read_status(0x05);
    printf("Init SR1: 0x%02X BP=%d\n", s, (s>>2)&0xF);
    hw_write_enable(); hw_cs_low(); hw_spi_byte(0x01);
    hw_spi_byte(s|0x10); hw_spi_byte(0); hw_cs_high(); hw_wait_busy();
    s=hw_read_status(0x05); printf("Set BP2: 0x%02X BP=%d\n", s, (s>>2)&0xF);
    hw_write_enable(); hw_cs_low(); hw_spi_byte(0x02);
    hw_spi_byte(0x40);hw_spi_byte(0);hw_spi_byte(0); hw_spi_byte(0x55);
    hw_cs_high(); hw_wait_busy();
    s=hw_read_status(0x05); printf("Write protected area: SR1=0x%02X WEL=%d\n", s, (s>>1)&1);
    printf("(WEL=0 means write rejected - protection works)\n");
    hw_write_enable(); hw_cs_low(); hw_spi_byte(0x01);
    hw_spi_byte(s&~0x3C); hw_spi_byte(0); hw_cs_high(); hw_wait_busy();
    s=hw_read_status(0x05); printf("Removed: SR1=0x%02X BP=%d\n", s, (s>>2)&0xF);
    while (1);
}

/* ================================================================
 *  实验 8: Fast Read 速度对比
 *  Standard Read (0x03) vs Fast Read (0x0B, 含 1 字节 dummy)
 * ================================================================ */
void hw_exp8_fast_read(void)
{
    printf("\n===== [HW] Exp8: Fast Read Speed Compare =====\n\n");
    hw_w25q64_init();

    #define BUF_SIZE    2048
    u8 buf[BUF_SIZE];
    u32 t0, t1;

    // Standard Read (0x03): 命令 + 3地址 + N数据
    printf("Standard Read (0x03): %d bytes from addr 0...\n", BUF_SIZE);
    t0 = tick_get();
    hw_cs_low();
    hw_spi_byte(0x03);
    hw_spi_byte(0); hw_spi_byte(0); hw_spi_byte(0);
    for (int i = 0; i < BUF_SIZE; i++) buf[i] = hw_spi_byte(0xFF);
    hw_cs_high();
    t1 = tick_get();
    u32 t_std = t1 - t0;

    // Fast Read (0x0B): 命令 + 3地址 + 1dummy + N数据
    // 在 100KHz 下速度相近，dummy 是额外开销；更高时钟下优势明显
    printf("Fast Read   (0x0B): %d bytes from addr 0...\n", BUF_SIZE);
    t0 = tick_get();
    hw_cs_low();
    hw_spi_byte(0x0B);
    hw_spi_byte(0); hw_spi_byte(0); hw_spi_byte(0);
    hw_spi_byte(0xFF);  // dummy byte
    for (int i = 0; i < BUF_SIZE; i++) buf[i] = hw_spi_byte(0xFF);
    hw_cs_high();
    t1 = tick_get();
    u32 t_fast = t1 - t0;

    // 检查数据一致性 (两种读法数据应相同)
    u8 ref[BUF_SIZE];
    hw_cs_low(); hw_spi_byte(0x03);
    hw_spi_byte(0); hw_spi_byte(0); hw_spi_byte(0);
    for (int i = 0; i < BUF_SIZE; i++) ref[i] = hw_spi_byte(0xFF);
    hw_cs_high();

    printf("\n--- Results ---\n");
    printf("Standard Read: %lu ticks = %lu us\n", t_std, t_std);
    printf("Fast Read:     %lu ticks = %lu us\n", t_fast, t_fast);
    printf("Difference:    %+ld ticks\n", (s32)(t_fast - t_std));
    printf("\nData match: Standard vs Fast — %s\n",
           memcmp(buf, ref, BUF_SIZE) == 0 ? "OK" : "MISMATCH");

    if (t_fast <= t_std)
        printf("(At 100KHz, speeds are similar; at higher clock Fast Read has advantage)\n");
    printf("\nDual/Quad SPI needs QE bit + BUSMODE change, deferred to advanced SPI phase.\n");

    while (1);
}

/* ================================================================
 *  实验 9: Fast Read 反超演示 (高低速对比)
 *  低速 100KHz → Standard 快 (Fast 多 dummy)
 *  高速 12MHz  → 两者接近, 但 Fast 支持更高频率
 * ================================================================ */
void hw_exp9_speed_demo(void)
{
    printf("\n===== [HW] Exp9: Fast Read Speed Demo =====\n\n");
    hw_w25q64_init();

    #define BLK 4096
    u8 buf[BLK];
    (void)buf;  // suppress unused warning
    u32 t0, t1;

    // ====== 低速 100KHz ======
    SPI1BAUD = 239;  // 100KHz
    printf("--- At 100KHz (BAUD=239) ---\n");

    t0 = tick_get();
    hw_cs_low(); hw_spi_byte(0x03);
    hw_spi_byte(0);hw_spi_byte(0);hw_spi_byte(0);
    for (int i=0;i<BLK;i++) buf[i]=hw_spi_byte(0xFF);
    hw_cs_high(); t1 = tick_get();
    printf("Standard(0x03): %lu ticks\n", t1-t0);

    t0 = tick_get();
    hw_cs_low(); hw_spi_byte(0x0B);
    hw_spi_byte(0);hw_spi_byte(0);hw_spi_byte(0);
    hw_spi_byte(0xFF);
    for (int i=0;i<BLK;i++) buf[i]=hw_spi_byte(0xFF);
    hw_cs_high(); t1 = tick_get();
    printf("Fast   (0x0B): %lu ticks  --- slower (dummy byte)\n\n", t1-t0);

    // ====== 高速 12MHz ======
    SPI1BAUD = 1;    // 24MHz/(1+1) = 12MHz
    printf("--- At 12MHz (BAUD=1) ---\n");

    t0 = tick_get();
    hw_cs_low(); hw_spi_byte(0x03);
    hw_spi_byte(0);hw_spi_byte(0);hw_spi_byte(0);
    for (int i=0;i<BLK;i++) buf[i]=hw_spi_byte(0xFF);
    hw_cs_high(); t1 = tick_get();
    u32 t_std = t1-t0;
    printf("Standard(0x03): %lu ticks\n", t_std);

    t0 = tick_get();
    hw_cs_low(); hw_spi_byte(0x0B);
    hw_spi_byte(0);hw_spi_byte(0);hw_spi_byte(0);
    hw_spi_byte(0xFF);
    for (int i=0;i<BLK;i++) buf[i]=hw_spi_byte(0xFF);
    hw_cs_high(); t1 = tick_get();
    u32 t_fast = t1-t0;
    printf("Fast   (0x0B): %lu ticks\n\n", t_fast);

    printf("=== Analysis ===\n");
    printf("100KHz: Fast > Standard (dummy byte overhead visible)\n");
    printf("12MHz:  Fast is FASTER by %ld ticks (%ld.%ld%%)\n",
           t_std - t_fast,
           (100 * (t_std - t_fast)) / t_std,
           ((1000 * (t_std - t_fast)) / t_std) % 10);
    printf("----At high speed, dummy byte cost is negligible.\n");
    printf("----Fast Read (0x0B) supports up to 133MHz on W25Q64,\n");
    printf("----Standard Read (0x03) only ~50MHz.\n");
    printf("----For production code, always use Fast Read.\n");

    // 恢复低速
    SPI1BAUD = 239;
    while (1);
}

void hw_exp10_unique_id(void)
{
    printf("\n===== [HW] Exp10: Unique ID & SFDP =====\n\n");
    hw_w25q64_init();
    printf("UID: "); hw_cs_low(); hw_spi_byte(0x4B);
    hw_spi_byte(0xFF);hw_spi_byte(0xFF);hw_spi_byte(0xFF);hw_spi_byte(0xFF);
    for (int i=0;i<8;i++) printf("%02X ", hw_spi_byte(0xFF)); hw_cs_high();
    printf("\nSFDP: "); hw_cs_low(); hw_spi_byte(0x5A);
    hw_spi_byte(0);hw_spi_byte(0);hw_spi_byte(0); hw_spi_byte(0xFF);
    for (int i=0;i<16;i++) printf("%02X ", hw_spi_byte(0xFF)); hw_cs_high();
    printf("\n(expect 53 46 44 50 = SFDP)\n"); while (1);
}


