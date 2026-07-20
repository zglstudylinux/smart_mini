// ADKEY 阶段二测试 — PB5/ADC12 三键原始值映射
//
// 硬件依据：
//   - bt892x_pinfunction.md §4.2 / §8.10：PB5 = ADC12
//   - 原理图：PB5/PWRKEY；P/P 直连 GND，PREV 串 12K，NEXT 串 47K
//
// SARADC 依据：SARADC_CTL (逐次逼近型 ADC) 数据手册摘要.md
//   - CLKCON0[28]：1 = x24m_clkdiv4 = 6MHz
//   - CLKGAT0[13]：SARADC 时钟门
//   - SADCBAUD = 5：6MHz / (2 * (5 + 1)) = 500kHz
//   - SADCCON[19] ADCAEN，[18] ADCANGIO，[16] ADCEN，[12] CH12PUEN
//   - 写 SADCCH[12] 清 ADCPND 并启动 ADC12 转换
//   - SADCCH[16] ADCPND = 1 表示转换完成
//   - SADCDAT12[9:0] 为 10 位转换结果

#include "test_common.h"
#include "test_adkey.h"

#define ADKEY_PIN_MASK              BIT(5)

#define SARADC_CLK_SEL_X24M_DIV4    BIT(28)
#define SARADC_CLK_GATE             BIT(13)
#define SARADC_AUTO_ANALOG_EN       BIT(19)
#define SARADC_AUTO_ANALOG_IO_EN    BIT(18)
#define SARADC_ADC_EN               BIT(16)
#define SARADC_CH12_PULLUP_EN       BIT(12)
#define SARADC_CH12_EN              BIT(12)
#define SARADC_ADCPND               BIT(16)

#define SARADC_BAUD_500KHZ          5u
#define SARADC_DATA_MASK            0x03ffu
#define SARADC_TIMEOUT_US           5000u
#define ADKEY_PRINT_PERIOD_MS       100u
#define ADKEY_WINDOW_SAMPLES        10u

// 阶段一双上拉实测稳定值：PLAY=24、PREV=115、NEXT=120、NONE=122。
// 相邻稳定值取中点；121 保留为 NEXT/NONE 死区，不映射为任何按键。
#define ADKEY_PLAY_MAX              69u
#define ADKEY_PREV_MAX              117u
#define ADKEY_NEXT_MAX              120u
#define ADKEY_NONE_MIN              122u

static void test_adkey_raw_init(void)
{
    // SARADC 时钟：x24m_clkdiv4 = 6MHz，并打开 SARADC 时钟门。
    CLKCON0 |= SARADC_CLK_SEL_X24M_DIV4;
    CLKGAT0 |= SARADC_CLK_GATE;

    // PB5：普通数字输入；恢复阶段一已验证的 GPIO 10K + ADC12 100K 双上拉。
    GPIOBDIR    |=  ADKEY_PIN_MASK;
    GPIOBDE     |=  ADKEY_PIN_MASK;
    GPIOBFEN    &= ~ADKEY_PIN_MASK;
    GPIOBPU     |=  ADKEY_PIN_MASK;
    GPIOBPD     &= ~ADKEY_PIN_MASK;
    GPIOBPU200K &= ~ADKEY_PIN_MASK;
    GPIOBPD200K &= ~ADKEY_PIN_MASK;
    GPIOBPU300  &= ~ADKEY_PIN_MASK;
    GPIOBPD300  &= ~ADKEY_PIN_MASK;

    // SARADC_CLK = 6MHz / (2 * (5 + 1)) = 500kHz。
    SADCBAUD = SARADC_BAUD_500KHZ;

    // 数据手册将通道建立时间标为可选，阶段一保持默认 0 SARADC_CLK。
    SADCST = 0;

    // 自动使能模拟模块和模拟 IO，同时使能 ADC 及 ADC12 100K 上拉。
    SADCCON = SARADC_AUTO_ANALOG_EN
            | SARADC_AUTO_ANALOG_IO_EN
            | SARADC_ADC_EN
            | SARADC_CH12_PULLUP_EN;
}

