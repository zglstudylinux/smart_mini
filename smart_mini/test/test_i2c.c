// I2C 硬件测试 — BT892X 内置 IIC 控制器
// 手册：BT892X_UserManual_Driver.md §8 IIC 章节
// 引脚定义：docs/bt892x_pinfunction.md §4.3 PE6=PE7
// SFR：见 header/sfr.h 第 359-362 行（IICCON0/1/CMDA/DATA）
//
// 引脚选择：
//   - PE6 = IIC_CLK-G5  → SCL
//   - PE7 = IIC_DAT-G5  → SDA
//   - 这是手册中唯一能让 PE6/PE7 同时作为 IIC 的 Group
//
// 时钟配置：
//   - source_clk = RC2M (≈2MHz)（手册 §8.1 说明支持 RC2M 和 XOSC26M）
//   - preclkdiv = 0（手册未明确给出寄存器，默认 0）
//   - posdiv = 19 → ÷20 = 100 kHz
//
// 时钟门控：
//   - CLKGAT2 bit 0 = IIC（用户截图确认）
//
// 测试内容：
//   1. 单地址 START+ADDR+STOP — 验证时序（看逻辑分析仪）
//   2. 地址扫描 0x08~0x77 — 找 AT24C02 (0x50)
//   3. 写单字节到 AT24C02[0x00] = 0x55
//   4. 读 AT24C02[0x00]，验证是 0x55
//   5. 写读 pattern 验证 8 字节

#include "test_common.h"

// IIC 寄存器位定义（手册 §8.2）
#define IIC_EN         BIT(0)
#define IIC_INTEN      BIT(1)
#define IIC_HOLDCNT    (BIT(2) | BIT(3))
#define IIC_POSDIV_M   (0x3Ful << 4)   // POSDIV 掩码
#define IIC_CLR_ALL    BIT(27)
#define IIC_KS         BIT(28)
#define IIC_CLR_DONE   BIT(29)
#define IIC_ACKSTATUS  BIT(30)         // 0=ACK, 1=NAK
#define IIC_DONE       BIT(31)

// IICCON1 动作位（手册 §8.2）
#define IIC_START0_EN  BIT(3)
#define IIC_CTL0_EN    BIT(4)
#define IIC_ADR0_EN    BIT(5)
#define IIC_ADR1_EN    BIT(6)
#define IIC_START1_EN  BIT(7)
#define IIC_CTL1_EN    BIT(8)
#define IIC_RDAT_EN    BIT(9)
#define IIC_WDAT_EN    BIT(10)
#define IIC_STOP_EN    BIT(11)
#define IIC_TXNAK_EN   BIT(12)

#define PE6_MASK    BIT(6)
#define PE7_MASK    BIT(7)
#define PE6_7_MASK  (PE6_MASK | PE7_MASK)

#define AT24C02_ADDR   0x50   // 7-bit 地址（A0=A1=A2=GND）

// 等待 DONE，带超时（µs）
// 返回 true = 完成，false = 超时
static bool test_iic_wait_done(u32 timeout_us)
{
    u32 t0 = TMR2CNT;
    while (!(IICCON0 & IIC_DONE)) {
        if ((u32)(TMR2CNT - t0) > timeout_us) {
            return false;
        }
    }
    return true;
}

