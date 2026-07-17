#ifndef _TEST_I2C_PE_MIN_H_
#define _TEST_I2C_PE_MIN_H_

// PE6/PE7 IIC 极简配置测试
// 目的：验证 PB1/PB2 的"无 CLKGAT1[29]=1 也能工作"结论是否也适用于 PE6/PE7
// 理论：x24m_clkdiv8 = 3 MHz 固定 Div8 应该是 IIC 默认时钟源
void test_i2c_pe_min_run(void);

#endif // _TEST_I2C_PE_MIN_H_