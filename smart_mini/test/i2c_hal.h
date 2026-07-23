/**
 * @file    i2c_hal.h
 * @brief   I2C HAL —— 软件 bit-bang + 硬件 IIC 公共原语 + AT24C02 驱动
 *
 *  设计原则（仿 [spi_hal.h](spi_hal.h) 浅重构模式）：
 *    * 抽底层原语 + 高层 transaction，测试代码只调用不复写寄存器
 *    * 测试代码区分 soft/hw (template-style)，不引入运行时多态
 *    * 仅抽象 BT892X 当前 IIC + AT24C02 子地址模式，未来换芯片再独立处理
 *
 *  引脚约定（与硬件 IIC G5 对齐，PE 端口）：
 *    PE6 = SCL  (硬件 IIC 接管；软件 bit-bang 时 GPIO 输出)
 *    PE7 = SDA  (硬件 IIC 接管；软件 bit-bang 时 GPIO 开漏模拟)
 *
 *  AT24C02 (256-byte EEPROM) 子地址模式：
 *    - 7-bit slave address = 0x50 (A0=A1=A2=GND)
 *    - 1-byte sub-address (0x00..0xFF)，单页 8 字节 (page write ≤ 8)
 *    - 写周期 ≤ 5ms (datasheet)
 */

#ifndef _I2C_HAL_H_
#define _I2C_HAL_H_

#include "test_common.h"   // GPIO 宏、delay_us、tick_get 等

// ===================== 引脚宏 =====================
#define I2C_SCL_PIN    BIT(6)   // PE6
#define I2C_SDA_PIN    BIT(7)   // PE7
#define I2C_PIN_MASK   (I2C_SCL_PIN | I2C_SDA_PIN)

// ===================== AT24C02 slave 地址 =====================
#define AT24C02_ADDR   0x50        // 7-bit address (A0=A1=A2=GND)

// ===================== 软件 bit-bang 原语（~80~100 kHz Standard mode） =====================
// 初始化：PE6=输出/PE7=开漏模拟，FEN=0（关硬件接管）
void i2c_hal_soft_init(void);
// START: SDA falls while SCL high
void i2c_hal_soft_start(void);
// STOP: SDA rises while SCL high
void i2c_hal_soft_stop(void);
// 写一字节（MSB-first），返回 true=ACK / false=NAK
bool i2c_hal_soft_write_byte(u8 data);
// 读一字节；send_ack=true → master ACK, false → NAK（最后字节）
u8  i2c_hal_soft_read_byte(bool send_ack);

// ===================== 硬件 IIC 原语 =====================
// IICCON0/1 位定义（手册 §8.2 表）
#define I2C_HW_EN        BIT(0)   // IICCON0[0]  - IIC 主使能
#define I2C_HW_INTEN     BIT(1)   // IICCON0[1]  - 中断使能（本工程不用）
#define I2C_HW_CLR_ALL   BIT(27)  // IICCON0[27] - 清除全部状态（W）
#define I2C_HW_KS        BIT(28)  // IICCON0[28] - kick start（W）
#define I2C_HW_CLR_DONE  BIT(29)  // IICCON0[29] - 清 DONE（W）
#define I2C_HW_ACKSTATUS BIT(30)  // IICCON0[30] - 0=ACK, 1=NAK（R）
#define I2C_HW_DONE      BIT(31)  // IICCON0[31] - 传输完成（R）

#define I2C_HW_START0_EN BIT(3)   // IICCON1[3]  - START
#define I2C_HW_CTL0_EN   BIT(4)   // IICCON1[4]  - 发送 CTL0（addr+R/W）
#define I2C_HW_ADR0_EN   BIT(5)   // IICCON1[5]  - 发送 ADR0（sub-addr）
#define I2C_HW_START1_EN BIT(7)   // IICCON1[7]  - repeated start Sr
#define I2C_HW_CTL1_EN   BIT(8)   // IICCON1[8]  - 发送 CTL1（second addr）
#define I2C_HW_RDAT_EN   BIT(9)   // IICCON1[9]  - 接收
#define I2C_HW_WDAT_EN   BIT(10)  // IICCON1[10] - 发送
#define I2C_HW_STOP_EN   BIT(11)  // IICCON1[11] - STOP
#define I2C_HW_TXNAK_EN  BIT(12)  // IICCON1[12] - 最后字节 NAK

// 初始化：CLKGAT2[0] + PE6/7 PAD + G5 mapping + IICCON0/1（手册 §8.3 step 1-3）
void i2c_hal_hw_init(void);
// 等 DONE，可选 timeout_us（0 = 等到底）
bool i2c_hal_hw_wait_done(u32 timeout_us);
// 探测地址（START + addr+R/W + STOP），返回 ACK
bool i2c_hal_hw_probe_addr(u8 dev_addr7, bool is_read, u32 timeout_us);
// 写 N 字节（1~4）到 sub-addr（手册 §8.3 step 4-9）
bool i2c_hal_hw_write(u8 dev_addr7, u8 reg_addr,
                      const u8 *data, u8 len, u32 timeout_us);
// 读 N 字节（1~4）从 sub-addr（repeated START + addr+R + RDAT）
bool i2c_hal_hw_read(u8 dev_addr7, u8 reg_addr,
                     u8 *buf, u8 len, u32 timeout_us);

// ===================== 软件 bit-bang 高层 transaction =====================
// 探测地址
bool i2c_hal_soft_probe_addr(u8 dev_addr7, bool is_read);
// 写 N 字节（1~4）到 sub-addr
bool i2c_hal_soft_write(u8 dev_addr7, u8 reg_addr,
                        const u8 *data, u8 len);
// 读 N 字节（1~4）从 sub-addr
bool i2c_hal_soft_read(u8 dev_addr7, u8 reg_addr,
                       u8 *buf, u8 len);

// ===================== AT24C02 业务封装（软/硬共用 API） =====================
// 硬件：调 i2c_hal_hw_*
bool at24c02_hw_probe(u8 addr7);                      // 单地址探测
u32  at24c02_hw_scan(u8 addr_lo, u8 addr_hi);         // 总线扫描
bool at24c02_hw_write_byte(u8 reg, u8 val);            // 写 1 字节（+5ms 写周期）
bool at24c02_hw_read_byte(u8 reg, u8 *out);            // 读 1 字节
bool at24c02_hw_write_bytes(u8 reg, const u8 *buf, u8 len);
bool at24c02_hw_read_bytes(u8 reg, u8 *buf, u8 len);

// 软件：调 i2c_hal_soft_*
bool at24c02_soft_probe(u8 addr7);
u32  at24c02_soft_scan(u8 addr_lo, u8 addr_hi);
bool at24c02_soft_write_byte(u8 reg, u8 val);
bool at24c02_soft_read_byte(u8 reg, u8 *out);
bool at24c02_soft_write_bytes(u8 reg, const u8 *buf, u8 len);
bool at24c02_soft_read_bytes(u8 reg, u8 *buf, u8 len);

#endif // _I2C_HAL_H_