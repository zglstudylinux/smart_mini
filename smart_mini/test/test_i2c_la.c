// 逻辑分析仪（LA）专用 IIC 测试
// 用途：当 PE6/PE7 接逻辑分析仪看 SCL 波形时（不能同时接 AT24C02），用此测试观察 SCL 实际频率
//
// 不依赖 AT24C02 ACK：即使没有从机应答，IIC 控制器仍会按配置驱动 SCL
// 完成 START + 8 data + 1 ACK + STOP = 9 个 SCL 周期/事务
//
// 测试流程：
//   Test 1: CLKCON1[23] = 0 (按旧假设是 RC2M 路径)
//   Test 2: CLKCON1[23] = 1 (按旧假设是 x24m_div_clk 路径)
//   每个测试做 50 次事务，让 LA 抓取足够 SCL 脉冲
//   测试间有 3 秒间隔，方便 LA 抓不同阶段的波形
//
// 测什么：
//   - SCL 周期 = (脉冲时长) 应该在 ~10 µs (100 kHz) 或其他值
//   - 如果 Test 1 = 10 µs 但 Test 2 = 1 ms → bit 23 选择确实切到 x24m_div_clk
//   - 如果两个都 ~10 µs → 两个选择都工作在 2 MHz
//   - 如果两个都 ~1 ms → CLKGAT1[29]=1 没生效或仍有问题
//
// 接法：CH1 接 PE6 (SCL), CH2 接 PE7 (SDA), GND 接 GND
// AT24C02 断开或 VCC 不接即可（不影响 IIC 驱动）

#include "test_common.h"

#define AT24C02_ADDR   0x50    // 没有从机时也会 ACK 失败但 SCL 仍驱动
#define PE6_MASK       BIT(6)
#define PE7_MASK       BIT(7)
#define PE6_7_MASK     (PE6_MASK | PE7_MASK)

#define IIC_EN         BIT(0)
#define IIC_KS         BIT(28)
#define IIC_CLR_ALL    BIT(27)
#define IIC_CLR_DONE   BIT(29)
#define IIC_DONE       BIT(31)
#define IIC_ACKSTATUS  BIT(30)
#define IIC_START0_EN  BIT(3)
#define IIC_CTL0_EN    BIT(4)
#define IIC_STOP_EN    BIT(11)

#define TX_PER_TEST    50     // 每个测试做 50 次事务，足够 LA 抓取

// IIC 全初始化（含 CLKGAT1[29] gate）
static void la_iic_init(void)
{
    // 1. 开 IIC 总时钟门
    CLKGAT2 |= BIT(0);

    // 2. **关键**：开 24M → Div 通路
    CLKGAT1 |= BIT(21) | BIT(29);

    // 3. 设 Div 输出 24M / (11+1) = 2 MHz (即使没有这个，初始 25 应给 ~923 kHz)
    //    设 11 让 Div 输出正好 2 MHz，便于 LA 观察
    CLKCON2 = (CLKCON2 & 0x00ffffff) | (11u << 24);

    // 4. FUNCMCON2 G5 映射 (PE6 SCL + PE7 SDA)
    FUNCMCON2 = (FUNCMCON2 & ~(0xFu << 24)) | (0x5u << 24);

    // 5. PE6/PE7 PAD 配置
    GPIOEDE   |=  PE6_7_MASK;
    GPIOEFEN  |=  PE6_7_MASK;
    GPIOEPU   |=  PE6_7_MASK;     // 10K 上拉
    GPIOEPD   &= ~PE6_7_MASK;
    GPIOEDIR  &= ~PE6_7_MASK;

    // 6. IIC 主控使能 (POSDIV=19, 期望 SCL ≈ 100 kHz @ 2 MHz src)
    IICCON0 = 0;  // 先关闭 IIC_EN
    delay_us(10);
    IICCON0 = (0u << 2) | (19u << 4) | IIC_EN;
    delay_us(10);
    IICCON0 |= IIC_CLR_ALL;
    delay_us(100);
}