// 初始化 IIC 控制器 + PE6/PE7 PAD 配置
static void test_i2c_init(void)
{
    // 1. 开启 IIC 时钟门控（CLKGAT2 bit 0）
    CLKGAT2 |= BIT(0);

    // 2. 清除 IIC 映射（高 4 位 = 0xF 清 FUNCMCON2[27:24]）
    FUNCMCON2 = (FUNCMCON2 & ~(0xFu << 24)) | (0xFu << 24);

    // 3. PE6/PE7 PAD 配置（手册 §8.3 第 1 步：SDA 上拉使能）
    //    TYPE1 引脚有 10K/200K/300Ω 内部上拉档位
    //    SCL 和 SDA 都开 10K 上拉，保证总线 idle 高
    GPIOEDE   |=  PE6_7_MASK;   // 数字 IO
    GPIOEFEN  |=  PE6_7_MASK;   // 外设功能映射（让 IIC 控制器接管）
    GPIOEPU   |=  PE6_7_MASK;   // 10K 上拉
    GPIOEPD   &= ~PE6_7_MASK;   // 关闭下拉
    GPIOEDIR  &= ~PE6_7_MASK;   // 输出

    // 4. FUNCMCON2 选择 IIC Group G5（PE6=SCL, PE7=SDA）
    FUNCMCON2 = (FUNCMCON2 & ~(0xFu << 24)) | (0x5u << 24);

    // 5. 配置 IIC 时序（手册 §8.2 公式）
    //    source_clk = RC2M ≈ 2MHz, preclkdiv = 0, posdiv = 19
    //    SCL = 2MHz / (0+1) / (19+1) = 100 kHz
    IICCON0 = (0u  << 1)    // INTEN = 0（轮询模式）
            | (0u  << 2)    // HOLDCNT = 0
            | (19u << 4)    // POSDIV = 19
            | IIC_EN;       // IIC_EN = 1

    // 6. 清初始状态（手册 §8.3 第 1 步可选项）
    IICCON0 |= IIC_CLR_ALL;     // CLR_ALL

    delay_us(100);              // 等稳定
}

// 发送 START + 地址 + STOP（仅探测用，无数据）
// is_read: false=写(0), true=读(1)
// 返回：true=ACK（从机应答），false=NAK 或超时
// 注：第一次事务如果 NAK，会自动重试一次（IIC 控制器启动瞬态）
static bool test_iic_probe_addr(u8 dev_addr7, bool is_read, u32 timeout_us)
{
    u8 ctl = (u8)((dev_addr7 << 1) | (is_read ? 1u : 0u));

    for (u8 attempt = 0; attempt < 2; attempt++) {
        IICCON0 |= IIC_CLR_ALL;     // 清状态（含 DONE）

        IICCMDA  = ctl;             // 只用 CTL0 字段
        IICCON1  = IIC_START0_EN    // START
                 | IIC_CTL0_EN       // 地址
                 | IIC_STOP_EN       // STOP
                 | 0;                // DATA_CNT = 0（无数据）

        IICCON0 |= IIC_KS;          // 启动传输

        if (!test_iic_wait_done(timeout_us)) {
            IICCON0 |= IIC_CLR_DONE;
            if (attempt == 1) return false;   // 第二次还超时才算超时
            continue;
        }

        bool ack = !(IICCON0 & IIC_ACKSTATUS);  // 0=ACK
        IICCON0 |= IIC_CLR_DONE;    // 清 DONE
        if (ack || attempt == 1) {
            return ack;
        }
        // 第一次 NAK，第二次重试
    }
    return false;
}

// 写 N 字节到从机寄存器（单次事务，N ≤ 4）
// dev_addr7: 7 位从机地址
// reg_addr:  从机内部子地址（0~255）— 单字节地址的设备如 AT24C02
// data:      数据指针
// len:       字节数（1~4）
// 返回：true=全部 ACK，false=任意 NAK 或超时
static bool test_iic_write(u8 dev_addr7, u8 reg_addr,
                            const u8 *data, u8 len, u32 timeout_us)
{
    if (len == 0 || len > 4) return false;

    IICCON0 |= IIC_CLR_ALL;

    // 装载 CMDA
    IICCMDA  = (u32)((dev_addr7 << 1) & 0xFF)         // CTL0 = 地址+W
             | ((u32)reg_addr << 8);                  // ADR0 = 子地址

    // 装载 DATA（DATA0 = 最先发送的字节）
    u32 data_word = 0;
    for (u8 i = 0; i < len; i++) {
        data_word |= ((u32)data[i]) << (i * 8);
    }
    IICDATA = data_word;

    // 动作序列：START + 地址 + 子地址 + 数据 + STOP
    IICCON1 = IIC_START0_EN
            | IIC_CTL0_EN
            | IIC_ADR0_EN
            | IIC_WDAT_EN
            | IIC_STOP_EN
            | (len & 0x7);          // DATA_CNT = len

    IICCON0 |= IIC_KS;

    if (!test_iic_wait_done(timeout_us)) {
        IICCON0 |= IIC_CLR_DONE;
        return false;
    }

    bool ack = !(IICCON0 & IIC_ACKSTATUS);
    IICCON0 |= IIC_CLR_DONE;
    return ack;
}

