/**
 * @file    i2c_hal.c
 * @brief   I2C HAL 实现 —— 软件 bit-bang + 硬件 IIC + AT24C02 驱动
 *
 *  手册依据：BT892X_UserManual_Driver.md
 *    §3.2 GPIO 通用控制寄存器（PE6/7 配置）
 *    §3.3 FUNCMCON2[27:24] = IIC Group（0101=G5）
 *    §8.1 IIC 概述 + §8.2 IIC 寄存器 + §8.3 IIC 使用步骤
 *
 *  之前 4 个 test_i2c*.c 文件的 init / probe / write / read 全是
 *  寄存器级裸代码，本 HAL 抽出来后只保留一份。
 */

#include "i2c_hal.h"

/* =====================================================================
 *  软件 bit-bang 实现（PE6=SCL 输出，PE7=SDA 开漏模拟 DIR 切换）
 *
 *  时序（Standard mode ~100 kHz，5+1+5+1 ≈ 12µs/bit ≈ 83 kHz）：
 *    1. SDA 切换 → delay_us(1) data setup
 *    2. SCL HIGH → delay_us(5) → 主机/从机采样 SDA
 *    3. SCL LOW  → delay_us(5)
 *
 *  设计：SCL 永远输出，SDA 用 DIR 切换模拟开漏（释放=输入，靠上拉拉高）
 * ===================================================================== */

// SCL 是输出（一直）
#define I2C_SCL_OUT()      do { GPIOEDIR &= ~I2C_SCL_PIN; } while (0)
#define I2C_SCL_HIGH()     do { I2C_SCL_OUT(); GPIOESET = I2C_SCL_PIN; } while (0)
#define I2C_SCL_LOW()      do { I2C_SCL_OUT(); GPIOECLR = I2C_SCL_PIN; } while (0)

// SDA 开漏：DIR 切换
#define I2C_SDA_OUT()      do { GPIOEDIR &= ~I2C_SDA_PIN; } while (0)
#define I2C_SDA_IN()       do { GPIOEDIR |=  I2C_SDA_PIN; } while (0)
#define I2C_SDA_OUT_HIGH() do { I2C_SDA_OUT(); GPIOESET = I2C_SDA_PIN; } while (0)
#define I2C_SDA_OUT_LOW()  do { I2C_SDA_OUT(); GPIOECLR = I2C_SDA_PIN; } while (0)
#define I2C_SDA_READ()     ((GPIOE & I2C_SDA_PIN) ? 1 : 0)

// 半周期延迟（Standard mode）
#define I2C_DELAY()        delay_us(5)
#define I2C_TSU()          delay_us(1)

void i2c_hal_soft_init(void)
{
    // PE6/7: digital IO, FEN=0 (GPIO 模式), 上拉使能
    GPIOEDE  |= I2C_PIN_MASK;
    GPIOEFEN &= ~I2C_PIN_MASK;
    GPIOEPU  |= I2C_PIN_MASK;
    GPIOEPD  &= ~I2C_PIN_MASK;
    // 总线 idle 高
    I2C_SDA_OUT_HIGH();
    I2C_SCL_HIGH();
    delay_us(100);
}

void i2c_hal_soft_start(void)
{
    // 起始条件：SCL 高时 SDA 下降
    I2C_SDA_OUT_HIGH();
    I2C_SCL_HIGH();
    I2C_DELAY();
    I2C_SDA_OUT_LOW();
    I2C_DELAY();
    I2C_SCL_LOW();
    I2C_DELAY();
}

void i2c_hal_soft_stop(void)
{
    // 停止条件：SCL 高时 SDA 上升
    I2C_SDA_OUT_LOW();
    I2C_DELAY();
    I2C_SCL_HIGH();
    I2C_DELAY();
    I2C_SDA_OUT_HIGH();
    I2C_DELAY();
}

