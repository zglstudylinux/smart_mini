// Timer 测试 — TMR1 精度 + 中断验证
// 手册：BT892X_UserManual_Driver.md §4 Timer 章节
// SFR：见 header/sfr.h 第 93-96 行（TMR1CON/CPND/CNT/PR）
// 时钟源：main.c 已配置 tmr_inc = x26m_div_clk = 1 MHz
//
// TMR0 已被 main.c 的 timer0_init 占用为 1ms 中断，TMR2 已被占为 1us tick
// 因此本测试使用 TMR1（空闲）
//
// 测试内容：
//   1. 轮询模式：用 TMR2CNT 测量 TMR1 在 1ms/10ms/100ms 下的实际耗时
//   2. 中断模式：注册 ISR，统计 1000 次中断的实际耗时（约 1s）

#include "test_common.h"

#define TMR1_POLL_TIMEOUT_US  5000000   // 5 秒超时（防卡死）

static volatile u32 g_t1_isr_count = 0;

// 中断服务程序 — 放到 .com_text.isr 段（与 interrupt.c 一致）
AT(.com_text.isr)
static void test_timer1_isr(void)
{
    TMR1CPND = BIT(9);     // 清 TMR1 溢出挂起
    g_t1_isr_count++;
}

// 轮询模式测量 TMR1 一次溢出的实际耗时
// expected_us：期望的周期（µs）
// 返回：实际耗时（µs，由 TMR2CNT 测量）
static u32 test_timer1_measure_poll(u32 expected_us)
{
    u32 t0, t1;

    // 【关键】先清溢出挂起位（否则上次的 BIT(9)=1 会让 while 立即跳过）
    TMR1CPND = BIT(9);

    // 1. 停止 TMR1，清零计数
    TMR1CON = 0;
    TMR1CNT = 0;
    TMR1PR  = expected_us - 1;     // 周期 = PR + 1 个 tmr_inc（1µs）

    // 2. 记录 TMR2 当前 tick（开始）
    t0 = TMR2CNT;

    // 3. 启动 TMR1（BIT(2)=INCSEL → tmr_inc，BIT(0)=TMREN）
    TMR1CON = BIT(2) | BIT(0);

    // 4. 等待溢出（TPND = bit9 置 1），带超时保护
    {
        u32 timeout = t0 + TMR1_POLL_TIMEOUT_US;
        while ((TMR1CON & BIT(9)) == 0) {
            if ((u32)(TMR2CNT - timeout) < 0x80000000ul) {
                TEST_LOG("  [TIMEOUT] TMR1 overflow not detected");
                TMR1CON = 0;
                return 0;
            }
        }
    }

    // 5. 记录 TMR2 当前 tick（结束）
    t1 = TMR2CNT;

    // 6. 停止 TMR1
    TMR1CON = 0;

    // 7. 返回实际耗时（µs）
    return t1 - t0;
}

void test_timer_run(void)
{
    u32 measured;
    int err;

    TEST_LOG("========================================");
    TEST_LOG("Timer1 test start (tmr_inc = 1MHz)");
    TEST_LOG("========================================");

    // ===== Part 1: 轮询模式精度测试 =====
    TEST_LOG("[Part 1] Polling mode - period accuracy");

    measured = test_timer1_measure_poll(1000);     // 1ms
    err = (int)measured - 1000;
    TEST_LOG("  1ms: expected 1000us, measured %u us, err=%d us",
             (u32)measured, err);

    measured = test_timer1_measure_poll(10000);    // 10ms
    err = (int)measured - 10000;
    TEST_LOG("  10ms: expected 10000us, measured %u us, err=%d us",
             (u32)measured, err);

    measured = test_timer1_measure_poll(100000);   // 100ms
    err = (int)measured - 100000;
    TEST_LOG("  100ms: expected 100000us, measured %u us, err=%d us",
             (u32)measured, err);

    // ===== Part 2: 中断模式触发测试 =====
    TEST_LOG("[Part 2] Interrupt mode - 1000 x 1ms ISR");

    register_isr(IRQ_TMR1_VECTOR, test_timer1_isr);
    g_t1_isr_count = 0;

    // 【关键】清除 Part 1 轮询遗留的溢出挂起 TPND(bit9)
    // 否则 TIE+PICEN 使能瞬间会立刻触发一次伪中断，使 1000 次计数提前约 1ms 完成
    // 手册 §4.2：TPND 只能通过 TMR1CPND[9] TPCLR 写 1 清除（写 TMR1CON 不清）
    TMR1CPND = BIT(9);

    // 配置 TMR1 为 1ms 周期中断
    TMR1CON = BIT(7);                  // TIE = 1（先开中断）
    TMR1CNT = 0;
    TMR1PR  = 1000 - 1;                // 1ms 周期
    TMR1CON |= BIT(2) | BIT(0);        // tmr_inc + 启动
    PICPR  &= ~BIT(IRQ_TMR1_VECTOR);   // 优先级（清 0 = 高优先级，按手册）
    PICEN  |=  BIT(IRQ_TMR1_VECTOR);   // 使能 TMR1 向量

    // 等待 1000 次中断
    {
        u32 t0 = TMR2CNT;
        u32 timeout = t0 + 2000000;    // 2 秒超时
        while (g_t1_isr_count < 1000) {
            if ((u32)(TMR2CNT - timeout) < 0x80000000ul) {
                TEST_LOG("  [TIMEOUT] ISR not firing (count=%u)", (u32)g_t1_isr_count);
                break;
            }
        }
        u32 t1 = TMR2CNT;

        // 关闭 TMR1 和中断向量
        TMR1CON = 0;
        PICEN  &= ~BIT(IRQ_TMR1_VECTOR);

        measured = t1 - t0;
        err = (int)measured - 1000000;
        TEST_LOG("  1000 ISR: expected 1000000us, measured %u us, err=%d us, count=%u",
                 (u32)measured, err, (u32)g_t1_isr_count);
    }

    TEST_LOG("========================================");
    TEST_LOG("Timer1 test done");
    TEST_LOG("========================================");

    while (1);
}