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
#define ADKEY_SCAN_PERIOD_US        5000u
#define ADKEY_DEBOUNCE_SAMPLES      5u
#define ADKEY_LONG_MS               700u
#define ADKEY_LONG_TICKS            ((ADKEY_LONG_MS * 1000u) / ADKEY_SCAN_PERIOD_US)
#define ADKEY_HOLD_MS               200u
#define ADKEY_HOLD_TICKS            ((ADKEY_HOLD_MS * 1000u) / ADKEY_SCAN_PERIOD_US)

static volatile u32 g_adkey_scan_tick = 0;

// 初始化 SARADC 时钟、PB5/ADC12 双上拉、使能模拟模块
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

// 启动一次 ADC12 转换并轮询等待完成，超时 5ms 返回 false
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

// 阶段一：每 100ms 打印 ADC12 原始值，并按 10 个样本汇总 1s 窗口 min/max/span
void test_adkey_raw_run(void)
{
    u32 raw;
    u32 window_min = SARADC_DATA_MASK;
    u32 window_max = 0;
    u32 window_count = 0;
    u32 sample_count = 0;

    printf("========================================\n");
    printf("ADKEY stage 1: PB5 / ADC12 raw sampling\n");
    printf("SARADC clock: x24m/4=6MHz, baud=5, ADC clock=500kHz\n");
    printf("PB5 pull-up: GPIO 10K + ADC12 100K\n");
    printf("SARADC analog auto-enable: ADCAEN=1, ADCANGIO=1\n");
    printf("Press in order: NONE -> PLAY -> PREV -> NEXT\n");
    printf("========================================\n");

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

            printf("ADC12 raw=%u sample=%u\n", raw, sample_count);

            if (window_count >= ADKEY_WINDOW_SAMPLES) {
                printf("1s window: min=%u max=%u span=%u\n",
                         window_min, window_max, window_max - window_min);
                window_min = SARADC_DATA_MASK;
                window_max = 0;
                window_count = 0;
            }
        } else {
            printf("[TIMEOUT] ADC12 conversion did not finish in %u us\n",
                     (u32)SARADC_TIMEOUT_US);
        }

        delay_ms(ADKEY_PRINT_PERIOD_MS);
    }
}

// 单次 ADC 原始值映射为按键；121 死区返回 KEY_UNKNOWN
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

// 按键枚举 → 可读字符串（NONE / PLAY / PREV / NEXT / UNKNOWN）
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

// 阶段二：每 100ms 读 ADC 并打印 raw → key 映射结果（无消抖）
void test_adkey_map_run(void)
{
    u32 raw;
    u8 key;

    printf("========================================\n");
    printf("ADKEY stage 2: PB5 / ADC12 key mapping\n");
    printf("PB5 pull-up: GPIO 10K + ADC12 100K\n");
    printf("Map: <=69 PLAY, <=117 PREV, <=120 NEXT, 121 UNKNOWN, >=122 NONE\n");
    printf("No debounce yet: repeated lines and transition UNKNOWN are expected\n");
    printf("========================================\n");

    test_adkey_raw_init();

    while (1) {
        if (test_adkey_raw_read(&raw)) {
            key = test_adkey_map_raw(raw);
            printf("ADC12 raw=%u -> key=%s code=0x%02x\n",
                     raw, test_adkey_key_name(key), (u32)key);
        } else {
            printf("[TIMEOUT] ADC12 conversion did not finish in %u us\n",
                     (u32)SARADC_TIMEOUT_US);
        }

        delay_ms(ADKEY_PRINT_PERIOD_MS);
    }
}

// TMR1 5ms ISR：清挂起 + 递增 g_adkey_scan_tick（不在 ISR 内读 ADC/打印）
AT(.com_text.isr)
static void test_adkey_timer1_isr(void)
{
    TMR1CPND = BIT(9);
    g_adkey_scan_tick++;
}