bool i2c_hal_soft_write_byte(u8 data)
{
    for (u8 i = 0; i < 8; i++) {
        if (data & 0x80) I2C_SDA_OUT_HIGH();
        else             I2C_SDA_OUT_LOW();
        I2C_TSU();
        I2C_SCL_HIGH(); I2C_DELAY();
        I2C_SCL_LOW();  I2C_DELAY();
        data <<= 1;
    }
    // ACK 时隙：释放 SDA 让从机驱动
    I2C_SDA_IN();
    I2C_TSU();
    I2C_SCL_HIGH(); I2C_DELAY();
    bool ack = (I2C_SDA_READ() == 0);  // 0=ACK
    I2C_SCL_LOW();  I2C_DELAY();
    return ack;
}

u8 i2c_hal_soft_read_byte(bool send_ack)
{
    u8 val = 0;
    I2C_SDA_IN();  // 释放 SDA
    for (u8 i = 0; i < 8; i++) {
        I2C_SCL_HIGH(); I2C_DELAY();
        val = (val << 1) | I2C_SDA_READ();
        I2C_SCL_LOW();  I2C_DELAY();
    }
    // Master ACK/NAK
    I2C_SDA_OUT();
    if (send_ack) I2C_SDA_OUT_LOW();
    else          I2C_SDA_OUT_HIGH();
    I2C_TSU();
    I2C_SCL_HIGH(); I2C_DELAY();
    I2C_SCL_LOW();  I2C_DELAY();
    I2C_SDA_OUT_HIGH();  // 释放
    return val;
}

/* ===================== 软件 bit-bang 高层 transaction ===================== */

bool i2c_hal_soft_probe_addr(u8 dev_addr7, bool is_read)
{
    i2c_hal_soft_start();
    bool ack = i2c_hal_soft_write_byte((u8)((dev_addr7 << 1) | (is_read ? 1u : 0u)));
    i2c_hal_soft_stop();
    return ack;
}

bool i2c_hal_soft_write(u8 dev_addr7, u8 reg_addr, const u8 *data, u8 len)
{
    if (len == 0 || len > 4) return false;
    i2c_hal_soft_start();
    if (!i2c_hal_soft_write_byte((u8)((dev_addr7 << 1) | 0u))) { i2c_hal_soft_stop(); return false; }
    if (!i2c_hal_soft_write_byte(reg_addr))                  { i2c_hal_soft_stop(); return false; }
    for (u8 i = 0; i < len; i++) {
        if (!i2c_hal_soft_write_byte(data[i]))                { i2c_hal_soft_stop(); return false; }
    }
    i2c_hal_soft_stop();
    return true;
}

bool i2c_hal_soft_read(u8 dev_addr7, u8 reg_addr, u8 *buf, u8 len)
{
    if (len == 0 || len > 4) return false;
    i2c_hal_soft_start();
    if (!i2c_hal_soft_write_byte((u8)((dev_addr7 << 1) | 0u))) { i2c_hal_soft_stop(); return false; }
    if (!i2c_hal_soft_write_byte(reg_addr))                  { i2c_hal_soft_stop(); return false; }
    i2c_hal_soft_start();   // Sr
    if (!i2c_hal_soft_write_byte((u8)((dev_addr7 << 1) | 1u))) { i2c_hal_soft_stop(); return false; }
    for (u8 i = 0; i < len - 1; i++) buf[i] = i2c_hal_soft_read_byte(true);   // ACK
    buf[len - 1] = i2c_hal_soft_read_byte(false);                            // NAK (last byte)
    i2c_hal_soft_stop();
    return true;
}

/* =====================================================================
 *  硬件 IIC 实现（PE6/PE7 G5 映射 → 硬件 IIC 控制器）
 *
 *  手册 §8.3 步骤：
 *    1. 开 CLKGAT2[0] = IIC 时钟门
 *    2. PE6/7 PAD: digital + pull-up
 *    3. FUNCMCON2[27:24] = 0101 (G5) 选 PE6 SCL + PE7 SDA
 *    4. IICCON0 配置 + CLR_ALL
 *    5. 装 IICCMDA (CTL0 + ADR0) + IICDATA (data)
 *    6. IICCON1 装动作序列
 *    7. IICCON0[28] KS=1 kick start
 *    8. 等 IICCON0[31] DONE=1（带 timeout）
 *    9. 读 ACKSTATUS，写 CLR_DONE 清挂起
 * ===================================================================== */

