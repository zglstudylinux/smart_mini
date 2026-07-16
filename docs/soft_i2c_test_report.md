# BT892X 软件 GPIO 模拟 I2C 测试报告

> **日期**: 2026-07-15  
> **芯片**: BT892X  
> **测试工具**: 逻辑分析仪  
> **测试结果**: ✅ 通过（解码器显示 0xA0 + 0xA5, NAK 正确）

---

## 1. 测试目的

用纯 GPIO 软件模拟 I2C 主机时序（Bit-Bang），验证 GPIO 翻转速度和控制精度。

---

## 2. 测试原理

### 2.1 I2C 协议（标准模式 ~100KHz）

- **SCL**: 时钟线，主机控制
- **SDA**: 数据线，开漏（需上拉电阻）
- **START**: SDA↓ 且 SCL=H
- **STOP**: SDA↑ 且 SCL=H
- **数据**: 8bit MSB first + 1bit ACK
- **SCL 半周期**: delay_us(5) ≈ 5µs → ~100KHz

### 2.2 引脚

| 信号 | 引脚 |
|------|------|
| SCL | PE6 |
| SDA | PE7 |

---

## 3. 测试内容

发送 I2C 写事务：`START + 0x50(W) + 0xA5 + STOP`

无实际从设备，ACK 位期望为 NAK（SDA 保持高）。

---

## 4. 测试结果

| 项目 | 预期 | 实测 | 通过 |
|------|------|------|:--:|
| START 条件 | SDA↓, SCL=H | 波形正常 | ✅ |
| 地址字节 | 0xA0 (0x50+W) | 解码器显示 0xA0 | ✅ |
| 地址 ACK | NAK (1) | SDA=H | ✅ |
| 数据字节 | 0xA5 | 解码器显示 0xA5 | ✅ |
| 数据 ACK | NAK (1) | SDA=H | ✅ |
| STOP 条件 | SDA↑, SCL=H | 波形正常 | ✅ |

---

## 5. 关键代码

```c
// I2C START
sda_out(1); SCL=H; delay();
sda_out(0); delay();              // SDA↓ → START

// 发送字节 (MSB first)
for (int i = 7; i >= 0; i--) {
    sda_out((data >> i) & 1);     // 设数据位
    SCL=H; delay();               // 锁存
    SCL=L; delay();
}
sda_in();                         // 释放SDA读ACK
SCL=H; delay(); ack = sda_read();
SCL=L; delay();

// I2C STOP
sda_out(0); SCL=H; delay();
sda_out(1); delay();              // SDA↑ → STOP
```

---

## 6. 结论

✅ **软件 I2C 主机验证通过**。GPIO Bit-Bang 可正确产生 START/STOP/数据/ACK 完整时序。SDA 开漏模式（输出→输入切换）配合 10K 内部上拉工作正常。