static bool test_adkey_raw_read(u32 *raw)
{
    u32 start;

    // 每次写 CH12EN 都会清 ADCPND，并启动一次新的 ADC12 转换。
    SADCCH = SARADC_CH12_EN;
    start = tick_get();

    while (!(SADCCH & SARADC_ADCPND)) {
        if (tick_check_expire(start, SARADC_TIMEOUT_US)) {
            return false;
        }
    }

    *raw = SADCDAT12 & SARADC_DATA_MASK;
    return true;
}

void test_adkey_raw_run(void)
{
    u32 raw;
    u32 window_min = SARADC_DATA_MASK;
    u32 window_max = 0;
    u32 window_count = 0;
    u32 sample_count = 0;

    TEST_LOG("========================================");
    TEST_LOG("ADKEY stage 1: PB5 / ADC12 raw sampling");
    TEST_LOG("SARADC clock: x24m/4=6MHz, baud=5, ADC clock=500kHz");
    TEST_LOG("PB5 pull-up: GPIO 10K + ADC12 100K");
    TEST_LOG("SARADC analog auto-enable: ADCAEN=1, ADCANGIO=1");
    TEST_LOG("Press in order: NONE -> PLAY -> PREV -> NEXT");
    TEST_LOG("========================================");

    test_adkey_raw_init();

    while (1) {
        if (test_adkey_raw_read(&raw)) {
            sample_count++;
            window_count++;

            if (raw < window_min) {
                window_min = raw;
            }
            if (raw > window_max) {
                window_max = raw;
            }

            TEST_LOG("ADC12 raw=%u sample=%u", raw, sample_count);

            if (window_count >= ADKEY_WINDOW_SAMPLES) {
                TEST_LOG("1s window: min=%u max=%u span=%u",
                         window_min, window_max, window_max - window_min);
                window_min = SARADC_DATA_MASK;
                window_max = 0;
                window_count = 0;
            }
        } else {
            TEST_LOG("[TIMEOUT] ADC12 conversion did not finish in %u us",
                     (u32)SARADC_TIMEOUT_US);
        }

        delay_ms(ADKEY_PRINT_PERIOD_MS);
    }
}

static u8 test_adkey_map_raw(u32 raw)
{
    if (raw <= ADKEY_PLAY_MAX) {
        return KEY_PLAY;
    }
    if (raw <= ADKEY_PREV_MAX) {
        return KEY_PREV;
    }
    if (raw <= ADKEY_NEXT_MAX) {
        return KEY_NEXT;
    }
    if (raw >= ADKEY_NONE_MIN) {
        return KEY_NONE;
    }
    return KEY_UNKNOWN;
}

static const char *test_adkey_key_name(u8 key)
{
    switch (key) {
    case KEY_NONE:
        return "NONE";
    case KEY_PLAY:
        return "PLAY";
    case KEY_PREV:
        return "PREV";
    case KEY_NEXT:
        return "NEXT";
    default:
        return "UNKNOWN";
    }
}

void test_adkey_map_run(void)
{
    u32 raw;
    u8 key;

    TEST_LOG("========================================");
    TEST_LOG("ADKEY stage 2: PB5 / ADC12 key mapping");
    TEST_LOG("PB5 pull-up: GPIO 10K + ADC12 100K");
    TEST_LOG("Map: <=69 PLAY, <=117 PREV, <=120 NEXT, 121 UNKNOWN, >=122 NONE");
    TEST_LOG("No debounce yet: repeated lines and transition UNKNOWN are expected");
    TEST_LOG("========================================");

    test_adkey_raw_init();

    while (1) {
        if (test_adkey_raw_read(&raw)) {
            key = test_adkey_map_raw(raw);
            TEST_LOG("ADC12 raw=%u -> key=%s code=0x%02x",
                     raw, test_adkey_key_name(key), (u32)key);
        } else {
            TEST_LOG("[TIMEOUT] ADC12 conversion did not finish in %u us",
                     (u32)SARADC_TIMEOUT_US);
        }

        delay_ms(ADKEY_PRINT_PERIOD_MS);
    }
}
