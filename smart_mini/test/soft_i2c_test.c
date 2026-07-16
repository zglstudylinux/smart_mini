/**
 * @file    soft_i2c_test.c
 * @brief   BT892X 软件 GPIO 模拟 I2C 主机 (Bit-Bang)
 *
 *  PE6=SCL, PE7=SDA (PORTE, 已验证可用)
 *  ~100KHz (delay_us(5) ≈ 5µs half-cycle)
 *
 *  I2C 协议:
 *    START: SDA↓ while SCL=H
 *    STOP:  SDA↑ while SCL=H
 *    数据: 8bit MSB first + 1bit ACK
 *
 *  测试: 发送 START + 地址(0x50+W) + 数据(0xA5) + STOP
 *        逻辑分析仪 CH1=SCL, CH2=SDA 观察波形
 */

#include "test.h"

#define SCL_PIN     BIT(6)
#define SDA_PIN     BIT(7)
#define I2C_DELAY   delay_us(5)     // ~5µs → SCL ~100KHz

/* 设置 SDA 为输出, 并写入电平 */
static void sda_out(u8 level) {
    GPIOEDIR &= ~SDA_PIN;           // 输出
    if (level) GPIOESET = SDA_PIN;
    else       GPIOECLR = SDA_PIN;
}

/* 设置 SDA 为输入 (开漏释放, 读ACK或数据) */
static void sda_in(void) {
    GPIOEDIR |= SDA_PIN;            // 输入 (高阻, 靠上拉)
}

/* 读 SDA 电平 */
static u8 sda_read(void) {
    return (GPIOE & SDA_PIN) ? 1 : 0;
}

/* ================================================================
 *  I2C 时序函数
 * ================================================================ */

static void i2c_start(void) {
    sda_out(1);
    GPIOESET = SCL_PIN;  I2C_DELAY;
    sda_out(0);          I2C_DELAY;  // SDA↓ while SCL=H → START
    GPIOECLR = SCL_PIN;  I2C_DELAY;
}

static void i2c_stop(void) {
    sda_out(0);
    GPIOESET = SCL_PIN;  I2C_DELAY;
    sda_out(1);          I2C_DELAY;  // SDA↑ while SCL=H → STOP
}

/* 发送一字节, 返回 ACK (0=ACK, 1=NAK) */
static u8 i2c_write_byte(u8 data) {
    // 8 数据位, MSB first
    for (int i = 7; i >= 0; i--) {
        sda_out((data >> i) & 1);
        I2C_DELAY;
        GPIOESET = SCL_PIN;  I2C_DELAY;  // SCL↑ 锁存数据
        GPIOECLR = SCL_PIN;  I2C_DELAY;  // SCL↓
    }
    // 第9位: ACK
    sda_in();                           // 释放 SDA, 等从机拉低
    I2C_DELAY;
    GPIOESET = SCL_PIN;  I2C_DELAY;     // SCL↑ 读 ACK
    u8 ack = sda_read();
    GPIOECLR = SCL_PIN;  I2C_DELAY;     // SCL↓
    sda_out(0);                         // 恢复 SDA 输出低
    return ack;                         // 0=ACK, 1=NAK
}

/* 接收一字节, ack=0 发送ACK, ack=1 发送NAK */
static u8 __attribute__((unused)) i2c_read_byte(u8 ack) {
    u8 data = 0;
    sda_in();                           // 释放 SDA
    for (int i = 7; i >= 0; i--) {
        I2C_DELAY;
        GPIOESET = SCL_PIN;  I2C_DELAY; // SCL↑
        if (sda_read()) data |= (1 << i);
        GPIOECLR = SCL_PIN;             // SCL↓
    }
    // ACK/NAK
    sda_out(ack);                       // 0=ACK(拉低), 1=NAK(拉高)
    I2C_DELAY;
    GPIOESET = SCL_PIN;  I2C_DELAY;
    GPIOECLR = SCL_PIN;  I2C_DELAY;
    sda_in();
    return data;
}

/* ================================================================
 *  初始化: SCL 输出, SDA 开漏 (先输出低, 需要时切输入)
 * ================================================================ */
static void soft_i2c_init(void)
{
    // SCL (PE6) → 输出
    GPIOEFEN &= ~SCL_PIN;
    GPIOEDE  |=  SCL_PIN;
    GPIOEDIR &= ~SCL_PIN;

    // SDA (PE7) → 输出, 带上拉 (需要读ACK时切输入)
    GPIOEFEN &= ~SDA_PIN;
    GPIOEDE  |=  SDA_PIN;
    GPIOEDIR &= ~SDA_PIN;
    GPIOEPU  |=  SDA_PIN;           // SDA 上拉 (开漏必须)

    // 初始: 总线空闲 (SCL=H, SDA=H)
    GPIOESET = SCL_PIN;
    sda_out(1);
}

/* ================================================================
 *  主测试: 发送 I2C 写事务, 逻辑分析仪观察
 * ================================================================ */
void soft_i2c_test(void)
{
    printf("\n===== BT892X Software I2C (Bit-Bang, ~100KHz) =====\n\n");

    soft_i2c_init();

    printf("SCL: PE6, SDA: PE7\n");
    printf("Logic Analyzer: CH1=SCL, CH2=SDA\n");
    printf("\nSending: START + 0x50(W) + 0xA5 + STOP\n");
    printf("(No slave device, expect NAK on both bytes)\n\n");

    while (1) {
        // I2C 写事务: START → 地址+W → 数据 → STOP
        i2c_start();
        u8 ack1 = i2c_write_byte(0x50 << 1 | 0);   // 7位地址 0x50 + W(0)
        u8 ack2 = i2c_write_byte(0xA5);             // 数据 0xA5
        i2c_stop();

        printf("Addr ACK=%s, Data ACK=%s (1=NAK expected, no slave)\n",
               ack1 ? "NAK" : "ACK",
               ack2 ? "NAK" : "ACK");

        delay_ms(500);
    }
}
