#ifndef _TEST_I2C_LA_H_
#define _TEST_I2C_LA_H_

// 逻辑分析仪专用 IIC 时钟测量测试
// 不依赖 AT24C02（可在断开从机时跑）
// 跑大量事务让用户用逻辑分析仪测实际 SCL 频率
void test_i2c_la_run(void);

#endif // _TEST_I2C_LA_H_