void i2c_hal_hw_init(void)
{
    // Step 1: 开 IIC 时钟门
    CLKGAT2 |= BIT(0);

    // Step 2: PE6/7 PAD
    GPIOEDE  |= I2C_PIN_MASK;
    GPIOEFEN |= I2C_PIN_MASK;   // 硬件接管
    GPIOEPU  |= I2C_PIN_MASK;
    GPIOEPD  &= ~I2C_PIN_MASK;
    GPIOEDIR &= ~I2C_PIN_MASK;

    // Step 3: G5 映射（手册 §3.3 L165 + §4.3）
    FUNCMCON2 = (FUNCMCON2 & ~(0xFu << 24)) | (0x5u << 24);

    // Step 4: IICCON0 + CLR_ALL（手册 §8.2 表：HOLDCNT=0, POSDIV=19, IIC_EN=1）
    IICCON0 = (0u << 2) | (19u << 4) | I2C_HW_EN;
    IICCON0 |= I2C_HW_CLR_ALL;
    delay_us(100);
}

bool i2c_hal_hw_wait_done(u32 timeout_us)
{
    u32 t0 = TMR2CNT;
    while (!(IICCON0 & I2C_HW_DONE)) {
        if (timeout_us > 0 && (u32)(TMR2CNT - t0) > timeout_us) {
            return false;
        }
    }
    return true;
}

bool i2c_hal_hw_probe_addr(u8 dev_addr7, bool is_read, u32 timeout_us)
{
    // Step 5: CTL0 = address+R/W（仅 probe，无 sub-addr/数据）
    IICCMDA = (u8)((dev_addr7 << 1) | (is_read ? 1u : 0u));

    // Step 6: START + CTL0 + STOP（无数据）
    IICCON1 = I2C_HW_START0_EN | I2C_HW_CTL0_EN | I2C_HW_STOP_EN | 0;

    // Step 7: kick start
    IICCON0 |= I2C_HW_KS;

    if (!i2c_hal_hw_wait_done(timeout_us)) {
        IICCON0 |= I2C_HW_CLR_DONE;
        return false;
    }
    // Step 8/9: 读 ACK + 清 DONE
    bool ack = !(IICCON0 & I2C_HW_ACKSTATUS);
    IICCON0 |= I2C_HW_CLR_DONE;
    return ack;
}

bool i2c_hal_hw_write(u8 dev_addr7, u8 reg_addr,
                       const u8 *data, u8 len, u32 timeout_us)
{
    if (len == 0 || len > 4) return false;

    // Step 5: IICCMDA[7:0] = CTL0=addr+W, [15:8] = ADR0=sub-addr
    IICCMDA = (u8)((dev_addr7 << 1) & 0xFF)
            | ((u32)reg_addr << 8);

    // Step 5b: IICDATA 把数据打包（len 字节）
    u32 data_word = 0;
    for (u8 i = 0; i < len; i++) {
        data_word |= ((u32)data[i]) << (i * 8);
    }
    IICDATA = data_word;

    // Step 6: START + CTL0 + ADR0 + WDAT + STOP
    IICCON1 = I2C_HW_START0_EN | I2C_HW_CTL0_EN | I2C_HW_ADR0_EN
            | I2C_HW_WDAT_EN | I2C_HW_STOP_EN
            | (len & 0x7);   // DATA_CNT

    // Step 7
    IICCON0 |= I2C_HW_KS;

    if (!i2c_hal_hw_wait_done(timeout_us)) {
        IICCON0 |= I2C_HW_CLR_DONE;
        return false;
    }
    bool ack = !(IICCON0 & I2C_HW_ACKSTATUS);
    IICCON0 |= I2C_HW_CLR_DONE;
    return ack;
}

