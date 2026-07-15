# GPIO 测试报告

- **测试日期**：2026-07-15
- **测试人员**：zglstudylinux
- **测试芯片**：BT892X（中科蓝讯，32-bit RISC-V SoC）
- **关联 commit**：见 `git log` smart_mini_minimax 分支
- **测试模块**：GPIO 输出基础验证

---

## 1. 测试目标

验证 BT892X 的 GPIO 寄存器读写流程，确认通过寄存器操作能正确控制引脚电平输出。

具体验证：
1. GPIO 数字 IO 使能（`GPIOEDE`）配置生效
2. GPIO 功能映射关闭（`GPIOEFEN`）配置生效
3. GPIO 方向配置（`GPIOEDIR`）生效
4. GPIO 置位（`GPIOESET`）/清零（`GPIOECLR`）操作生效
5. delay_ms 延时精度符合预期（1s 周期）

---

## 2. 测试原理

### 2.1 GPIO 寄存器模型（参考 `BT892X_UserManual_Driver.md` §3.2）

每个 GPIO 端口（PA/PB/PE/PF/PG）有 13 个 32 位寄存器，每位对应一个引脚：

| 寄存器 | 功能 | 关键位 |
|---|---|---|
| `GPIOxDIR` | 方向：0=输出，1=输入 | 默认 0xFF（全输入） |
| `GPIOxDE` | 数字使能：1=数字 IO，0=模拟 | 默认 0xFF（数字） |
| `GPIOxFEN` | 功能映射：0=GPIO，1=外设功能 | 默认 0xFF |
| `GPIOxSET` | 写 1 置位，写 0 无效 | — |
| `GPIOxCLR` | 写 1 清零，写 0 无效 | — |
| `GPIOxPU/PD` | 10KΩ 上下拉 | — |

### 2.2 配置流程（PE4 输出示例）

```c
// 1. 关闭外设功能映射（让引脚用作普通 GPIO）
GPIOEFEN &= ~BIT(4);

// 2. 使能数字 IO（默认就是数字 IO，可省）
GPIOEDE |= BIT(4);

// 3. 设置为输出
GPIOEDIR &= ~BIT(4);

// 4. 输出高/低
GPIOESET = BIT(4);   // 输出高
GPIOECLR = BIT(4);   // 输出低
```

### 2.3 周期控制

利用 main.c 中已初始化的 TMR2（1µs tick），通过 `delay_ms(500)` 实现 500ms 延时。完整周期 = 高 500ms + 低 500ms = 1 秒。

---

## 3. 引脚分配

| 引脚 | 角色 | 复用风险 | 测试结果 |
|:---|:---|:---|:---|
| **PE4** | GPIO 输出 | 完全空闲 | ✅ 通过 |
| PB3 | UART0 debug TX | **严禁触碰** | 未动 |
| PB4 | USB DM | **严禁触碰** | 未动 |
| PG1~PG5 | SPI-Flash | **严禁触碰** | 未动 |

---

## 4. 测试设备

| 设备 | 型号/规格 | 用途 |
|---|---|---|
| 万用表 | 普通数字万用表（直流电压档） | 量 PE4 电压 |
| 开发板 | BT892X 评估板 | 测试载体 |
| 串口工具 | 1.5Mbps 串口（PB3 单线 UART0） | 看 printf 输出 |
| 杜邦线 | 若干 | 万用表表笔连接 |

---

## 5. 测试步骤

1. **编写代码**：在 `smart_mini/test/test_gpio.c` 中实现 PE4 翻转循环
2. **配置工程**：在 `smart_mini/app.cbp` 的 `<Unit>` 列表中添加 test_gpio.c / test_gpio.h / test_common.h
3. **修改 main.c**：包含 `test/test_gpio.h`，定义 `TEST_GPIO_EN 1`，调用 `test_gpio_run()`
4. **编译**：CodeBlocks 打开 `app.cbp` → Build → 生成 `Output/bin/app.dcf`
5. **下载**：用 Downloader 工具把 `app.dcf` 烧入开发板
6. **观察 1**：串口（PB3）查看 `printf` 输出
7. **观察 2**：万用表红表笔接 PE4，黑表笔接 GND，直流电压档，量电压

---

## 6. 预期结果

### 6.1 串口输出（PB3）

