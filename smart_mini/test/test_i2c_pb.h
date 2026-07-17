#ifndef _TEST_I2C_PB_H_
#define _TEST_I2C_PB_H_

// PB1/PB2/PE6 硬件 IIC 测试
// 参考别人代码：PB1=SCL, PB2=SDA, PE6=WP, G3 映射, POSDIV=29
// 目的：验证在缺少 CLKGAT1[29] 配置时 IIC 是否能工作
void test_i2c_pb_run(void);

#endif // _TEST_I2C_PB_H_