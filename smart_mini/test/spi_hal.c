/**
 * @file    spi_hal.c
 * @brief   SPI HAL 实现 —— 软件 bit-bang + 硬件 SPI1 公共原语
 * @note    与 test_spi_loop/wave/w25q64/asm/timing 共享，消除重复
 */

#include "spi_hal.h"

/* ===================== 软件 bit-bang =====================
 * 手动 GPIO 模拟 SPI Mode 0（CPOL=0, CPHA=0, MSB-first）
 *
 *  时序（每 bit ~3µs，3 次 delay_us(1)）：
 *    1. 设 MOSI（先于 CLK）
 *    2. delay_us(1) - 满足 data setup time
 *    3. CLK 上升沿 (delay_us(1) 间) - 主机此时采样 MISO
 *    4. CLK 下降沿 (delay_us(1) 间) - 然后切下一 bit
 *
 *  手册依据：BT892X_UserManual_Driver.md
 *    §3.2 GPIO 通用控制寄存器（每步对应 GPIOEDE/FEN/DIR/SET/CLR/读）
 *    §5.2 SPI1 引脚映射
 */
void spi_hal_soft_init(void)
{
    /* CS/CLK/MOSI 设为普通 GPIO 输出；MISO 设为输入 */
    GPIOEFEN &= ~(SPI_CS_PIN | SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN);  /* §3.2: FEN=0 用作 GPIO */
    GPIOEDE  |=  (SPI_CS_PIN | SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN);  /* §3.2: DE=1 数字 IO */

    /* 软件态下 CS/CLK/MOSI 输出，MISO 输入 */
    GPIOEDIR &= ~(SPI_CS_PIN | SPI_CLK_PIN | SPI_MOSI_PIN);  /* CS/CLK/MOSI 输出 */
    GPIOEDIR |=   SPI_MISO_PIN;                           /* MISO 输入 */

    GPIOESET  =   SPI_CS_PIN;    /* CS 空闲高 */
    GPIOECLR  =   SPI_CLK_PIN;   /* CLK 空闲低（Mode 0） */
}

u8 spi_hal_soft_byte(u8 tx)
{
    u8 rx = 0;
    for (int i = 7; i >= 0; i--) {
        if (tx & (1 << i)) GPIOESET = SPI_MOSI_PIN;  /* §3.2: MOSI=1 */
        else               GPIOECLR = SPI_MOSI_PIN;  /* §3.2: MOSI=0 */
        delay_us(1);
        GPIOESET = SPI_CLK_PIN; delay_us(1);         /* §3.2: CLK 上升沿 */
        if (GPIOE & SPI_MISO_PIN) rx |= (1 << i);    /* §3.2: 中心采样 MISO */
        GPIOECLR = SPI_CLK_PIN; delay_us(1);         /* §3.2: CLK 下降沿 */
    }
    return rx;
}

/* ===================== 硬件 SPI1 =====================
 * BT892X 硬件 SPI1 控制器，Mode 0 主机，G4 组（PE6/PE7/PE5）
 *
 *  手册依据：
 *    §3.3 FUNCMCON1[15:12] = SPI1MAP，0100=G4
 *    §7.2 SPIxCON (SPISM=0 BUSMODE=00 SPIEN=1 默认 Mode 0)
 *    §7.2 SPIxBAUD: 波特率 = Fsys / (BAUD + 1)
 *    §7.2 SPIxBUF: 写启动 / 读接收
 *    §7.2 SPIxCPND: 写 1 清挂起
 */
void spi_hal_hw_init(u32 baud)
{
    /* 引脚映射 G4 (FUNCMCON1[15:12]=0x4) */
    FUNCMCON1 &= ~(0xF << 12);
    FUNCMCON1 |=  (0x4 << 12);

    /* CLK/MOSI/MISO → 功能 IO；CS 用 GPIO 手动控制（§3.2） */
    GPIOEFEN |= SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN;
    GPIOEDE  |= SPI_CLK_PIN | SPI_MOSI_PIN | SPI_MISO_PIN;
    GPIOEDIR &= ~(SPI_CLK_PIN | SPI_MOSI_PIN);   /* CLK/MOSI 输出 */
    GPIOEDIR |=   SPI_MISO_PIN;                   /* MISO 输入 */

    GPIOEFEN &= ~SPI_CS_PIN;         /* §3.2: CS 用普通 GPIO */
    GPIOEDE  |=  SPI_CS_PIN;
    GPIOEDIR &= ~SPI_CS_PIN;
    GPIOESET  =  SPI_CS_PIN;         /* CS 空闲高 */

    /* §7.2 SPI1BAUD = Fsys / (BAUD + 1) */
    SPI1BAUD = baud;       /* 100kHz = 239, 12MHz = 1 */
    SPI1CON  = BIT(0);     /* §7.2: SPIEN=1 (Mode 0 其他位默认 0) */
}