// 单个 probe（短超时，无 retry，加速）
static void la_probe(void)
{
    IICCON0 |= IIC_CLR_ALL;
    IICCMDA  = (u8)((AT24C02_ADDR << 1) | 0);  // 地址+W
    IICCON1  = IIC_START0_EN | IIC_CTL0_EN | IIC_STOP_EN | 0;
    IICCON0 |= IIC_KS;

    u32 t0 = TMR2CNT;
    while (!(IICCON0 & IIC_DONE)) {
        if ((u32)(TMR2CNT - t0) > 1000) {  // 1ms 超时
            IICCON0 |= IIC_CLR_DONE;
            return;
        }
    }
    IICCON0 |= IIC_CLR_DONE;
}

// 一组测试：50 次事务，匹配到指定时钟源
static void la_run_one(const char *label, u32 clr_mask_set)
{
    TEST_LOG("");
    TEST_LOG("[%s] starting, expect SCL period on logic analyzer", label);
    TEST_LOG("  Capturing 50 transactions now...");

    // 选择时钟源
    if (clr_mask_set == 0) {
        // 选 0 路径 (清除 bit 23)
        CLKCON1 &= ~BIT(23);
        TEST_LOG("  CLKCON1[23] = 0 (path A)");
    } else {
        // 选 1 路径 (置位 bit 23)
        CLKCON1 |= BIT(23);
        TEST_LOG("  CLKCON1[23] = 1 (path B)");
    }

    // 重置 IIC 状态机
    IICCON0 = (IICCON0 & ~(0x3Ful << 4)) | (19ul << 4) | IIC_EN;
    delay_us(100);

    // 做 50 次事务（无论 ACK 与否都完成 SCL 序列）
    for (u32 i = 0; i < TX_PER_TEST; i++) {
        la_probe();
    }

    TEST_LOG("  Done 50 transactions");
    TEST_LOG("  >> Now measure SCL period on logic analyzer <<");
}

void test_i2c_la_run(void)
{
    TEST_LOG("========================================");
    TEST_LOG("Logic Analyzer IIC Clock Test");
    TEST_LOG("========================================");
    TEST_LOG("Wire: CH1 -> PE6 (SCL), CH2 -> PE7 (SDA)");
    TEST_LOG("Note: AT24C02 may be disconnected - that's OK");
    TEST_LOG("Note: CLKGAT1[29]=1 (24M->Div gate) is set");
    TEST_LOG("Note: CLKCON2[31:24]=11 (force Div = 2 MHz)");
    TEST_LOG("Note: POSDIV=19, target SCL ~ 100 kHz if IICK = 2 MHz");
    TEST_LOG("");

    la_iic_init();

    // 读出当前寄存器状态
    TEST_LOG("[Initial state]");
    TEST_LOG("  CLKGAT1 = 0x%08x", CLKGAT1);
    TEST_LOG("  CLKCON2 = 0x%08x", CLKCON2);
    TEST_LOG("  CLKCON1 = 0x%08x", CLKCON1);
    TEST_LOG("");

    TEST_LOG("Each test does 50 transactions. Wait 5 sec between tests.");
    TEST_LOG("Logic analyzer should show SCL pulses on PE6.");

    // Test 1: bit 23 = 0
    la_run_one("Test 1: CLKCON1[23] = 0", 0);
    delay_ms(5000);   // 5 秒给 LA 捕获

    // Test 2: bit 23 = 1
    la_run_one("Test 2: CLKCON1[23] = 1", 1);
    delay_ms(5000);   // 5 秒给 LA 捕获

    TEST_LOG("");
    TEST_LOG("========================================");
    TEST_LOG("Logic Analyzer test done");
    TEST_LOG("Report SCL period measured for each test.");
    TEST_LOG("========================================");

    while (1);
}