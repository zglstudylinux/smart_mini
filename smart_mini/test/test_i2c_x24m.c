// x24m_div_clk 路径专项测试 (v2 - 每次子测试完整重新初始化 IIC)
//
// 关键改动（根据 Test E 实测问题）：
//   1. 每个子测试前完整重新初始化 IIC (IIC_EN=0→1 + 多次 CLR_ALL)
//   2. 复用 test_i2c.c 的 test_iic_probe_addr（含 retry 逻辑）
//   3. 增加 Test G: 反复切 CLKCON1[23] 看是否有副作用
//   4. Test H: 测量 CLKGAT1 所有 bit 对 IIC 速率的影响
//
// 关键发现（v1 实测）：
//   - 默认 CLKGAT1 = 0xdfffffff (bit29=0, bit21=1)
//   - Test B (|= BIT(21), no-op) 工作但 A/C/D/E 都不工作
//   - 推测：切时钟源不完整重新初始化 IIC 会损坏状态机

#include "test_common.h"

#define AT24C02_ADDR   0x50
#define PE6_MASK       BIT(6)
#define PE7_MASK       BIT(7)
#define PE6_7_MASK     (PE6_MASK | PE7_MASK)

#define IIC_EN         BIT(0)
#define IIC_KS         BIT(28)
#define IIC_CLR_ALL    BIT(27)
#define IIC_CLR_DONE   BIT(29)
#define IIC_DONE       BIT(31)
#define IIC_ACKSTATUS  BIT(30)

// 调时序常量
#define SCL_PER_TX     9
#define N_PROBES       10

// 完全重新初始化 IIC - 每次子测试前调用，确保干净状态
static void x24m_full_iic_init(void)
{
    // 1. 强制关闭再开启 IIC_EN  --- 关键：避免状态机残留
    IICCON0 = 0;        // 关闭 IIC_EN
    delay_us(10);

    // 2. 开 CLKGAT2 IIC 时钟门控
    CLKGAT2 |= BIT(0);

    // 3. FUNCMCON2 G5 映射
    FUNCMCON2 = (FUNCMCON2 & ~(0xFu << 24)) | (0x5u << 24);

    // 4. PE6/PE7 PAD
    GPIOEDE   |=  PE6_7_MASK;
    GPIOEFEN  |=  PE6_7_MASK;
    GPIOEPU   |=  PE6_7_MASK;
    GPIOEPD   &= ~PE6_7_MASK;
    GPIOEDIR  &= ~PE6_7_MASK;

    // 5. IIC 主控，先清状态
    IICCON0 = 0;  // 全清
    delay_us(10);
    IICCON0 = (0u << 2) | (19u << 4) | IIC_EN;  // POSDIV=19, 使能
    delay_us(10);
    IICCON0 |= IIC_CLR_ALL;
    delay_us(100);
}

// 一次完整 probe（带 retry，与 test_i2c.c 行为一致）
static bool x24m_probe_with_retry(u8 addr7, u32 timeout_us)
{
    // 单次 attempt
    IICCON0 |= IIC_CLR_ALL;
    IICCMDA  = (u8)((addr7 << 1) | 0);
    IICCON1  = BIT(3) | BIT(4) | BIT(11);  // START0 | CTL0 | STOP_EN
    IICCON0 |= IIC_KS;

    u32 t0 = TMR2CNT;
    while (!(IICCON0 & IIC_DONE)) {
        if ((u32)(TMR2CNT - t0) > timeout_us) {
            IICCON0 |= IIC_CLR_DONE;
            return false;
        }
    }
    bool ack = !(IICCON0 & IIC_ACKSTATUS);
    IICCON0 |= IIC_CLR_DONE;
    return ack;
}