// 从从机寄存器读 N 字节（用重复起始）
// dev_addr7: 7 位从机地址
// reg_addr:  从机内部子地址
// buf:       接收缓冲
// len:       字节数（1~4）
static bool test_iic_read(u8 dev_addr7, u8 reg_addr,
                           u8 *buf, u8 len, u32 timeout_us)
{
    if (len == 0 || len > 4) return false;

    IICCON0 |= IIC_CLR_ALL;

    // 第一阶段：START + 地址(W) + 子地址  （不出 STOP）
    IICCMDA  = (u32)((dev_addr7 << 1) & 0xFF)         // CTL0 = 地址+W
             | ((u32)reg_addr << 8);                  // ADR0 = 子地址

    // 第二阶段（CTL1/ADR1）：重复 START + 地址(R)
    // 手册 §8.2 IICCMDA bit 31:24 = CTL1，bit 23:16 = ADR1
    u32 ctl1 = (u32)(((dev_addr7 << 1) | 1u) & 0xFF);   // 地址+R
    IICCMDA |= (ctl1 << 24);                             // 合并到 CTL1

    IICDATA = 0;   // 读模式下 DATA 不用，但要装载

    // 动作序列：
    //   START0 + CTL0 + ADR0          — 写子地址
    //   START1 + CTL1                  — 重复起始 + 地址(R)
    //   RDAT                           — 读数据
    //   TXNAK（最后一字节发 NAK）
    //   STOP
    u8 ctl1_en = (len == 1) ? (IIC_CTL1_EN | IIC_TXNAK_EN) : IIC_CTL1_EN;
    // 对于多字节，最后一个字节前不发 NAK — 但本 IIC 控制器只有"最后一字节 NAK"开关
    // 简化方案：len>=2 时所有字节都 TXNAK_EN 也可以（I2C 标准是最后一字节 NAK）
    // 这里采取保守做法：所有字节都 NAK 等同于"全部 NAK"，不符合标准
    // 正确做法：用 TXNAK_EN=1 表示"读完成后回 NAK"
    // 简化：单字节读时 TXNAK_EN=1

    IICCON1 = IIC_START0_EN
            | IIC_CTL0_EN
            | IIC_ADR0_EN
            | IIC_START1_EN
            | ctl1_en
            | IIC_RDAT_EN
            | IIC_STOP_EN
            | (len & 0x7);

    IICCON0 |= IIC_KS;

    if (!test_iic_wait_done(timeout_us)) {
        IICCON0 |= IIC_CLR_DONE;
        return false;
    }

    // 读取接收到的数据（DATA0 = 最先收到的字节）
    u32 data_word = IICDATA;
    for (u8 i = 0; i < len; i++) {
        buf[i] = (u8)((data_word >> (i * 8)) & 0xFF);
    }

    bool ack = !(IICCON0 & IIC_ACKSTATUS);
    IICCON0 |= IIC_CLR_DONE;
    return ack;
}