// 配置 TMR1 为 5ms 周期中断，注册 ISR 并使能 IRQ
static void test_adkey_timer1_init(void)
{
    TMR1CON = 0;
    PICEN &= ~BIT(IRQ_TMR1_VECTOR);

    register_isr(IRQ_TMR1_VECTOR, test_adkey_timer1_isr);
    g_adkey_scan_tick = 0;

    TMR1CPND = BIT(9);
    TMR1CNT = 0;
    TMR1PR = ADKEY_SCAN_PERIOD_US - 1;
    TMR1CON = BIT(7);
    TMR1CON |= BIT(2) | BIT(0);

    PICPR &= ~BIT(IRQ_TMR1_VECTOR);
    PICEN |= BIT(IRQ_TMR1_VECTOR);
}

// 在稳定键切换时发边沿消息：先 old 的 SHORT_UP，再 new 的 SHORT
static void test_adkey_emit_transition(u8 old_key, u8 new_key, u32 raw)
{
    u16 message;

    if (old_key != KEY_NONE) {
        message = (u16)(KEY_SHORT_UP | old_key);
        printf("msg=0x%04x KEY_SHORT_UP %s raw=%u\n",
                 (u32)message, test_adkey_key_name(old_key), raw);
    }

    if (new_key != KEY_NONE) {
        message = (u16)(KEY_SHORT | new_key);
        printf("msg=0x%04x KEY_SHORT %s raw=%u\n",
                 (u32)message, test_adkey_key_name(new_key), raw);
    }
}

// 阶段三：TMR1 5ms 扫描，连续 5 次相同即认定为稳定消抖，输出 SHORT/SHORT_UP
void test_adkey_debounce_run(void)
{
    u32 handled_tick;
    u32 current_tick;
    u32 raw;
    u8 sampled_key;
    u8 candidate_key = KEY_NONE;
    u8 stable_key = KEY_NONE;
    u8 same_count = 0;

    printf("========================================\n");
    printf("ADKEY stage 3: 5ms x 5 debounce\n");
    printf("TMR1 scan period: 5ms; stable count: 5; debounce: 25ms\n");
    printf("Expected events: KEY_SHORT on stable press, KEY_SHORT_UP on stable release\n");
    printf("Long press and repeat are NOT implemented in this stage\n");
    printf("========================================\n");

    test_adkey_raw_init();
    test_adkey_timer1_init();
    handled_tick = g_adkey_scan_tick;

    while (1) {
        current_tick = g_adkey_scan_tick;
        if (current_tick == handled_tick) {
            continue;
        }
        handled_tick = current_tick;

        if (!test_adkey_raw_read(&raw)) {
            printf("[TIMEOUT] ADC12 conversion did not finish in %u us\n",
                     (u32)SARADC_TIMEOUT_US);
            continue;
        }

        sampled_key = test_adkey_map_raw(raw);
        if (sampled_key == KEY_UNKNOWN) {
            candidate_key = KEY_UNKNOWN;
            same_count = 0;
            continue;
        }

        if (sampled_key != candidate_key) {
            candidate_key = sampled_key;
            same_count = 1;
            continue;
        }

        if (same_count < ADKEY_DEBOUNCE_SAMPLES) {
            same_count++;
        }

        if ((same_count >= ADKEY_DEBOUNCE_SAMPLES) &&
            (candidate_key != stable_key)) {
            u8 old_key = stable_key;
            stable_key = candidate_key;
            test_adkey_emit_transition(old_key, stable_key, raw);
        }
    }
}

// 阶段四已并入阶段五；保留入口仅打印引导日志并死循环
void test_adkey_long_run(void)
{
    printf("ADKEY stage 4 has been merged into stage 5 (HOLD)\n");
    printf("Please enable TEST_ADKEY_HOLD_EN instead\n");
    while (1) {
        delay_ms(1000);
    }
}