u8 spi_hal_hw_byte(u8 tx)
{
    SPI1BUF = tx;                       /* §7.2: 写启动 */
    while (!(SPI1CON & BIT(16)));        /* §7.2: 等 SPIPND=1 */
    SPI1CPND = BIT(16);                  /* §7.2: 写 1 清挂起 */
    return (u8)SPI1BUF;                  /* §7.2: 读接收 */
}

/* ===================== CS 控制 ===================== */
void spi_hal_cs_low(void)
{
    GPIOECLR = SPI_CS_PIN;
    delay_us(1);   /* SPI 时序要求：CS 拉低后稍作延迟 */
}

void spi_hal_cs_high(void)
{
    GPIOESET = SPI_CS_PIN;
    delay_us(1);
}

/* =====================================================================
 *  W25Q64 Flash 驱动层（按 bus 分两套实现，软/硬共用 CS 原语）
 *
 *  命令时序参考 Winbond W25Q64 datasheet；SPI Mode 0, MSB-first
 *  每条命令流程：CS_LOW → 命令(1B) → 地址(0/3B) → 数据(0/N B) → CS_HIGH
 *  写操作后需等 SR1[0] (BUSY) 清零 (spi_hal_w25_*_wait_busy)
 * ===================================================================== */

/* ===== 软件 bit-bang 版 ===== */
void spi_hal_w25_sw_write_enable(void)
{
    spi_hal_cs_low();
    spi_hal_soft_byte(0x06);    /* Write Enable 命令 */
    spi_hal_cs_high();
}

u8 spi_hal_w25_sw_read_status(u8 cmd)
{
    u8 s;
    spi_hal_cs_low();
    spi_hal_soft_byte(cmd);      /* 0x05=SR1, 0x35=SR2 */
    s = spi_hal_soft_byte(0xFF);
    spi_hal_cs_high();
    return s;
}

void spi_hal_w25_sw_wait_busy(void)
{
    while (spi_hal_w25_sw_read_status(0x05) & 0x01);   /* BUSY 位清零才退出 */
}

void spi_hal_w25_sw_read_data(u32 addr, u8 *buf, u32 len)
{
    spi_hal_cs_low();
    spi_hal_soft_byte(0x03);                           /* Read Data 命令 */
    spi_hal_soft_byte((u8)(addr >> 16));
    spi_hal_soft_byte((u8)(addr >> 8));
    spi_hal_soft_byte((u8)(addr));
    for (u32 i = 0; i < len; i++) buf[i] = spi_hal_soft_byte(0xFF);
    spi_hal_cs_high();
}

void spi_hal_w25_sw_page_program(u32 addr, const u8 *buf, u32 len)
{
    spi_hal_w25_sw_write_enable();
    spi_hal_cs_low();
    spi_hal_soft_byte(0x02);                           /* Page Program 命令 */
    spi_hal_soft_byte((u8)(addr >> 16));
    spi_hal_soft_byte((u8)(addr >> 8));
    spi_hal_soft_byte((u8)(addr));
    for (u32 i = 0; i < len; i++) spi_hal_soft_byte(buf[i]);
    spi_hal_cs_high();
    spi_hal_w25_sw_wait_busy();
}

void spi_hal_w25_sw_sector_erase(u32 addr)
{
    spi_hal_w25_sw_write_enable();
    spi_hal_cs_low();
    spi_hal_soft_byte(0x20);                           /* Sector Erase 4KB */
    spi_hal_soft_byte((u8)(addr >> 16));
    spi_hal_soft_byte((u8)(addr >> 8));
    spi_hal_soft_byte((u8)(addr));
    spi_hal_cs_high();
    spi_hal_w25_sw_wait_busy();
}

/* ===== 硬件 SPI1 版 ===== */
void spi_hal_w25_hw_write_enable(void)
{
    spi_hal_cs_low();
    spi_hal_hw_byte(0x06);
    spi_hal_cs_high();
}

u8 spi_hal_w25_hw_read_status(u8 cmd)
{
    u8 s;
    spi_hal_cs_low();
    spi_hal_hw_byte(cmd);
    s = spi_hal_hw_byte(0xFF);
    spi_hal_cs_high();
    return s;
}

void spi_hal_w25_hw_wait_busy(void)
{
    while (spi_hal_w25_hw_read_status(0x05) & 1);
}

void spi_hal_w25_hw_read_data(u32 addr, u8 *buf, u32 len)
{
    spi_hal_cs_low();
    spi_hal_hw_byte(0x03);
    spi_hal_hw_byte((u8)(addr >> 16));
    spi_hal_hw_byte((u8)(addr >> 8));
    spi_hal_hw_byte((u8)(addr));
    for (u32 i = 0; i < len; i++) buf[i] = spi_hal_hw_byte(0xFF);
    spi_hal_cs_high();
}

