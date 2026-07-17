// PE6/PE7 IIC 极简配置测试（基于用户最新框图洞察）
//
// 关键假设：IIC 时钟由 clkcon1[23] MUX 在两条路径间选择：
//   path A: rc2m_clk (RING OSC2M, 约 32 kHz，低速)
//   path B: x24m_clkdiv8 (24M / 8 = 3 MHz，固定 Div8，常开)
//
// 默认 clkcon1[23] 应该选 path B (x24m_clkdiv8 = 3 MHz)
//
// 那么 PB1/PB2 和 PE6/PE7 应该用相同的最简配置：
//   CLKGAT2 |= BIT(0)        // IIC 时钟门（默认就开）
//   FUNCMCON2 |= (group<<24) // 引脚映射（G3 或 G5）
//   POSDIV = 19 ÷ 20 = IICK / 20
//   IICCON0 |= IIC_EN
//
// 不需要 CLKGAT1[29]=1，不需要修改 CLKCOCN2[31:24]！
//
// 本测试用 PE6/PE7 验证：5 个对比场景
//   A: 极简（仅 CLKGAT2 + FUNCMCON2 G5 + POSDIV=19, clkcon1[23]=0 = path A rc2m）
//   B: 极简（clkcon1[23]=1 = path B x24m_clkdiv8）
//   C: + CLKGAT1 |= BIT(29) + clkcon1[23]=1
//   D: + CLKCOCN2[31:24]=11 + clkcon1[23]=1 (强制 x24m_div_clk=2 MHz)
//   E: 控制 - 切回 RC2M (clkcon1[23]=0)

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

#define N_PROBES       10

// 完全重置 IIC
static void min_iic_reset(void)
{
    IICCON0 = 0;
    delay_us(10);
}

// 配置 PE6/PE7 PAD
static void min_pe_pad(void)
{
    GPIOEDE  |=  PE6_7_MASK;
    GPIOEFEN |=  PE6_7_MASK;
    GPIOEPU  |=  PE6_7_MASK;
    GPIOEPD  &= ~PE6_7_MASK;
    GPIOEDIR &= ~PE6_7_MASK;
}

