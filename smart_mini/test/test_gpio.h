#ifndef _TEST_GPIO_H_
#define _TEST_GPIO_H_

// GPIO 测试入口
// PE4 输出 1Hz 方波（500ms 高 / 500ms 低），用万用表量电压可看到 ~1s 周期切换
void test_gpio_run(void);

#endif // _TEST_GPIO_H_