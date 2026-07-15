/**
 * @file    spi_test.c
 * @brief   BT892X SPI1 主机模式测试
 *
 * 测试内容:
 *   SPI1 G4: CLK=PE6, MOSI=PE7, MISO=PE5
 *   1. 发送 0x55/0xAA/0x00/0xFF, 逻辑分析仪抓 CLK+MOSI 波形
 *   2. 跳线 MOSI↔MISO 回环收发 256 字节
 *
 * 参考手册: BT892X_UserManual_Driver.md §7 SPI
 * 引脚手册: bt892x_pinfunction.md
 *
 * SPI 模式 0: CLKIDS=0(空闲低), SMPS=0(下降沿输出), SPIOSS=0
 * 波特率: 1MHz (SPI1BAUD = 24MHz/1MHz - 1 = 23)
 */

#include "test.h"

static void spi1_init(void)
{
    // ================================================================
    // 引脚映射: SPI1 G4 (FUNCMCON1[15:12] = 0x4)
    //   CLK=PE6, MOSI(DO)=PE7, MISO(DI)=PE5
    // ================================================================
    FUNCMCON1 &= ~(0xF << 12);
    FUNCMCON1 |=  (0x4 << 12);          // SPI1MAP = G4

    // PE6 → SPI1 CLK (功能IO, 输出)
    GPIOEFEN |=  BIT(6);
    GPIOEDE  |=  BIT(6);
    GPIOEDIR &= ~BIT(6);

    // PE7 → SPI1 MOSI/DO (功能IO, 输出)
    GPIOEFEN |=  BIT(7);
    GPIOEDE  |=  BIT(7);
    GPIOEDIR &= ~BIT(7);

    // PE5 → SPI1 MISO/DI (功能IO, 输入)
    GPIOEFEN |=  BIT(5);
    GPIOEDE  |=  BIT(5);
    GPIOEDIR |=  BIT(5);

    // ================================================================
    // SPI1 配置: 模式0, 主机, 3线, 1MHz
    //   CLKIDS=0(空闲低), SMPS=0(下降沿输出), SPIOSS=0
    //   BUSMODE=00(3线), SPISM=0(主机)
    // ================================================================
    SPI1BAUD = 239;                     // 24MHz / (239+1) = 100KHz (便于逻辑分析仪观察)

    SPI1CON = (0 << 10)                 // SPIOSS=0
            | (0 << 6)                  // SMPS=0 (下降沿输出)
            | (0 << 5)                  // CLKIDS=0 (空闲低)
            | (0 << 2)                  // BUSMODE=00 (3线)
            | (0 << 1)                  // SPISM=0 (主机)
            | BIT(0);                   // SPIEN
}

/*  SPI 收发一个字节 (主机模式, 阻塞) */
static u8 spi1_transfer(u8 tx_data)
{
    SPI1BUF = tx_data;                  // 写数据, 启动传输
    while (!(SPI1CON & BIT(16)));       // 等待 SPIPND=1 (传输完成)
    SPI1CPND = BIT(16);                 // 清除挂起
    return (u8)SPI1BUF;                 // 读取接收数据
}

// ================================================================
//  主测试
// ================================================================
void soft_spi_test(void)
{
    printf("\n===== BT892X SPI1 Test (Mode 0, 1MHz) =====\n\n");

    spi1_init();

    printf("SPI1 G4: CLK=PE6, MOSI=PE7, MISO=PE5\n");
    printf("Mode 0 (CPOL=0, CPHA=0), Master, 100KHz\n\n");

    // ================================================================
    // 测试1: 发送测试模式, 逻辑分析仪观察
    //   CH1: PE6(CLK), CH2: PE7(MOSI)
    // ================================================================
    printf("--- Test 1: Send pattern (logic analyzer) ---\n");
    printf("CH1=PE6(CLK), CH2=PE7(MOSI)\n");

    u8 patterns[] = { 0x55, 0xAA, 0x00, 0xFF, 0xF0, 0x0F };
    for (int i = 0; i < 6; i++) {
        spi1_transfer(patterns[i]);
        delay_ms(200);
        printf("  Sent: 0x%02X\n", patterns[i]);
    }

    // ================================================================
    // 测试2: 回环测试 (跳线 PE7↔PE5)
    // ================================================================
    printf("\n--- Test 2: Loopback (jumper PE7(MOSI) <-> PE5(MISO)) ---\n");

    int errors = 0, total = 0;
    for (int val = 0; val <= 0xFF; val++) {
        u8 rx = spi1_transfer((u8)val);
        total++;
        if (rx != (u8)val) {
            errors++;
            if (errors <= 5)
                printf("ERR: sent=0x%02X, rx=0x%02X\n", val, rx);
        }
    }

    printf("\n===== SPI1 Loopback Result =====\n");
    printf("Total: %d, Errors: %d\n", total, errors);
    printf(errors == 0 ? "ALL PASSED!\n" : "FAILED\n");

    // ================================================================
    // 持续发送 0x55 供逻辑分析仪观察
    // ================================================================
    printf("\nSending 0x55 continuously for logic analyzer...\n");
    while (1) {
        spi1_transfer(0x55);
        delay_us(10);
    }
}