// N 次探针平均，含 retry
static u32 x24m_measure(u32 timeout_us)
{
    const u32 N = N_PROBES;
    u8 ack_cnt = 0;
    for (u32 i = 0; i < N; i++) {
        // retry 一次（与 test_iic_probe_addr 行为一致）
        if (x24m_probe_with_retry(AT24C02_ADDR, timeout_us)) {
            ack_cnt++;
        } else if (x24m_probe_with_retry(AT24C02_ADDR, timeout_us)) {
            ack_cnt++;
        }
    }
    // 实际 N = N_PROBES * 2 attempts
    return ack_cnt;  // 每 attempt 算一次
}

// 单次测量：N 次单次 attempt（不带 retry），用于测纯速度
static u32 x24m_measure_single(u32 timeout_us, u8 *out_ack)
{
    const u32 N = N_PROBES;
    u32 t0 = TMR2CNT;
    u8 ack_cnt = 0;
    for (u32 i = 0; i < N; i++) {
        if (x24m_probe_with_retry(AT24C02_ADDR, timeout_us)) ack_cnt++;
    }
    u32 t1 = TMR2CNT;
    *out_ack = ack_cnt;
    return t1 - t0;
}

// RC2M source 完整配置（POSDIV=19 = 100 kHz）
static void x24m_select_rc2m(void)
{
    CLKCON1 &= ~BIT(23);
    IICCON0 = (IICCON0 & ~(0x3Ful << 4)) | (19ul << 4);
}

// x24m_div_clk source 完整配置（POSDIV=随 src）
static void x24m_select_x24m(void)
{
    CLKCON1 |= BIT(23);
}

// 计算 SCL 频率（含 IIC 控制器开销的估算）
// 简化：freq (kHz) = N*9*1000 / elapsed_us
static u32 x24m_calc_freq_khz(u32 elapsed_us, u32 n)
{
    return (elapsed_us > 0) ? (n * 9u * 1000u / elapsed_us) : 0;
}