// IIC 单次 probe（带 1 次 retry）
static bool min_probe(u8 addr7, u32 timeout_us)
{
    IICCON0 |= IIC_CLR_ALL;
    IICCMDA  = (u8)((addr7 << 1) | 0);
    IICCON1  = BIT(3) | BIT(4) | BIT(11);
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

// 测量 N 次 probe 的耗时
static u32 min_measure_one(const char *label, u32 timeout_us)
{
    u32 t0 = TMR2CNT;
    u8 ack = 0;
    for (u32 i = 0; i < N_PROBES; i++) {
        if (min_probe(AT24C02_ADDR, timeout_us)) ack++;
        else if (min_probe(AT24C02_ADDR, timeout_us)) ack++;
    }
    u32 t1 = TMR2CNT;
    u32 elapsed = t1 - t0;
    u32 f = (elapsed > 0) ? (N_PROBES * 9u * 1000u / elapsed) : 0;
    TEST_LOG("  %s: ACK=%u/%u, total=%u us, freq ~ %u kHz",
             label, ack, N_PROBES * 2, elapsed, f);
    return elapsed;
}

void test_i2c_pe_min_run(void)
{
    TEST_LOG("========================================");
    TEST_LOG("PE6/PE7 IIC minimal config test");
    TEST_LOG("Hypothesis: IICK defaults to x24m_clkdiv8 = 3 MHz");
    TEST_LOG("========================================");

    // 保存初始状态
    u32 saved_clkgat1 = CLKGAT1;
    u32 saved_clkcon1 = CLKCON1;
    u32 saved_clkcon2 = CLKCON2;
    u32 saved_iiccon0 = IICCON0;
    TEST_LOG("[Init] CLKGAT1=0x%08x CLKCON1=0x%08x CLKCON2=0x%08x",
             saved_clkgat1, saved_clkcon1, saved_clkcon2);

    // 通用初始化：只开 IIC 时钟门
    CLKGAT2 |= BIT(0);
    min_pe_pad();

    // ===== Test A: 极简 - clkcon1[23]=0 (path A: rc2m_clk 假设) =====
    TEST_LOG("[Test A] Minimal: CLKCON1[23]=0, POSDIV=19");
    TEST_LOG("  If path A is rc2m (~32 kHz), expect SCL ~ 1 kHz");
    min_iic_reset();
    FUNCMCON2 = (FUNCMCON2 & ~(0xFu << 24)) | (0x5u << 24);  // G5
    CLKCON1 = (CLKCON1 & ~BIT(23));  // select path A
    IICCON0 = (0u << 2) | (19u << 4) | IIC_EN;
    delay_us(10);
    IICCON0 |= IIC_CLR_ALL;
    delay_us(100);
    min_measure_one("Test A", 100000);

    // ===== Test B: 极简 - clkcon1[23]=1 (path B: x24m_clkdiv8 = 3 MHz) =====
    TEST_LOG("[Test B] Minimal: CLKCON1[23]=1, POSDIV=19");
    TEST_LOG("  If path B is x24m_clkdiv8 = 3 MHz, expect SCL = 150 kHz");
    min_iic_reset();
    FUNCMCON2 = (FUNCMCON2 & ~(0xFu << 24)) | (0x5u << 24);
    CLKCON1 = (CLKCON1 & ~BIT(23)) | BIT(23);  // select path B
    IICCON0 = (0u << 2) | (19u << 4) | IIC_EN;
    delay_us(10);
    IICCON0 |= IIC_CLR_ALL;
    delay_us(100);
    min_measure_one("Test B", 100000);

    // ===== Test C: + CLKGAT1|=BIT(29), clkcon1[23]=1 =====
    TEST_LOG("[Test C] + CLKGAT1 |= BIT(29), clkcon1[23]=1");
    CLKGAT1 |= BIT(29);
    IICCON0 = (0u << 2) | (19u << 4) | IIC_EN;
    delay_us(100);
    IICCON0 |= IIC_CLR_ALL;
    delay_us(100);
    min_measure_one("Test C", 100000);

    // ===== Test D: + CLKCOCN2[31:24]=11 (强制 2 MHz) =====
    TEST_LOG("[Test D] + CLKGAT1[29]=1 + CLKCON2[31:24]=11 (force 2 MHz)");
    TEST_LOG("  If x24m_div_clk = 2 MHz, SCL = 100 kHz");
    CLKCON2 = (CLKCON2 & 0x00ffffff) | (11u << 24);
    IICCON0 = (0u << 2) | (19u << 4) | IIC_EN;
    delay_us(100);
    IICCON0 |= IIC_CLR_ALL;
    delay_ms(10);  // 等时钟切换稳定
    min_measure_one("Test D", 100000);

    // ===== Test E: 控制 - 反复切换 clkcon1[23] 看是否影响状态 =====
    // 不切硬件，只是小心重置 IIC，验证哪些 setting 实际生效
    TEST_LOG("[Test E] Switch back to clkcon1[23]=0 with re-init");
    CLKCON1 = (CLKCON1 & ~BIT(23));  // back to path A
    min_iic_reset();
    IICCON0 = (0u << 2) | (19u << 4) | IIC_EN;
    delay_ms(10);
    IICCON0 |= IIC_CLR_ALL;
    min_measure_one("Test E", 100000);

    // 恢复
    CLKGAT1 = saved_clkgat1;
    CLKCON1 = saved_clkcon1;
    CLKCON2 = saved_clkcon2;
    IICCON0 = saved_iiccon0;
    TEST_LOG("[End] Restored registers to main.c defaults");

    TEST_LOG("========================================");
    TEST_LOG("Done. Report which test gives ~100 kHz SCL.");
    TEST_LOG("========================================");

    while (1);
}