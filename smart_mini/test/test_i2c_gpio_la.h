#ifndef _TEST_I2C_GPIO_LA_H_
#define _TEST_I2C_GPIO_LA_H_

// GPIO bit-bang I2C logic-analyzer timing test
// Pins: PE5=SCL, PE6=SDA (same as test_i2c_gpio.c)
// Burst transactions, user measures SCL on logic analyzer
void test_i2c_gpio_la_run(void);

#endif // _TEST_I2C_GPIO_LA_H_