// 阶段五：完整状态机——SHORT → LONG(700ms) → 每 200ms HOLD → 松开发 LONG_UP
void test_adkey_hold_run(void)
{
    u32 handled_tick;
    u32 current_tick;
    u32 press_tick = 0;
    u32 long_tick = 0;
    u32 held_ms;
    u32 raw;
    u8 sampled_key;
    u8 candidate_key = KEY_NONE;
    u8 stable_key = KEY_NONE;
    u8 same_count = 0;
    bool long_sent = false;

    printf("========================================\n");
    printf("ADKEY stage 5: 200ms HOLD repeat\n");
    printf("Scan: TMR1 5ms; debounce: 5 samples (25ms)\n");
    printf("Events: SHORT -> LONG at 700ms -> HOLD every 200ms -> LONG_UP on release\n");
    printf("========================================\n");

    test_adkey_raw_init();
    test_adkey_timer1_init();
    handled_tick = g_adkey_scan_tick;

    while (1) {
        current_tick = g_adkey_scan_tick;
        if (current_tick == handled_tick) {
            continue;
        }
        handled_tick = current_tick;

        if (!test_adkey_raw_read(&raw)) {
            printf("[TIMEOUT] ADC12 conversion did not finish in %u us\n",
                     (u32)SARADC_TIMEOUT_US);
            continue;
        }

        sampled_key = test_adkey_map_raw(raw);
        if (sampled_key == KEY_UNKNOWN) {
            candidate_key = KEY_UNKNOWN;
            same_count = 0;
            continue;
        }

        if (sampled_key != candidate_key) {
            candidate_key = sampled_key;
            same_count = 1;
            continue;
        }

        if (same_count < ADKEY_DEBOUNCE_SAMPLES) {
            same_count++;
        }

        if ((same_count >= ADKEY_DEBOUNCE_SAMPLES) &&
            (candidate_key != stable_key)) {
            u8 old_key = stable_key;
            held_ms = (current_tick - press_tick) *
                      (ADKEY_SCAN_PERIOD_US / 1000u);

            if (old_key != KEY_NONE) {
                u16 release_message;
                if (long_sent) {
                    release_message = (u16)(KEY_LONG_UP | old_key);
                    printf("msg=0x%04x KEY_LONG_UP %s held=%u ms raw=%u\n",
                             (u32)release_message,
                             test_adkey_key_name(old_key), held_ms, raw);
                } else {
                    release_message = (u16)(KEY_SHORT_UP | old_key);
                    printf("msg=0x%04x KEY_SHORT_UP %s held=%u ms raw=%u\n",
                             (u32)release_message,
                             test_adkey_key_name(old_key), held_ms, raw);
                }
            }

            stable_key = candidate_key;
            long_sent = false;
            long_tick = 0;

            if (stable_key != KEY_NONE) {
                u16 press_message = (u16)(KEY_SHORT | stable_key);
                press_tick = current_tick;
                printf("msg=0x%04x KEY_SHORT %s raw=%u\n",
                         (u32)press_message,
                         test_adkey_key_name(stable_key), raw);
            }
        }

        if ((stable_key != KEY_NONE) &&
            (sampled_key == stable_key) &&
            !long_sent &&
            ((u32)(current_tick - press_tick) >= ADKEY_LONG_TICKS)) {
            u16 long_message = (u16)(KEY_LONG | stable_key);
            held_ms = (current_tick - press_tick) *
                      (ADKEY_SCAN_PERIOD_US / 1000u);
            long_sent = true;
            long_tick = current_tick;
            printf("msg=0x%04x KEY_LONG %s held=%u ms raw=%u\n",
                     (u32)long_message,
                     test_adkey_key_name(stable_key), held_ms, raw);
        }

        if ((stable_key != KEY_NONE) &&
            (sampled_key == stable_key) &&
            long_sent &&
            ((u32)(current_tick - long_tick) >= ADKEY_HOLD_TICKS)) {
            u16 hold_message = (u16)(KEY_HOLD | stable_key);
            held_ms = (current_tick - press_tick) *
                      (ADKEY_SCAN_PERIOD_US / 1000u);
            long_tick = current_tick;
            printf("msg=0x%04x KEY_HOLD %s held=%u ms raw=%u\n",
                     (u32)hold_message,
                     test_adkey_key_name(stable_key), held_ms, raw);
        }
    }
}
