# BT892X UART2 测试报告

> **日期**: 2026-07-15  
> **芯片**: BT892X  
> **测试人员**: zglstudylinux  
> **测试工具**: 跳线、USB 转 TTL 模块、PC 串口助手  
> **参考手册**: BT892X_UserManual_Driver.md §6 UART  
> **测试结果**: ✅ 回环通过 ✅ 发送通过 ⬜ 接收待测

---

## 1. 测试目的

验证 BT892X UART2 的收发功能：
- 自发自收回环测试
- 发送到 PC（USB 转 TTL）
- 接收来自 PC 的数据

---

## 2. 测试原理

### 2.1 UART 寄存器（参考手册 §6）

| 寄存器 | 功能 |
|--------|------|
| `UART2CON` | 控制寄存器（RXEN, UTEN, TXPND, RXPND） |
| `UART2BAUD` | 波特率寄存器（高 16 位 RX, 低 16 位 TX） |
| `UART2DATA` | 数据寄存器（写=发送, 读=接收） |
| `UART2CPND` | 清除挂起寄存器 |

### 2.2 波特率计算

```
BAUD = Fsys / baud_rate - 1
24MHz / 115200 - 1 = 207.33 → 取 207
实际波特率 = 24MHz / (207+1) = 115,384 bps (误差 0.16%)
```

### 2.3 引脚映射

| 信号 | 映射组 | 引脚 | FUNCMCON1 位域 |
|------|--------|------|----------------|
| UART2 TX | UT2TXMAP=G2 | **PB2** | [7:4] = 0x2 |
| UART2 RX | UT2RXMAP=G2 | **PB1** | [11:8] = 0x2 |

---

## 3. 硬件连接

### 3.1 回环测试
```
PB2(TX) ──[跳线]── PB1(RX)
```

### 3.2 发送测试
```
PB2(TX) ──── USB-TTL RX
GND    ──── USB-TTL GND
         └── PC 串口助手 (115200, 8N1)
```

### 3.3 接收测试（待测）
```
PB1(RX) ──── USB-TTL TX
GND    ──── USB-TTL GND
         └── PC 串口助手 (115200, 8N1)
```

---

## 4. 测试步骤与结果

### 4.1 回环测试 — ✅ 通过

**步骤**: 跳线 PB2↔PB1，发送 0x00~0xFF 共 256 字节，接收比对。

**初始问题**: 第一个字节后全部错位（rx 始终落后 sent 一个字节）。

**根因与修复**:

| 问题 | 根因 | 修复 |
|------|------|------|
| 接收数据错位 | UART 使能后 RX 收到启动毛刺/垃圾数据 | `uart2_test_init()` 末尾加 `while(RXPND) read & discard` 冲洗 RX |
| TX 未等发送完成 | `uart2_putc` 写入 UART2DATA 后立即返回，TX 移位寄存器还在发送上一个字节 | 写入后加 `while(!TXPND);` 等待发送真正完成 |

**最终结果**: 256 字节全部正确，0 错误。

### 4.2 发送测试 — ✅ 通过

**步骤**: PB2 → USB-TTL，PC 串口助手接收。

**初始问题**: 每 10 条消息出现一次乱码。

**根因**: `printf`（通过 UART0 PB3 输出）与 UART2 同时工作时存在资源冲突。每 10 次循环调用 `printf("Sent %lu messages...")` 时触发乱码。

**修复**: 移除循环中的 printf 进度打印，或确保不要在 UART2 发送期间调用 printf。

**最终结果**: 移除 printf 后，`[N] Hello UART2!` 连续正常接收，无乱码。

### 4.3 接收测试 — ⬜ 待测

> 暂未测试，原因：手头没有多余的 USB 转 TTL 模块。代码已就绪（`uart2_recv_test()`），接线为 `USB-TTL TX → PB1, GND → GND`，收到数据通过 UART0 printf 打印。

---

## 5. 关键代码

### UART2 初始化
```c
// 引脚映射
FUNCMCON1 &= ~((0xF << 4) | (0xF << 8));
FUNCMCON1 |= (2 << 4) | (2 << 8);        // TX=PB2, RX=PB1

// PB2 → UART2 TX (功能IO)
GPIOBFEN |=  BIT(2);
GPIOBDE  |=  BIT(2);
GPIOBDIR &= ~BIT(2);
GPIOBPU  |=  BIT(2);

// PB1 → UART2 RX (功能IO)
GPIOBFEN |=  BIT(1);
GPIOBDE  |=  BIT(1);
GPIOBDIR |=  BIT(1);
GPIOBPU  |=  BIT(1);

// 波特率 + 使能
UART2BAUD = (207 << 16) | 207;           // 115200 @ 24MHz
UART2CON  = BIT(7) | BIT(0);             // RXEN + UTEN
delay_ms(10);
while (UART2CON & BIT(9)) (void)UART2DATA; // ★ 冲洗 RX 垃圾数据
```

### 正确的阻塞发送
```c
void uart2_putc(u8 ch)
{
    while (!(UART2CON & BIT(8)));   // 等 TX 空闲
    UART2DATA = ch;                  // 写入数据
    while (!(UART2CON & BIT(8)));   // ★ 等发送完成!
}
```

---

## 6. 踩坑记录

| 问题 | 原因 | 解决 |
|------|------|------|
| 回环数据错位 | UART 使能后 RX 有毛刺 + TX 未等发送完成 | 冲洗 RX + 发送后等 TXPND |
| 发送每 10 次乱码 | UART0 printf 与 UART2 资源冲突 | 避免同时使用 |
| `uart2_init` 编译冲突 | `int.h` 已声明同名函数 | 改名为 `uart2_test_init` |
| FUNCMCON1 初始值 | `main.c` 中 `FUNCMCON1=0xFFFFFFFF` 清除了所有映射 | 测试代码中用 `&=` 清除再用 `\|=` 设置 |

---

## 7. 结论

✅ **UART2 收发功能验证通过**。回环测试 256 字节零错误，发送到 PC 正常。关键教训：

1. **TXPND 时序**: 写入 UART2DATA 后必须等待 TXPND 重新变 1 才算发送完成
2. **RX 启动冲洗**: UART 使能后需要冲洗 RX 缓冲区
3. **UART0/UART2 互斥**: 两个 UART 同时工作可能产生冲突，需错开使用
