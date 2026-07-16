/**
 * @file    spi_int_test.c
 * @brief   BT892X SPI1 中断 + DMA中断 模式操作 W25Q64
 *  硬件 SPI1 G4: CS=PE4, CLK=PE6, MOSI=PE7, MISO=PE5
 *  模式 3: 硬件 SPI 中断 (SPIIE + ISR)
 *  模式 5: 硬件 SPI DMA + 中断 (DMA传输, ISR通知完成)
 */

#include "test.h"

#define I_CS    BIT(4)
#define I_CLK   BIT(6)
#define I_MOSI  BIT(7)
#define I_MISO  BIT(5)

static volatile int spi_done;

static void spi_isr(void)
{
    SPI1CPND = BIT(16);
    spi_done = 1;
}

static void int_spi_init(u32 baud)
{
    FUNCMCON1 &= ~(0xF << 12);
    FUNCMCON1 |=  (0x4 << 12);

    GPIOEFEN |= I_CLK | I_MOSI | I_MISO;
    GPIOEDE  |= I_CLK | I_MOSI | I_MISO;
    GPIOEDIR &= ~(I_CLK | I_MOSI);
    GPIOEDIR |=  I_MISO;

    GPIOEFEN &= ~I_CS;  GPIOEDE |= I_CS;
    GPIOEDIR &= ~I_CS;  GPIOESET = I_CS;

    SPI1BAUD = baud;
    SPI1CON  = BIT(0);

    register_isr(IRQ_SPI_VECTOR, spi_isr);
    PICEN |= BIT(IRQ_SPI_VECTOR);
}

static u8 int_spi_byte(u8 tx)
{
    spi_done = 0;
    SPI1BUF = tx;
    while (!spi_done);
    return (u8)SPI1BUF;
}

// ====== 模式 3: SPI 中断 ======
static void test_spi_interrupt(void)
{
    printf("\n--- Mode 3: SPI Interrupt ---\n");
    int_spi_init(239);
    SPI1CON |= BIT(7);  // SPIIE

    GPIOECLR = I_CS; delay_us(1);
    int_spi_byte(0x9F);                     // cmd (garbage read)
    u8 id0 = int_spi_byte(0xFF);            // Manufacturer
    u8 id1 = int_spi_byte(0xFF);            // Memory Type
    u8 id2 = int_spi_byte(0xFF);            // Capacity
    GPIOESET = I_CS;
    printf("JEDEC ID: 0x%02X 0x%02X 0x%02X %s\n", id0, id1, id2,
           id0 == 0xEF ? "OK" : "FAIL");

    u8 buf[512];
    (void)buf;
    u32 t0 = tick_get();
    GPIOECLR = I_CS; delay_us(1);
    int_spi_byte(0x03);
    int_spi_byte(0); int_spi_byte(0); int_spi_byte(0);
    for (int i = 0; i < 512; i++) buf[i] = int_spi_byte(0xFF);
    GPIOESET = I_CS;
    printf("Interrupt read 512B: %lu ticks\n", tick_get() - t0);
}

// ====== 模式 5: SPI DMA + 中断 ======
static void test_spi_dma_int(void)
{
    printf("\n--- Mode 5: SPI DMA + Interrupt ---\n");
    int_spi_init(239);
    SPI1CON |= BIT(7);

    #define DBLK 4096
    u8 buf[DBLK];

    // 发送命令+地址 (手动)
    GPIOECLR = I_CS; delay_us(1);
    int_spi_byte(0x03);                     // Read command
    int_spi_byte(0); int_spi_byte(0); int_spi_byte(0);  // Addr=0

    // 切到 DMA 接收, 严格清零标志
    spi_done = 0;
    SPI1CON |= BIT(4);              // RXSEL=1 (接收)
    SPI1DMAADR = (u32)buf;
    SPI1DMACNT = DBLK;              // 启动 DMA

    u32 t0 = tick_get();
    while (!spi_done);              // 等 ISR
    u32 t1 = tick_get();
    SPI1CON &= ~BIT(4);
    GPIOESET = I_CS;
    printf("DMA+Int read %dB: %lu ticks\n", DBLK, t1 - t0);

    // 数据校验
    u8 ref[64];
    GPIOECLR = I_CS; delay_us(1);
    int_spi_byte(0x03); int_spi_byte(0); int_spi_byte(0); int_spi_byte(0);
    for (int i = 0; i < 64; i++) ref[i] = int_spi_byte(0xFF);
    GPIOESET = I_CS;
    int ok = 1;
    for (int i = 0; i < 64; i++) if (buf[i] != ref[i]) { ok = 0; break; }
    printf("Data verify: %s\n\n", ok ? "OK" : "FAIL");
}

void spi_int_test(void)
{
    printf("\n===== SPI1 Interrupt & DMA+Interrupt =====\n");
    test_spi_interrupt();
    test_spi_dma_int();

    printf("=== SPI 5 Modes Summary ===\n");
    printf("1. GPIO   bit-bang SPI polling - DONE\n");
    printf("2. HW     SPI      polling    - DONE\n");
    printf("3. HW     SPI      interrupt  - DONE\n");
    printf("4. HW     SPI      DMA poll   - DONE\n");
    printf("5. HW     SPI      DMA+int    - DONE\n");
    while (1);
}