bool i2c_hal_hw_read(u8 dev_addr7, u8 reg_addr,
                      u8 *buf, u8 len, u32 timeout_us)
{
    if (len == 0 || len > 4) return false;

    // Step 5: CTL0 + ADR0
    IICCMDA = (u8)((dev_addr7 << 1) & 0xFF)
            | ((u32)reg_addr << 8);
    // CTL1 (repeated START 后的第二地址) = [31:24]
    IICCMDA |= (u32)(((dev_addr7 << 1) | 1u) & 0xFF) << 24;
    IICDATA = 0;  // placeholder

    // Step 6: START0 + CTL0 + ADR0 + START1 + CTL1 + RDAT + STOP + TXNAK(最后字节)
    IICCON1 = I2C_HW_START0_EN | I2C_HW_CTL0_EN | I2C_HW_ADR0_EN
            | I2C_HW_START1_EN | I2C_HW_CTL1_EN
            | I2C_HW_RDAT_EN | I2C_HW_STOP_EN
            | I2C_HW_TXNAK_EN
            | (len & 0x7);

    IICCON0 |= I2C_HW_KS;

    if (!i2c_hal_hw_wait_done(timeout_us)) {
        IICCON0 |= I2C_HW_CLR_DONE;
        return false;
    }
    // 拆 DATA0 = 收到的字节
    u32 data_word = IICDATA;
    for (u8 i = 0; i < len; i++) {
        buf[i] = (u8)((data_word >> (i * 8)) & 0xFF);
    }
    bool ack = !(IICCON0 & I2C_HW_ACKSTATUS);
    IICCON0 |= I2C_HW_CLR_DONE;
    return ack;
}

/* =====================================================================
 *  AT24C02 业务封装（软/硬共用 API）
 *  AT24C02 page write = 8 字节一次（手册），本工程 1~4 字节调用
 *  写周期 ≤ 5ms（datasheet）—— write_byte 已内置 delay_ms(10) 等待
 * ===================================================================== */

bool at24c02_hw_probe(u8 addr7) {
    return i2c_hal_hw_probe_addr(addr7, false, 100000);
}

u32 at24c02_hw_scan(u8 addr_lo, u8 addr_hi) {
    u32 found = 0;
    for (u32 a = addr_lo; a <= addr_hi; a++) {
        if (i2c_hal_hw_probe_addr((u8)a, false, 50000)) found++;
    }
    return found;
}

bool at24c02_hw_write_byte(u8 reg, u8 val) {
    bool ok = i2c_hal_hw_write(AT24C02_ADDR, reg, &val, 1, 100000);
    if (ok) delay_ms(10);   // write cycle
    return ok;
}

bool at24c02_hw_read_byte(u8 reg, u8 *out) {
    return i2c_hal_hw_read(AT24C02_ADDR, reg, out, 1, 100000);
}

bool at24c02_hw_write_bytes(u8 reg, const u8 *buf, u8 len) {
    bool ok = i2c_hal_hw_write(AT24C02_ADDR, reg, buf, len, 100000);
    if (ok) delay_ms(10);
    return ok;
}

bool at24c02_hw_read_bytes(u8 reg, u8 *buf, u8 len) {
    return i2c_hal_hw_read(AT24C02_ADDR, reg, buf, len, 100000);
}

bool at24c02_soft_probe(u8 addr7) {
    return i2c_hal_soft_probe_addr(addr7, false);
}

u32 at24c02_soft_scan(u8 addr_lo, u8 addr_hi) {
    u32 found = 0;
    for (u32 a = addr_lo; a <= addr_hi; a++) {
        if (i2c_hal_soft_probe_addr((u8)a, false)) found++;
    }
    return found;
}

bool at24c02_soft_write_byte(u8 reg, u8 val) {
    bool ok = i2c_hal_soft_write(AT24C02_ADDR, reg, &val, 1);
    if (ok) delay_ms(10);
    return ok;
}

bool at24c02_soft_read_byte(u8 reg, u8 *out) {
    return i2c_hal_soft_read(AT24C02_ADDR, reg, out, 1);
}

bool at24c02_soft_write_bytes(u8 reg, const u8 *buf, u8 len) {
    bool ok = i2c_hal_soft_write(AT24C02_ADDR, reg, buf, len);
    if (ok) delay_ms(10);
    return ok;
}

bool at24c02_soft_read_bytes(u8 reg, u8 *buf, u8 len) {
    return i2c_hal_soft_read(AT24C02_ADDR, reg, buf, len);
}