void spi_hal_w25_hw_page_program(u32 addr, const u8 *buf, u32 len)
{
    spi_hal_w25_hw_write_enable();
    spi_hal_cs_low();
    spi_hal_hw_byte(0x02);
    spi_hal_hw_byte((u8)(addr >> 16));
    spi_hal_hw_byte((u8)(addr >> 8));
    spi_hal_hw_byte((u8)(addr));
    for (u32 i = 0; i < len; i++) spi_hal_hw_byte(buf[i]);
    spi_hal_cs_high();
    spi_hal_w25_hw_wait_busy();
}

void spi_hal_w25_hw_sector_erase(u32 addr)
{
    spi_hal_w25_hw_write_enable();
    spi_hal_cs_low();
    spi_hal_hw_byte(0x20);
    spi_hal_hw_byte((u8)(addr >> 16));
    spi_hal_hw_byte((u8)(addr >> 8));
    spi_hal_hw_byte((u8)(addr));
    spi_hal_cs_high();
    spi_hal_w25_hw_wait_busy();
}

/* =====================================================================
 *  硬件 SPI1 - Interrupt / DMA 高层原语（供 timing 测试等）
 *
 *  Interrupt 模式流程（手册 §7.2 SPI1CON[7] SPIIE + §6 节）：
 *    1. setup: SPI1CON |= BIT(7); register_isr(...); PICEN |= BIT(vector)
 *    2. byte_it: SPI1BUF = tx; while(!spi_done); (ISR sets spi_done)
 *    3. teardown: PICEN &= ~BIT(vector); SPI1CON &= ~BIT(7)
 *
 *  DMA 模式流程（手册 §7.3）：
 *    读 - 命令+地址用手发，RXSEL=1(SPI1CON[4]=1)，写 SPI1DMAADR/CNT 触发
 *    写 - 命令+地址用手发，RXSEL=0，写 SPI1DMAADR/CNT 触发
 * ===================================================================== */

/* ===== Interrupt 模式 ===== */
static volatile int spi_hal_spi_done;

AT(.com_text.isr)
static void spi_hal_spi_isr(void)
{
    SPI1CPND = BIT(16);     /* §7.2 清挂起 */
    spi_hal_spi_done = 1;
}

void spi_hal_hw_it_setup(void)
{
    SPI1CON |= BIT(7);                                /* SPIIE */
    register_isr(IRQ_SPI_VECTOR, spi_hal_spi_isr);
    PICEN |= BIT(IRQ_SPI_VECTOR);
}

void spi_hal_hw_it_teardown(void)
{
    PICEN &= ~BIT(IRQ_SPI_VECTOR);
    SPI1CON &= ~BIT(7);
}

u8 spi_hal_hw_byte_it(u8 tx)
{
    spi_hal_spi_done = 0;
    SPI1BUF = tx;
    while (!spi_hal_spi_done);          /* 阻塞等 ISR */
    return (u8)SPI1BUF;
}

/* ===== DMA 模式 ===== */
static void spi_hal_hw_read_dma_cmd(u32 addr)
{
    /* CS low + 命令+地址走 polling（手册 §7.3） */
    spi_hal_cs_low();
    spi_hal_hw_byte(0x03);
    spi_hal_hw_byte((u8)(addr >> 16));
    spi_hal_hw_byte((u8)(addr >> 8));
    spi_hal_hw_byte((u8)(addr));
}

void spi_hal_hw_read_dma(u32 addr, u8 *buf, u32 len)
{
    spi_hal_hw_read_dma_cmd(addr);
    /* 切到 DMA 接收：RXSEL=1（手册 §7.2 SPI1CON[4]=1） */
    SPI1CON |= BIT(4);
    SPI1DMAADR = (u32)buf;
    SPI1DMACNT = len;
    while (!(SPI1CON & BIT(16)));     /* §7.2 等 SPIPND */
    SPI1CPND = BIT(16);
    SPI1CON &= ~BIT(4);               /* 恢复 RXSEL=0 */
    spi_hal_cs_high();
}

void spi_hal_hw_write_dma(u32 addr, const u8 *buf, u32 len)
{
    spi_hal_cs_low();
    spi_hal_hw_byte(0x02);                           /* Page Program */
    spi_hal_hw_byte((u8)(addr >> 16));
    spi_hal_hw_byte((u8)(addr >> 8));
    spi_hal_hw_byte((u8)(addr));
    /* 切到 DMA 发送：RXSEL=0 */
    SPI1CON &= ~BIT(4);
    SPI1DMAADR = (u32)buf;
    SPI1DMACNT = len;
    while (!(SPI1CON & BIT(16)));
    SPI1CPND = BIT(16);
    spi_hal_cs_high();
}