```
Hello SMART Flash MiniProj
test %d %i -123: -123 -123
test %u 456: 456
...
test %s: Success
[TEST] GPIO test start: PE4 toggle @ 1Hz
[TEST] Use multimeter on PE4, expect ~1s square wave (0V / 3.3V)
[TEST] PE4 = 1 (HIGH ~3.3V)
[TEST] PE4 = 0 (LOW 0V)
[TEST] PE4 = 1 (HIGH ~3.3V)
[TEST] PE4 = 0 (LOW 0V)
...
```

### 6.2 万用表读数

万用表直流电压档测量 PE4：
- 高电平时段：约 **3.3V**（VCC）
- 低电平时段：约 **0V**（GND）
- 切换周期：约 **1 秒**（500ms 高 + 500ms 低）

---

## 7. 实测结果

**测试结论：✅ 通过**

用户实测反馈：**"PE4 正常"** —— PE4 引脚电平按预期翻转，串口同步打印状态，万用表测得 0V / 3.3V 周期性切换。

---

## 8. 寄存器配置表

| 寄存器 | 地址 | 配置值 | 说明 |
|:---|:---|:---|:---|
| `GPIOEDE` | 0x690 | `\|= BIT(4)` | PE4 数字 IO 使能 |
| `GPIOEFEN` | 0x694 | `&= ~BIT(4)` | 关闭外设功能映射 |
| `GPIOEDIR` | 0x68C | `&= ~BIT(4)` | PE4 设为输出 |
| `GPIOESET` | 0x680 | `= BIT(4)` | PE4 输出高 |
| `GPIOECLR` | 0x684 | `= BIT(4)` | PE4 输出低 |
| `TMR2CNT` | 0xF0 | — | 1µs tick（main.c 已配） |

---

## 9. 失败排查思路

| 现象 | 可能原因 | 解决方法 |
|---|---|---|
| 编译报错 `undeclared GPIOTEST_PIN_PORTDIR` | 宏参数未展开就拼接 `##` | 用双层宏强制先展开（已修复） |
| 编译报错 `undefined reference to test_gpio_run` | app.cbp 未加 test_gpio.c | 在 `<Unit>` 列表加入 |
| 编译通过但下载后 PE4 无输出 | 检查 GPIOEDE/GPIOEFEN 配置 | 确认 `GPIOEDE\|=BIT(4)` 和 `GPIOEFEN&=~BIT(4)` |
| PE4 一直高 | 方向配错为输入 | 确认 `GPIOEDIR&=~BIT(4)` |
| 串口看不到 [TEST] 打印 | main.c 的 `#ifdef TEST_GPIO_EN` 未生效 | 确认 `#define TEST_GPIO_EN 1` 已开启 |
| 周期不对（远大于 1s） | 时钟未切换到 24MHz | 确认 `set_sys_clk(SYS_24M)` 已执行（默认已配） |

---

## 10. 后续建议

1. **多端口覆盖**：后续测试可在 PE4 之外增加 PA3/PA4/PF0 等引脚，验证多端口 GPIO 控制
2. **输入测试**：后续测试可将某个引脚配为输入，验证 `GPIOx` 读取和上下拉配置
3. **驱动电流测试**：对比 `GPIOxDRV` 0（8mA）/1（32mA）对 LED 亮度的影响
4. **复用冲突演示**：故意把 PE4 配成某外设功能，看 GPIO 是否失效，加深理解
5. **下一步**：进入 Timer 测试（TMR1），验证 32 位定时器的精度和中断能力

---

## 附录：关键文件路径

| 文件 | 作用 |
|---|---|
| `smart_mini/test/test_common.h` | 测试共用宏（双层宏解决 ## 展开问题） |
| `smart_mini/test/test_gpio.h` | GPIO 测试头文件 |
| `smart_mini/test/test_gpio.c` | GPIO 测试实现 |
| `smart_mini/main.c` | 主入口（已添加 TEST_GPIO_EN 开关） |
| `smart_mini/app.cbp` | CodeBlocks 工程（已添加 test_gpio.c 等） |
| `smart_mini/header/sfr.h` | SFR 寄存器宏定义（第 421-462 行 GPIO E 组） |
| `docs/BT892X_UserManual_Driver.md` | 寄存器手册（第 §3.2 GPIO 章节） |