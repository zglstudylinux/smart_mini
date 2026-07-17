// PB1/PB2/PE6 硬件 IIC 测试
// 参考用户提供的某代码，验证在最少配置下 PB1/PB2 IIC 路径是否可用
//
// 引脚分配（基于 bt892x_pinfunction.md §8.5）：
//   PB1 = IIC_SCL_G3
//   PB2 = IIC_DAT_G3
//   PE6 = WP  (写保护，AT24C02 标准用法；高电平禁止写)
//
// 配置对比（探索 IICK 来源）：
//   Test A: 完全按参考代码（仅 CLKGAT2[0], FUNCMCON2=0x3, CLKCON1[23]=1, POSDIV=29）
//   Test B: + CLKGAT1|=BIT(29)
//   Test C: + CLKGAT1|=BIT(21)|BIT(29) + CLKCOCN2[31:24]=11 (force 2 MHz)
//   Test D: + CLKCOCN2[31:24]=19 (force 24/20 = 1.2 MHz, 对应 ~40 kHz SCL?)
//           → 真的验证: IICK = 24/(N+1)/POSDIV+1
//   Test E: + CLKCOCN2[31:24]=8 (force 24/9 = 2.67 MHz, ÷30 = 89 kHz)
//   Test F: 控制组 - 切回 RC2M (CLKCON1[23]=0)
//
// 每个测试 10 次 probe + TMR2 测 SCL freq
// 同时 AT24C02 应答 = 时钟 + 数据路径都正常
//
// 若 Test A 就工作，说明别人方案是正确的，CLKGAT1[29]=1 只是 nice-to-have
// 若 Test A 慢但 B 工作，说明 CLKGAT1[29] 确实是关键
// 若 A 都不工作但 E 工作，说明 CLKCON2 也要设

#include "test_common.h"

#define AT24C02_ADDR   0x50
#define PB1_MASK       BIT(1)
#define PB2_MASK       BIT(2)
#define PB1_2_MASK     (PB1_MASK | PB2_MASK)
#define PE6_MASK       BIT(6)

#define IIC_EN         BIT(0)
#define IIC_INTEN      BIT(1)
#define IIC_KS         BIT(28)
#define IIC_CLR_ALL    BIT(27)
#define IIC_CLR_DONE   BIT(29)
#define IIC_DONE       BIT(31)
#define IIC_ACKSTATUS  BIT(30)

// 完全重置 IIC 控制器
static void pb_iic_full_reset(void)
{
    IICCON0 = 0;
    delay_us(10);
}

// IIC 初始化（配置 PB1/PB2 引脚 + G3 映射）
// posdiv: POSDIV 值（0~63）
// clkccon2_div: CLKCON2[31:24] 分频系数 N，255 表示不动
static void pb_iic_init(u32 posdiv, u32 clkccon2_div, u32 extra_clkgat1)
{
    pb_iic_full_reset();

    // 1. 开 IIC 总时钟门
    CLKGAT2 |= BIT(0);

    // 2. 开 24M→Div 通路（如果需要）
    if (extra_clkgat1 != 0) {
        CLKGAT1 |= extra_clkgat1;
    }

    // 3. 设置 CLKCON2[31:24]（如果需要）
    if (clkccon2_div != 255) {
        CLKCON2 = (CLKCON2 & 0x00ffffff) | ((clkccon2_div & 0xFF) << 24);
    }

    // 4. FUNCMCON2 G3 映射（PB1=CLK, PB2=SDA）
    FUNCMCON2 = (FUNCMCON2 & ~(0xFu << 24)) | (0x3u << 24);

    // 5. PB1/PB2 PAD 配置：数字 IO + 拉 + 功能映射（G3 必须开 FEN）
    GPIOBDE  |=  PB1_2_MASK;
    GPIOBFEN |=  PB1_2_MASK;     // 开启外设功能映射（G3 接管）
    GPIOBPU  |=  PB1_2_MASK;     // 上拉
    GPIOBPD  &= ~PB1_2_MASK;
    GPIOBDIR &= ~PB1_MASK;       // PB1 (SCL) 输出
    GPIOBDIR |=  BIT(2);          // PB2 (SDA) 输入/输出(IIC 控制器会处理)

    // 6. PE6 (WP) - 仅做 GPIO 输出，不参与 IIC
    GPIOEDE  |=  PE6_MASK;
    GPIOEFEN &= ~PE6_MASK;
    GPIOEDIR &= ~PE6_MASK;        // 输出
    GPIOEPU  |=  PE6_MASK;
    GPIOESET  =  PE6_MASK;        // 默认高（禁止写 AT24C02）

    // 7. IIC 主控：POSDIV + UTEN
    IICCON0 = (0u << 2) | ((posdiv & 0x3F) << 4) | IIC_EN;
    delay_us(10);
    IICCON0 |= IIC_CLR_ALL;
    delay_us(100);
}

