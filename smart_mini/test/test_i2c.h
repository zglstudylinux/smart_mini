#ifndef _TEST_I2C_H_
#define _TEST_I2C_H_

// I2C (硬件 IIC 控制器) 测试入口
// 使用 PE6 = SCL, PE7 = SDA，Group G5
// 外接 AT24C02 EEPROM（地址 0x50）
void test_i2c_run(void);

#endif // _TEST_I2C_H_