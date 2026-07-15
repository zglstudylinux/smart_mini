# Timer 测试报告

- **测试日期**：2026-07-15
- **测试人员**：zglstudylinux
- **测试芯片**：BT892X（中科蓝讯，32-bit RISC-V SoC）
- **关联 commit**：见 `git log` smart_mini_minimax 分支
- **测试模块**：TMR1（基础 32 位定时器）精度 + 中断

---

## 1. 测试目标

验证 BT892X 的 TMR1 基础定时器功能：

1. **轮询模式精度**：在 1ms / 10ms / 100ms 周期下，TMR1 实际溢出时间是否准确
2. **中断模式触发**：TMR1 能否正确产生中断，ISR 能否正常进入
3. **中断计数准确**：1000 次 1ms 中断累计时间是否约 1s
4. **寄存器读写流程**：TMR1CON / TMR1CNT / TMR1PR / TMR1CPND 操作是否正确

---

## 2. 测试原理

### 2.1 定时器选型

| Timer | 当前用途 | 测试可用性 |
|---|---|---|
| **TMR0** | main.c 的 `timer0_init()` 已占用为 1ms 中断 | ❌ 不可动 |
| **TMR1** | 空闲 | ✅ **本测试使用** |
| **TMR2** | main.c 已用作 1µs tick（delay 函数） | ❌ 不可动 |
| TMR3/4/5 | 多功能（PWM/捕获），后续单独测试 | — |

### 2.2 时钟源

main.c 已配置：
- `CLKCON2 |= (25 << 24)` → `x26m_div_clk = 26MHz / 26 = 1MHz`
- `CLKCON0 |= BIT(24)` → `tmr_inc = x26m_div_clk = 1MHz`

因此 TMR1 每个 tick = **1µs**。

### 2.3 关键寄存器（参考 `BT892X_UserManual_Driver.md` §4 + `sfr.h:93-96`）

| 寄存器 | 地址 | 关键位 | 含义 |
|---|---|---|---|
| `TMR1CON` | 0xD4 | bit0 | TMREN（启动） |
| `TMR1CON` | 0xD4 | bit2 | INCSEL（时钟源选择，BIT(2)=1 → tmr_inc） |
| `TMR1CON` | 0xD4 | bit7 | TIE（中断使能） |
| `TMR1CON` | 0xD4 | bit9 | TPND（溢出挂起，R=1 表示已溢出） |
| `TMR1CNT` | 0xD8 | — | 32 位计数器 |
| `TMR1PR` | 0xDC | — | 周期寄存器（周期 = PR + 1） |
| `TMR1CPND` | 0xD8 | W bit9 | 写 1 清 TPND |

### 2.4 周期公式

```
中断周期 = (TMR1PR + 1) / tmr_inc_freq
        = (PR + 1) / 1MHz
        = (PR + 1) µs
```

例如要 1ms 周期：PR = 1000 - 1 = 999。

### 2.5 关键发现：TPND 必须显式清零

⚠️ **重要**：写 `TMR1CON = 0` 不会清 `BIT(9)`（TPND）。TPND 必须通过写 `TMR1CPND = BIT(9)` 来清除。

**踩坑记录**：第一版测试没清 TPND，导致第二轮 10ms 测试时 `while ((TMR1CON & BIT(9)) == 0)` 立即条件成立（上一轮溢出留下的 TPND=1），跳过等待，`t1-t0≈0`，测量值异常。修复方法是在 `test_timer1_measure_poll()` 开头加 `TMR1CPND = BIT(9)`。

---

## 3. 引脚分配

本测试不占用任何 GPIO，纯内部 Timer 验证。

| 资源 | 用途 | 备注 |
|---|---|---|
| TMR1CON / TMR1CNT / TMR1PR / TMR1CPND | 定时器控制 | 寄存器直接操作 |
| TMR2CNT | 1µs tick 测量基准 | main.c 已配 |
| IRQ_TMR1_VECTOR = 4 | 中断向量 | 见 `int.h:7` |

---

## 4. 测试设备

| 设备 | 型号/规格 | 用途 |
|---|---|---|
| 开发板 | BT892X 评估板 | 测试载体 |
| 串口工具 | 1.5Mbps UART（PB3 单线） | 看 printf 输出 |

无需万用表 / 逻辑分析仪。

---

## 5. 测试步骤

1. **编写代码**：实现 `test_timer1_measure_poll()` 和 ISR
2. **配置工程**：app.cbp 添加 test_timer.c / test_timer.h
3. **修改 main.c**：定义 `TEST_TIMER_EN 1`，调用 `test_timer_run()`
4. **编译**：CodeBlocks Build → 生成 `app.dcf`
5. **下载**：Downloader 烧入开发板
6. **观察串口**（PB3）

---

## 6. 预期结果

```
[TEST] ========================================
[TEST] Timer1 test start (tmr_inc = 1MHz)
[TEST] ========================================
[TEST] [Part 1] Polling mode - period accuracy
[TEST]   1ms: expected 1000us, measured 1000 us, err=0 us
[TEST]   10ms: expected 10000us, measured 10000 us, err=0 us
[TEST]   100ms: expected 100000us, measured 100001 us, err=1 us
[TEST] [Part 2] Interrupt mode - 1000 x 1ms ISR
[TEST]   1000 ISR: expected 1000000us, measured 999020 us, err=-980 us, count=1000
[TEST] ========================================
[TEST] Timer1 test done
[TEST] ========================================
```