// 单个 probe（带 1 次 retry）
static bool pb_probe(u8 addr7, u32 timeout_us)
{
    IICCON0 |= IIC_CLR_ALL;
    IICCMDA  = (u8)((addr7 << 1) | 0);
    IICCON1  = BIT(3) | BIT(4) | BIT(11);   // START | CTL | STOP
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

// 测量 + 报告
static u32 pb_measure(const char *label, u32 timeout_us)
{
    const u32 N = 10;
    u32 t0 = TMR2CNT;
    u8 ack_cnt = 0;
    for (u32 i = 0; i < N; i++) {
        if (pb_probe(AT24C02_ADDR, timeout_us)) ack_cnt++;
        else if (pb_probe(AT24C02_ADDR, timeout_us)) ack_cnt++;  // 1 retry
    }
    u32 t1 = TMR2CNT;
    u32 elapsed = t1 - t0;
    u32 f = (elapsed > 0) ? (N * 9u * 1000u / elapsed) : 0;
    TEST_LOG("  %s: ACK=%u/%u, total=%u us, freq ~ %u kHz",
             label, ack_cnt, N * 2, elapsed, f);
    return f;
}

void test_i2c_pb_run(void)
{
    TEST_LOG("========================================");
    TEST_LOG("PB1/PB2/PE6 IIC test");
    TEST_LOG("========================================");

    // 保存初始状态
    u32 saved_clkgat1 = CLKGAT1;
    u32 saved_clkcon1 = CLKCON1;
    u32 saved_clkcon2 = CLKCON2;
    u32 saved_iiccon0 = IICCON0;
    TEST_LOG("[Init] CLKGAT1=0x%08x CLKCON1=0x%08x CLKCON2=0x%08x",
             saved_clkgat1, saved_clkcon1, saved_clkcon2);

    // ===== Test A: 完全按参考代码 =====
    TEST_LOG("[Test A] Reference config: CLKGAT2[0]+FUNCMCON2=0x3+CLKCON1[23]=1+POSDIV=29");
    TEST_LOG("  Expected IICK=3 MHz, SCL=100 kHz");
    TEST_LOG("  ** NO CLKGAT1[29]=1, NO CLKCON2 change **");
    pb_iic_init(29, 255, 0);     // posdiv=29, no CLKCON2 change, no CLKGAT1
    CLKCON1 |= BIT(23);           // select 1 path
    pb_measure("Test A", 200000);

    // ===== Test B: + CLKGAT1 |= BIT(29) =====
    TEST_LOG("[Test B] + CLKGAT1 |= BIT(29)");
    pb_iic_init(29, 255, BIT(29));
    pb_measure("Test B", 200000);

    // ===== Test C: 全部打开 + CLKCOCN2=11 =====
    TEST_LOG("[Test C] + CLKGAT1|=BIT(21)|BIT(29) + CLKCON2[31:24]=11");
    TEST_LOG("  强制 x24m_div_clk = 24/12 = 2 MHz");
    pb_iic_init(29, 11, BIT(21) | BIT(29));  // posdiv=29, ÷30 → 67 kHz
    pb_measure("Test C", 200000);

    // ===== Test D: 控制 - 切回 RC2M (CLKCON1[23]=0) =====
    TEST_LOG("[Test D] Reference config but CLKCON1[23]=0 (RC2M path)");
    CLKCON1 &= ~BIT(23);
    pb_measure("Test D", 200000);

    // ===== Test E: 验证 IICK 与 CLKCON2 关系 =====
    // 如果 IICK 真是 3 MHz, CLKCOCN2[31:24]=11 应该给 IICK=2 MHz
    // 那么 POSDIV=19 (÷20) 时 SCL = 100 kHz
    TEST_LOG("[Test E] CLKGAT1[29]=1 + CLKCON2[31:24]=11 + POSDIV=19");
    TEST_LOG("  if IICK = 2 MHz, SCL=100 kHz");
    pb_iic_init(19, 11, BIT(29));   // posdiv=19, ÷20
    pb_measure("Test E", 200000);

    // 恢复
    CLKGAT1 = saved_clkgat1;
    CLKCON1 = saved_clkcon1;
    CLKCON2 = saved_clkcon2;
    IICCON0 = saved_iiccon0;
    TEST_LOG("[End] Restored all registers to main.c defaults");

    TEST_LOG("========================================");
    TEST_LOG("PB1/PB2/PE6 test done");
    TEST_LOG("========================================");

    while (1);
}