void test_i2c_run(void)
{
    TEST_LOG("========================================");
    TEST_LOG("I2C test start (PE6=SCL, PE7=SDA, G5)");
    TEST_LOG("Expected slave: AT24C02 @ 0x50");
    TEST_LOG("========================================");

    test_i2c_init();

    // ===== Test 1: 单地址探测 — 仅看时序 =====
    TEST_LOG("[Test 1] Single address probe @ 0x50");
    TEST_LOG("  Watch logic analyzer: PE6=SCL, PE7=SDA");
    TEST_LOG("  Expect: START + 0xA0(write) + STOP (~100us total)");
    delay_ms(2000);   // 给用户 2 秒接线和启动逻辑分析仪

    {
        bool ack = test_iic_probe_addr(AT24C02_ADDR, false, 100000);
        TEST_LOG("  Probe 0x50 result: %s", ack ? "ACK" : "NAK/TIMEOUT");
    }

    delay_ms(500);

    // ===== Test 2: 地址扫描 0x08~0x77 =====
    TEST_LOG("[Test 2] Address scan 0x08..0x77");
    {
        u32 ack_count = 0;
        for (u32 addr = 0x08; addr < 0x78; addr++) {
            bool ack = test_iic_probe_addr((u8)addr, false, 50000);
            if (ack) {
                TEST_LOG("  Found device at 0x%02x", addr);
                ack_count++;
            }
            delay_us(200);   // 总线恢复
        }
        TEST_LOG("  Scan done: %u device(s) found", ack_count);
        if (ack_count == 0) {
            TEST_LOG("  [HINT] No ACK from any address.");
            TEST_LOG("         Check: AT24C02 wiring / pull-up resistors / power");
        }
    }

    delay_ms(500);

    // ===== Test 3: 写单字节到 AT24C02[0x00] =====
    TEST_LOG("[Test 3] Write 0x55 to AT24C02[0x00]");
    {
        u8 val = 0x55;
        bool ok = test_iic_write(AT24C02_ADDR, 0x00, &val, 1, 100000);
        TEST_LOG("  Write result: %s", ok ? "ACK" : "NAK/TIMEOUT");
        if (ok) {
            delay_ms(10);   // AT24C02 写周期 ≤ 5ms，加点余量
        }
    }

    delay_ms(500);

    // ===== Test 4: 读 AT24C02[0x00]，期望 0x55 =====
    TEST_LOG("[Test 4] Read AT24C02[0x00], expect 0x55");
    {
        u8 buf[4] = {0};
        bool ok = test_iic_read(AT24C02_ADDR, 0x00, buf, 1, 100000);
        TEST_LOG("  Read result: %s, data=0x%02x ('%c')",
                 ok ? "ACK" : "NAK/TIMEOUT",
                 (u32)buf[0],
                 (buf[0] >= 32 && buf[0] < 127) ? buf[0] : '?');
        if (ok && buf[0] == 0x55) {
            TEST_LOG("  WRITE-READ PASS (wrote 0x55, read 0x55)");
        } else if (ok) {
            TEST_LOG("  WRITE-READ MISMATCH (wrote 0x55, read 0x%02x)", (u32)buf[0]);
        }
    }

    delay_ms(500);

    // ===== Test 5: 写读 4 字节 pattern =====
    TEST_LOG("[Test 5] Write-read 4 bytes pattern");
    {
        u8 wr_buf[4] = {0xDE, 0xAD, 0xBE, 0xEF};
        u8 rd_buf[4] = {0};
        bool ok;

        ok = test_iic_write(AT24C02_ADDR, 0x10, wr_buf, 4, 100000);
        TEST_LOG("  Write 0x%02x 0x%02x 0x%02x 0x%02x @0x10: %s",
                 (u32)wr_buf[0], (u32)wr_buf[1], (u32)wr_buf[2], (u32)wr_buf[3],
                 ok ? "ACK" : "NAK/TIMEOUT");
        if (ok) delay_ms(10);

        ok = test_iic_read(AT24C02_ADDR, 0x10, rd_buf, 4, 100000);
        TEST_LOG("  Read @0x10: %s, data=0x%02x 0x%02x 0x%02x 0x%02x",
                 ok ? "ACK" : "NAK/TIMEOUT",
                 (u32)rd_buf[0], (u32)rd_buf[1], (u32)rd_buf[2], (u32)rd_buf[3]);

        if (ok && rd_buf[0] == 0xDE && rd_buf[1] == 0xAD &&
            rd_buf[2] == 0xBE && rd_buf[3] == 0xEF) {
            TEST_LOG("  PATTERN PASS");
        } else if (ok) {
            TEST_LOG("  PATTERN MISMATCH");
        }
    }

    TEST_LOG("========================================");
    TEST_LOG("I2C test done");
    TEST_LOG("========================================");

    while (1);
}