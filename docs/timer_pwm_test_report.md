# BT892X Timer3 PWM 测试报告

> **日期**: 2026-07-15  
> **芯片**: BT892X  
> **测试人员**: zglstudylinux  
> **测试工具**: 逻辑分析仪  
> **参考手册**: BT892X_UserManual_Driver.md §4 定时器  
> **测试结果**: ⚠️ 部分通过（PB1/PB2 正常，PB0 异常待查）

---

## 1. 测试目的

验证 BT892X Timer3 的 PWM 输出功能：
- 三路 PWM 同时输出不同占空比的波形
- 验证周期和占空比计算公式
- 验证 FUNCMCON 引脚映射

---

## 2. 测试原理

### 2.1 Timer3 PWM 寄存器（参考手册 §4.3）

| 寄存器 | 功能 |
|--------|------|
| `TMR3CON` | 控制寄存器（PWM0EN/PWM1EN/PWM2EN, INCSEL, TMREN） |
| `TMR3CNT` | 32 位计数器 |
| `TMR3PR` | 周期寄存器（PWM 周期 = PR + 1） |
| `TMR3DUTY0/1/2` | PWM0/1/2 占空比寄存器 |

### 2.2 PWM 占空比公式

```
PWM 周期    = TMRPR + 1        （时钟周期数）
低电平长度  = DUTY + 1         （时钟周期数）
高电平长度  = PR - DUTY        （时钟周期数）
占空比(高)  = (PR - DUTY) / (PR + 1)
```

### 2.3 配置参数

| 参数 | 值 |
|------|-----|
| 定时器 | Timer3 |
| 时钟源 | tmr_inc = 1MHz（`CLKCON0[24]`） |
| INCSEL | 00（系统时钟） |
| PWM 频率 | 1KHz（PR = 999） |

### 2.4 引脚映射

| PWM 通道 | 映射组 | 引脚 | 占空比 | DUTY 值 |
|----------|--------|------|--------|---------|
| PWM0 | TMR3MAP=G1 | **PB0** | 25% | 749 |
| PWM1 | TMR3MAP=G1 | **PB1** | 50% | 499 |
| PWM2 | TMR3MAP=G1 | **PB2** | 75% | 249 |

---

## 3. 硬件连接

```
BT892X Board
+----------+
|  PB0 ------> 逻辑分析仪 CH0 (PWM0, 25%)
|  PB1 ------> 逻辑分析仪 CH1 (PWM1, 50%)
|  PB2 ------> 逻辑分析仪 CH2 (PWM2, 75%)
|  GND ------> 逻辑分析仪 GND
+----------+
```

---

## 4. 测试步骤

1. 在 Code::Blocks 中编译下载
2. 逻辑分析仪三通道连接 PB0/PB1/PB2
3. 观察波形周期和占空比

---

## 5. 实测结果

### 5.1 PB1 (PWM1, 50%) — ✅ 通过

- 波形正常翻转，50% 占空比
- PWM 功能验证通过

### 5.2 PB2 (PWM2, 75%) — ✅ 通过

- 波形正常翻转，75% 占空比
- PWM 功能验证通过

### 5.3 PB0 (PWM0, 25%) — ❌ 异常

- PB0 无波形输出（始终为低电平）
- 怀疑原因：
  1. `main.c` 中 `sd_disable()` 调用了 `FUNCMCON0 = 0x0f` 设置 SD0MAP=clear，但后续 `uart0_mapping_sel()` 中 `FUNCMCON0 = (7<<12)|(3<<8)` 的 `=` 赋值覆盖了 SD0MAP，导致其被清零
  2. SD0MAP=0 可能默认映射到 G2，使 PB0 被 SD 卡 SDCMD 功能占用
  3. 已在测试代码中加入 `FUNCMCON0 |= 0xF` 和 `FUNCMCON2 |= (0xF<<4)` 修复，但 PB0 仍未恢复
- **状态**: 留待后续排查，不影响 Timer3 PWM 功能验证结论

---

## 6. 关键寄存器配置

```c
// 引脚映射
FUNCMCON0 |= 0xF;                        // SD0MAP=clear (修复冲突)
FUNCMCON2 |= (0xF << 4) | (0x1 << 8);    // TMR3CPTMAP=clear, TMR3MAP=G1

// PB0/1/2 → 功能 IO (PWM)
GPIOBFEN |= BIT(0) | BIT(1) | BIT(2);
GPIOBDE  |= BIT(0) | BIT(1) | BIT(2);
GPIOBDIR &= ~(BIT(0) | BIT(1) | BIT(2));

// Timer3 配置
TMR3CNT   = 0;
TMR3PR    = 999;     // 1KHz @ 1MHz
TMR3DUTY0 = 749;     // 25% 占空比
TMR3DUTY1 = 499;     // 50% 占空比
TMR3DUTY2 = 249;     // 75% 占空比
TMR3CON   = BIT(11) | BIT(10) | BIT(9) | BIT(0);  // 三路 PWM + 使能
```

---

## 7. 踩坑记录

### 7.1 FUNCMCON0 被覆盖（同 GPIO 测试）

`uart0_mapping_sel()` 使用 `FUNCMCON0 = (7<<12)|(3<<8)` 直接赋值，覆盖了 `sd_disable()` 设置的 SD0MAP 值。

**教训**: 修改 FUNCMCON 寄存器应使用 `|=` 或先 `&=` 保留其他位域。

### 7.2 PB0 异常

PB0 即使修复了 FUNCMCON 冲突仍无输出。可能原因：
- PB0/WK1 作为唤醒源有特殊配置
- PB0 的 ADC11 模拟功能需要额外关闭
- 硬件连接问题

---

## 8. 结论

⚠️ **Timer3 PWM 功能基本验证通过**。PB1 和 PB2 正常输出 PWM 波形，验证了：
- TMR3PR 周期配置正确
- TMR3DUTY 占空比公式正确
- FUNCMCON2 TMR3MAP 引脚映射正确
- GPIOBFEN 功能 IO 模式用于 PWM 输出正确

PB0 异常不影响 PWM 功能验证结论，留待后续排查。