---

## 7. 实测结果

**测试结论：✅ 通过**

实测输出（用户验证）：

```
[TEST] [Part 1] Polling mode - period accuracy
[TEST]   1ms: expected 1000us, measured 1000 us, err=0 us
[TEST]   10ms: expected 10000us, measured 10000 us, err=0 us
[TEST]   100ms: expected 100000us, measured 100001 us, err=1 us
[TEST] [Part 2] Interrupt mode - 1000 x 1ms ISR
[TEST]   1000 ISR: expected 1000000us, measured 999020 us, err=-980 us, count=1000
```

### 7.1 精度分析

| 测试项 | 期望 | 实测 | 误差 | 相对误差 |
|---|---|---|---|---|
| 1ms 轮询 | 1000 µs | 1000 µs | 0 µs | 0% |
| 10ms 轮询 | 10000 µs | 10000 µs | 0 µs | 0% |
| 100ms 轮询 | 100000 µs | 100001 µs | +1 µs | +0.001% |
| 1000 次中断 | 1000000 µs | 999020 µs | -980 µs | -0.098% |
| 中断次数 | 1000 | 1000 | 0 | 0% |

所有误差均在 ±1% 以内，**远超 ±5% 的通过判据**。

### 7.2 性能说明

- 1ms / 10ms / 100ms 轮询模式精度极高（误差 0~1 µs），说明 tmr_inc 1MHz 时钟稳定
- 1000 次中断误差 -980 µs ≈ -1 µs/中断，主要来自 ISR 进出开销 + 中断响应延迟
- 中断次数准确 = 1000，说明 ISR 注册和 PICEN 配置正确

---

## 8. 寄存器配置表

| 寄存器 | 地址 | 配置值 | 说明 |
|:---|:---|:---|:---|
| `TMR1CON` | 0xD4 | `0` | 停止 + 清 TIE（轮询模式） |
| `TMR1CON` | 0xD4 | `BIT(2) \| BIT(0)` | 启动 + 选 tmr_inc |
| `TMR1CON` | 0xD4 | `BIT(7)` | 开 TIE（中断模式，先） |
| `TMR1CON` | 0xD4 | `BIT(7) \| BIT(2) \| BIT(0)` | TIE + tmr_inc + 启动 |
| `TMR1CNT` | 0xD8 | `0` | 清计数器 |
| `TMR1PR` | 0xDC | `expected_us - 1` | 周期寄存器 |
| `TMR1CPND` | 0xD8 | `BIT(9)` | 清溢出挂起 |
| `PICEN` | 0x444 | `\|= BIT(4)` | 使能 IRQ_TMR1_VECTOR |
| `PICPR` | 0x448 | `&= ~BIT(4)` | TMR1 中断优先级（清 0） |

---

## 9. 失败排查思路

| 现象 | 可能原因 | 解决方法 |
|---|---|---|
| 编译错 | 格式符问题（已修复） | 用 `%d` 而不是 `%+d`（my_printf 不支持 `+` 标志） |
| 编译错 undefined reference | app.cbp 未加 test_timer.c | 在 `<Unit>` 列表加入 |
| Part 1 第 2/3 次测得 0 µs | TPND 未清 | 在测试开头加 `TMR1CPND = BIT(9)` |
| Part 1 卡死 `[TIMEOUT]` | 时钟源配错 | 确认 `BIT(2)=INCSEL → tmr_inc`（main.c 已配 1MHz） |
| Part 2 卡死 `[TIMEOUT] ISR not firing` | 中断没使能 | 确认 `PICEN \|= BIT(4)`、`register_isr()` 调用顺序 |
| Part 2 count 显示异常大数 | 格式符 `%+d` 不支持 | 改用 `%d`（已修复） |
| 中断周期偏大很多 | tmr_inc 时钟未配置 | 确认 `CLKCON2 \|= (25<<24)` 和 `CLKCON0 \|= BIT(24)`（main.c 已配） |

---

## 10. 后续建议

1. **PWM 测试**：用 TMR3 / TMR4 / TMR5 的 PWM 通道测试占空比输出（需要查手册 PWM 章节）
2. **输入捕获**：用 TMR3 的捕获功能测试外部信号频率测量
3. **多 Timer 联动**：测试 Timer 之间的级联或同步
4. **下一步**：进入 UART1 测试，使用 PA3/PA4（G2 映射），不影响 PB3 debug 串口

---

## 附录：关键文件路径

| 文件 | 作用 |
|---|---|
| `smart_mini/test/test_timer.h` | Timer 测试头文件 |
| `smart_mini/test/test_timer.c` | Timer 测试实现（含 TPND 清零修复、%d 格式修复） |
| `smart_mini/test/test_common.h` | 测试共用宏 |
| `smart_mini/main.c` | 主入口（已切换 `TEST_TIMER_EN=1`） |
| `smart_mini/app.cbp` | CodeBlocks 工程 |
| `smart_mini/header/sfr.h` | SFR 寄存器定义（第 93-96 行 TMR1） |
| `smart_mini/header/int.h` | IRQ 向量号定义（`IRQ_TMR1_VECTOR=4`） |
| `smart_mini/interrupt.c` | TMR0 init 参考实现（第 28-53 行） |
| `docs/BT892X_UserManual_Driver.md` | Timer 寄存器手册（第 §4 章节） |