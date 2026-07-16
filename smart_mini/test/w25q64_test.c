/**
 * @file    w25q64_test.c
 * @brief   BT892X 软件 SPI 驱动 W25Q64 Flash (读 JEDEC ID)
 *
 * 引脚 (PORTE):
 *   PE4 = CS,  PE5 = CLK
 *   PE6 = MOSI, PE7 = MISO
 *
 * W25Q64 命令:
 *   0x9F = Read JEDEC ID → 返回 0xEF 0x40 0x17 (Winbond 64Mbit)
 *
 * SPI Mode 0 (CPOL=0, CPHA=0), MSB first
 */

#include "test.h"

#define SPI_CS      BIT(4)  // PE4
#define SPI_CLK     BIT(5)  // PE5
#define SPI_MOSI    BIT(6)  // PE6
#define SPI_MISO    BIT(7)  // PE7

/* ================================================================
 *  软件 SPI 收发一个字节 (Mode 0, MSB first)
 * ================================================================ */
static u8 soft_spi_byte(u8 tx)
{
    u8 rx = 0;
    for (int i = 7; i >= 0; i--) {
        // MOSI: 设数据位
        if (tx & (1 << i))
            GPIOESET = SPI_MOSI;
        else
            GPIOECLR = SPI_MOSI;

        delay_us(1);                    // 数据建立

        // CLK 上升沿
        GPIOESET = SPI_CLK;
        delay_us(1);

        // MISO: 在 CLK 高电平时采样
        if (GPIOE & SPI_MISO)
            rx |= (1 << i);

        // CLK 下降沿
        GPIOECLR = SPI_CLK;
        delay_us(1);
    }
    return rx;
}

/* ================================================================
 *  读 W25Q64 JEDEC ID (命令 0x9F)
 *  返回: id[0]=制造商, id[1]=类型, id[2]=容量
 * ================================================================ */
static void w25q64_read_jedec_id(u8 *id)
{
    GPIOECLR = SPI_CS;          // CS = LOW (选中)
    delay_us(1);

    soft_spi_byte(0x9F);        // 发送命令

    id[0] = soft_spi_byte(0xFF); // 读 Manufacturer ID
    id[1] = soft_spi_byte(0xFF); // 读 Memory Type
    id[2] = soft_spi_byte(0xFF); // 读 Capacity

    GPIOESET = SPI_CS;          // CS = HIGH (释放)
    delay_us(1);
}

/* ================================================================
 *  初始化: CS/CLK/MOSI 输出, MISO 输入
 * ================================================================ */
static void w25q64_init(void)
{
    // PE4 (CS) → 输出, 初始高 (不选中)
    GPIOEFEN &= ~SPI_CS;
    GPIOEDE  |=  SPI_CS;
    GPIOEDIR &= ~SPI_CS;
    GPIOESET  =  SPI_CS;

    // PE5 (CLK) → 输出, 初始低 (Mode 0)
    GPIOEFEN &= ~SPI_CLK;
    GPIOEDE  |=  SPI_CLK;
    GPIOEDIR &= ~SPI_CLK;
    GPIOECLR  =  SPI_CLK;

    // PE6 (MOSI) → 输出
    GPIOEFEN &= ~SPI_MOSI;
    GPIOEDE  |=  SPI_MOSI;
    GPIOEDIR &= ~SPI_MOSI;

    // PE7 (MISO) → 输入
    GPIOEFEN &= ~SPI_MISO;
    GPIOEDE  |=  SPI_MISO;
    GPIOEDIR |=  SPI_MISO;
}

/* ================================================================
 *  主测试
 * ================================================================ */
void w25q64_test(void)
{
    printf("\n===== W25Q64 Flash Test (Soft SPI) =====\n\n");

    w25q64_init();

    printf("Pins: CS=PE4, CLK=PE5, MOSI=PE6, MISO=PE7\n");
    printf("Wiring to W25Q64 module:\n");
    printf("  PE4(CS)   -> W25Q64 CS\n");
    printf("  PE5(CLK)  -> W25Q64 CLK\n");
    printf("  PE6(MOSI) -> W25Q64 DI\n");
    printf("  PE7(MISO) -> W25Q64 DO\n");
    printf("  3.3V       -> W25Q64 VCC\n");
    printf("  GND        -> W25Q64 GND\n\n");

    u8 id[3];
    w25q64_read_jedec_id(id);

    printf("JEDEC ID: 0x%02X 0x%02X 0x%02X\n", id[0], id[1], id[2]);
    printf("Expected: 0xEF 0x40 0x17 (Winbond W25Q64 8MB)\n\n");

    if (id[0] == 0xEF && id[1] == 0x40 && id[2] == 0x17) {
        printf("MATCH! W25Q64 detected successfully.\n");
    } else if (id[0] == 0x00 && id[1] == 0x00 && id[2] == 0x00) {
        printf("All zeros - check wiring (MISO floating or disconnected).\n");
    } else if (id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF) {
        printf("All 0xFF - check wiring (MISO held HIGH, no device?).\n");
    } else {
        printf("Unknown device - check wiring and connections.\n");
    }

    while (1);
}