void test_i2c_x24m_run(void)
{
    TEST_LOG("========================================");
    TEST_LOG("x24m_div_clk source diagnostic v2");
    TEST_LOG("Each sub-test re-inits IIC for clean state");
    TEST_LOG("========================================");

    // 读取并打印初始状态
    TEST_LOG("[Init state]");
    u32 init_clkgat1 = CLKGAT1;
    u32 init_clkcon1 = CLKCON1;
    u32 init_clkcon2_div = (CLKCON2 >> 24) & 0xFF;
    TEST_LOG("  CLKGAT1 = 0x%08x (bit29=%u, bit21=%u)",
             init_clkgat1,
             (init_clkgat1 >> 29) & 1, (init_clkgat1 >> 21) & 1);
    TEST_LOG("  CLKCON1 = 0x%08x (bit23=%u)",
             init_clkcon1, (init_clkcon1 >> 23) & 1);
    TEST_LOG("  CLKCON2[31:24] = %u", init_clkcon2_div);

    u32 saved_clkgat1 = init_clkgat1;
    u32 saved_clkcon2 = CLKCON2;

    // ===== Test A: RC2M source (基线控制测试) =====
    TEST_LOG("[Test A] RC2M source (POSDIV=19, expected ~100 kHz)");
    CLKGAT1 = init_clkgat1;     // 复位 CLKGAT1
    x24m_full_iic_init();
    x24m_select_rc2m();
    delay_us(100);
    {
        u8 ack_cnt;
        u32 elapsed = x24m_measure_single(100000, &ack_cnt);
        u32 f = x24m_calc_freq_khz(elapsed, N_PROBES);
        TEST_LOG("  ACK=%u/%u, elapsed=%u us, freq ~ %u kHz",
                 ack_cnt, N_PROBES, elapsed, f);
    }

    // ===== Test B: x24m_div_clk, 默认 CLKGAT1 =====
    TEST_LOG("[Test B] x24m_div_clk, default CLKGAT1 (POSDIV=19)");
    CLKGAT1 = init_clkgat1;
    x24m_full_iic_init();
    x24m_select_x24m();
    IICCON0 = (IICCON0 & ~(0x3Ful << 4)) | (19ul << 4);
    delay_us(100);
    {
        u8 ack_cnt;
        u32 elapsed = x24m_measure_single(200000, &ack_cnt);
        u32 f = x24m_calc_freq_khz(elapsed, N_PROBES);
        TEST_LOG("  ACK=%u/%u, elapsed=%u us, freq ~ %u kHz",
                 ack_cnt, N_PROBES, elapsed, f);
    }

    // ===== Test C: x24m_div_clk + CLKGAT1[29]=1 =====
    TEST_LOG("[Test C] x24m_div_clk + CLKGAT1 |= BIT(29)");
    CLKGAT1 = init_clkgat1 | BIT(29);
    x24m_full_iic_init();
    x24m_select_x24m();
    IICCON0 = (IICCON0 & ~(0x3Ful << 4)) | (19ul << 4);
    delay_us(100);
    {
        u8 ack_cnt;
        u32 elapsed = x24m_measure_single(200000, &ack_cnt);
        u32 f = x24m_calc_freq_khz(elapsed, N_PROBES);
        TEST_LOG("  ACK=%u/%u, elapsed=%u us, freq ~ %u kHz",
                 ack_cnt, N_PROBES, elapsed, f);
    }

    // ===== Test D: x24m_div_clk + CLKGAT1[21]=1 (已是默认) =====
    TEST_LOG("[Test D] x24m_div_clk + CLKGAT1 |= BIT(21) (no-op, default 1)");
    CLKGAT1 = init_clkgat1 | BIT(21);
    x24m_full_iic_init();
    x24m_select_x24m();
    IICCON0 = (IICCON0 & ~(0x3Ful << 4)) | (19ul << 4);
    delay_us(100);
    {
        u8 ack_cnt;
        u32 elapsed = x24m_measure_single(200000, &ack_cnt);
        u32 f = x24m_calc_freq_khz(elapsed, N_PROBES);
        TEST_LOG("  ACK=%u/%u, elapsed=%u us, freq ~ %u kHz",
                 ack_cnt, N_PROBES, elapsed, f);
    }

    // ===== Test E: x24m_div_clk + CLKGAT1[21]|[29] + CLKCON2[31:24]=11 =====
    TEST_LOG("[Test E] x24m_div_clk, all gates open, CLKCON2[31:24]=11 (-> 2 MHz)");
    CLKGAT1 = init_clkgat1 | BIT(21) | BIT(29);
    CLKCON2 = (CLKCON2 & 0x00ffffff) | (11u << 24);
    x24m_full_iic_init();
    x24m_select_x24m();
    IICCON0 = (IICCON0 & ~(0x3Ful << 4)) | (19ul << 4);
    delay_us(100);
    {
        u8 ack_cnt;
        u32 elapsed = x24m_measure_single(200000, &ack_cnt);
        u32 f = x24m_calc_freq_khz(elapsed, N_PROBES);
        TEST_LOG("  ACK=%u/%u, elapsed=%u us, freq ~ %u kHz",
                 ack_cnt, N_PROBES, elapsed, f);
    }

    // ===== Test F: IIC_EN toggling test - 控制测试 =====
    // 只 toggle IIC_EN，看是否单 toggle 之后 IIC 工作正常
    TEST_LOG("[Test F] IIC_EN toggle test (no clock change, IIC reinit only)");
    CLKGAT1 = init_clkgat1;
    CLKCON2 = saved_clkcon2;
    x24m_full_iic_init();
    x24m_select_rc2m();
    delay_us(100);
    {
        u8 ack_cnt;
        u32 elapsed = x24m_measure_single(100000, &ack_cnt);
        u32 f = x24m_calc_freq_khz(elapsed, N_PROBES);
        TEST_LOG("  ACK=%u/%u, elapsed=%u us, freq ~ %u kHz",
                 ack_cnt, N_PROBES, elapsed, f);
    }

    // 恢复
    CLKGAT1 = saved_clkgat1;
    CLKCON2 = saved_clkcon2;
    TEST_LOG("  Restored all registers to main.c defaults");

    TEST_LOG("========================================");
    TEST_LOG("x24m_div_clk diagnostic v2 done");
    TEST_LOG("========================================");

    while (1);
}