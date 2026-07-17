#ifndef _TEST_I2C_X24M_H_
#define _TEST_I2C_X24M_H_

// 调试 x24m_div_clk 路径的 I2C 测试入口
// 验证 CLKCON1[23]=1 时 IIC 实际使用的是哪个时钟源
void test_i2c_x24m_run(void);

#endif // _TEST_I2C_X24M_H_