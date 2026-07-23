#ifndef _TEST_COMMON_H_
#define _TEST_COMMON_H_

#include "include.h"   // 拿到全局类型、SFR、printf、delay_*

// 统一的测试日志前缀，方便 grep
#define TEST_LOG(fmt, ...)  printf("[TEST] " fmt "\n", ##__VA_ARGS__)

// ===== GPIO 操作宏 =====
// 关键技术点：C 标准规定，宏参数在使用 ## 拼接时**不会**先被展开。
// 因此需要两层宏：外层先展开 PORT 参数，内层再做 ## 拼接。
// 用法：TEST_GPIO_OUT(E, 4)  把 PE4 配置为普通 GPIO 输出

#define TEST_GPIO_OUT_HELPER(PORT, PIN)  do { \
        GPIO##PORT##DIR &= ~BIT(PIN); \
        GPIO##PORT##DE  |=  BIT(PIN); \
        GPIO##PORT##FEN &= ~BIT(PIN); \
    } while (0)
#define TEST_GPIO_OUT(PORT, PIN)  TEST_GPIO_OUT_HELPER(PORT, PIN)

#define TEST_GPIO_IN_HELPER(PORT, PIN)   do { \
        GPIO##PORT##DIR |=  BIT(PIN); \
        GPIO##PORT##DE  |=  BIT(PIN); \
        GPIO##PORT##FEN &= ~BIT(PIN); \
    } while (0)
#define TEST_GPIO_IN(PORT, PIN)   TEST_GPIO_IN_HELPER(PORT, PIN)

#define TEST_GPIO_HIGH_HELPER(PORT, PIN)  (GPIO##PORT##SET = BIT(PIN))
#define TEST_GPIO_HIGH(PORT, PIN)  TEST_GPIO_HIGH_HELPER(PORT, PIN)

#define TEST_GPIO_LOW_HELPER(PORT, PIN)   (GPIO##PORT##CLR = BIT(PIN))
#define TEST_GPIO_LOW(PORT, PIN)   TEST_GPIO_LOW_HELPER(PORT, PIN)

#define TEST_GPIO_READ_HELPER(PORT, PIN)  ((GPIO##PORT >> (PIN)) & 1u)
#define TEST_GPIO_READ(PORT, PIN)  TEST_GPIO_READ_HELPER(PORT, PIN)

#endif // _TEST_COMMON_H_