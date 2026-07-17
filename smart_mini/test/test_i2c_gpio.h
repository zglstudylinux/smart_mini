#ifndef _TEST_I2C_GPIO_H_
#define _TEST_I2C_GPIO_H_

// AT24C02 GPIO bit-bang I2C test entry
// Pins: PE6 = SCL (manual GPIO), PE7 = SDA (manual GPIO, same as hardware IIC)
// External: AT24C02 module (SCL<->PE6, SDA<->PE7, VCC=3.3V, GND, A0=A1=A2=GND = addr 0x50)
void test_i2c_gpio_run(void);

#endif // _TEST_I2C_GPIO_H_