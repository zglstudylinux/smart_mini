// GPIO 诊断测试 — PE 和 PB 端口引脚翻转
// 目的：诊断 PE/PB 端口哪些引脚在开发板上物理可达
// 手册：BT892X_UserManual_Driver.md §3.2 GPIO 寄存器说明
// SFR：见 header/sfr.h 第 421-462 行（Group6 GPIO A/B/E/F）
//
// 测试列表（跳过不可用引脚）：
//   - PE0（警告：MUTE PIN，高压相关）、PE4、PE5、PE6、PE7
//   - PB0/PB1/PB2（注意：wakeup source，但本测试不进入 sleep，无影响）
//   - 跳过：PB3（UART0 debug TX）、PB4（USB DM）、PB5（WKO 复位唤醒）
//
// 验证方法：逻辑分析仪夹探针到对应引脚，串口打印当前测试的引脚

#include "test_common.h"

#define TOGGLE_PER_PIN_MS   2000    // 每个引脚翻转持续时间

// 测试单个引脚 - 通用版本（port 通过宏传入）
// 注意：寄存器名是 GPIOxDE（不是 GPIOxADE），x 是端口字母
//   GPIOADE / GPIOEDE / GPIOBDE 等
#define TEST_PIN_TOGGLE(PORT, pin_num, duration_ms) do { \
        GPIO##PORT##DE   |=  BIT(pin_num); \
        GPIO##PORT##FEN &= ~BIT(pin_num); \
        GPIO##PORT##DIR &= ~BIT(pin_num); \
        GPIO##PORT##CLR  =   BIT(pin_num); \
        TEST_LOG("========================================"); \
        TEST_LOG(">>> Testing P" #PORT "%d: toggle 1Hz for %u ms", (u32)pin_num, duration_ms); \
        TEST_LOG(">>> Probe P" #PORT "%d with logic analyzer now!", (u32)pin_num); \
        u32 _t0 = TMR2CNT; \
        u32 _cnt = 0; \
        while ((u32)(TMR2CNT - _t0) < (duration_ms) * 1000) { \
            GPIO##PORT##SET = BIT(pin_num); \
            delay_ms(500); \
            GPIO##PORT##CLR = BIT(pin_num); \
            delay_ms(500); \
            _cnt++; \
        } \
        TEST_LOG("<<< P" #PORT "%d done (%u toggles, pin left LOW)", (u32)pin_num, _cnt); \
        GPIO##PORT##DIR |= BIT(pin_num); \
        GPIO##PORT##FEN |= BIT(pin_num); \
        GPIO##PORT##DE  &= ~BIT(pin_num); \
    } while (0)

void test_gpio_run(void)
{
    TEST_LOG("========================================");
    TEST_LOG("PE/PB GPIO diagnostic test");
    TEST_LOG("Connect logic analyzer probes to PE0/4/5/6/7 and PB0/1/2");
    TEST_LOG("Each pin will toggle 1Hz for 2 seconds");
    TEST_LOG("========================================");

    // ===== PE 端口 =====
    // PE1/PE2/PE3 不存在（手册 §4.3 只列出 PE0, PE4, PE5, PE6, PE7）
    TEST_LOG("");
    TEST_LOG("*** PE port ***");

    // PE0 - 警告：MUTE PIN (TYPE4 高压相关)
    TEST_PIN_TOGGLE(E, 0, TOGGLE_PER_PIN_MS);

    // PE4
    TEST_PIN_TOGGLE(E, 4, TOGGLE_PER_PIN_MS);

    // PE5
    TEST_PIN_TOGGLE(E, 5, TOGGLE_PER_PIN_MS);

    // PE6
    TEST_PIN_TOGGLE(E, 6, TOGGLE_PER_PIN_MS);

    // PE7
    TEST_PIN_TOGGLE(E, 7, TOGGLE_PER_PIN_MS);

    // ===== PB 端口 =====
    // 跳过 PB3 (debug TX) / PB4 (USBDM) / PB5 (WKO reset)
    TEST_LOG("");
    TEST_LOG("*** PB port (skipping PB3/PB4/PB5) ***");

    // PB0 (WK1 wakeup source)
    TEST_PIN_TOGGLE(B, 0, TOGGLE_PER_PIN_MS);

    // PB1 (WK2 wakeup source)
    TEST_PIN_TOGGLE(B, 1, TOGGLE_PER_PIN_MS);

    // PB2 (WK3 wakeup source)
    TEST_PIN_TOGGLE(B, 2, TOGGLE_PER_PIN_MS);

    TEST_LOG("========================================");
    TEST_LOG("All tested pins done.");
    TEST_LOG("========================================");

    while (1);
}