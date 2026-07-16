/**
 * @file    hw_spi_test.c
 * @brief   BT892X 硬件 SPI1 测试 + W25Q64 读 JEDEC ID
 *
 *  SPI1 G4: CLK=PE6, MOSI=PE7, MISO=PE5 (硬件控制)
 *  CS: PE4 (GPIO 手动控制)
 *
 *  SPI Mode 0, 主机, 100KHz
 */

#include "test.h"

#define HW_CS       BIT(4)  // PE4 (GPIO 手动 CS)
#define HW_CLK      BIT(6)  // PE6
#define HW_MOSI     BIT(7)  // PE7
#define HW_MISO     BIT(5)  // PE5

static void hw_spi_init(void)
{
    // SPI1 引脚映射: G4
    FUNCMCON1 &= ~(0xF << 12);
    FUNCMCON1 |=  (0x4 << 12);

    // CLK, MOSI, MISO → 功能 IO
    GPIOEFEN |= HW_CLK | HW_MOSI | HW_MISO;
    GPIOEDE  |= HW_CLK | HW_MOSI | HW_MISO;
    GPIOEDIR &= ~(HW_CLK | HW_MOSI);       // CLK, MOSI = 输出
    GPIOEDIR |=  HW_MISO;                  // MISO = 输入

    // CS (PE4) → GPIO 输出, 初始高
    GPIOEFEN &= ~HW_CS;
    GPIOEDE  |=  HW_CS;
    GPIOEDIR &= ~HW_CS;
    GPIOESET  =  HW_CS;

    // SPI1 配置: Mode 0, 主机, 100KHz
    SPI1BAUD = 239;                         // 24MHz/240 = 100KHz
    SPI1CON  = BIT(0);                      // SPIEN (Mode 0 default)
}

/* 硬件 SPI1 收发一字节 */
static u8 hw_spi_byte(u8 tx)
{
    SPI1BUF = tx;
    while (!(SPI1CON & BIT(16)));           // 等 SPIPND
    SPI1CPND = BIT(16);                     // 清除
    return (u8)SPI1BUF;
}

void hw_spi_test(void)
{
    printf("\n===== Hardware SPI1 Test =====\n\n");
    hw_spi_init();

    printf("SPI1 G4: CLK=PE6, MOSI=PE7, MISO=PE5, CS=PE4(GPIO)\n");
    printf("Mode 0, Master, 100KHz\n\n");

    // ============================================================
    // 1. 硬件 SPI 回环测试 (跳线 PE7↔PE5)
    // ============================================================
    printf("--- Test 1: Loopback (jumper PE7--PE5) ---\n");
    int err = 0;
    for (int v = 0; v <= 0xFF; v++) {
        u8 rx = hw_spi_byte((u8)v);
        if (rx != (u8)v) { err++; if (err <= 3) printf("ERR: s=0x%02X r=0x%02X\n", v, rx); }
    }
    printf("Loopback: %s (%d/256 errors)\n\n", err ? "FAILED" : "PASSED", err);

    // ============================================================
    // 2. W25Q64 读 JEDEC ID
    // ============================================================
    printf("--- Test 2: W25Q64 JEDEC ID ---\n");
    GPIOECLR = HW_CS;  delay_us(1);         // CS LOW
    hw_spi_byte(0x9F);
    u8 id0 = hw_spi_byte(0xFF);
    u8 id1 = hw_spi_byte(0xFF);
    u8 id2 = hw_spi_byte(0xFF);
    GPIOESET = HW_CS;  delay_us(1);         // CS HIGH

    printf("JEDEC ID: 0x%02X 0x%02X 0x%02X\n", id0, id1, id2);
    printf("Expected:  0xEF 0x40 0x17\n");
    printf(id0 == 0xEF ? "MATCH!\n" : "MISMATCH\n");

    while (1);
}
