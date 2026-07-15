# smart_mini — BT892X 外设学习测试工程

本工程基于中科蓝讯 **BT892X**（32 位 RISC-V SoC），通过**寄存器级编程**逐步测试五大外设：GPIO、Timer、UART、SPI、I2C。

## 工程结构

```
minimax/
├── docs/                  # 参考手册 + 测试报告
│   ├── BT892X_UserManual_Driver.md   # 寄存器驱动手册
│   ├── bt892x_pinfunction.md         # 引脚功能定义
│   ├── bt892x_pinfunction-20230830.xlsx
│   ├── TWS  DEV V2.2.pdf
│   ├── bt892x_usermanual.pdf
│   ├── plan.md                        # 整体学习计划
│   ├── test_gpio.md                   # GPIO 测试报告
│   ├── test_timer.md                  # Timer 测试报告
│   ├── test_uart.md                   # UART 测试报告
│   ├── test_spi.md                    # SPI 测试报告
│   └── test_i2c.md                    # I2C 测试报告
└── smart_mini/            # CodeBlocks 工程根
    ├── app.cbp           # CodeBlocks 工程文件
    ├── main.c            # 主入口
    ├── interrupt.c       # 中断处理
    ├── reset.S           # 启动文件
    ├── ram.ld            # 链接脚本
    ├── header/           # 头文件（sfr.h 寄存器定义、include.h 等）
    ├── test/             # 测试代码（5 个外设各一文件）
    └── Output/           # 构建产物（gitignore 排除）
```

## 构建工具链

```
CodeBlocks IDE
  ↓ Build
riscv32-gcc 编译 .c/.S → .o
  ↓ Link
riscv32-ld -Tram.ld → app.rv32 (ELF)
  ↓ postbuild.bat
riscv32-elf-objcopy → app.bin
xmaker → app.xm → app.dcf (可烧录固件)
  ↓ Downloader 工具
写入 BT892X 芯片
```

## 开发流程

1. 用 **CodeBlocks** 打开 `smart_mini/app.cbp`
2. 编辑 `smart_mini/test/test_xxx.c` 或 `smart_mini/main.c`
3. **Build** → 生成 `Output/bin/app.dcf`
4. 用 **Downloader** 工具把 `app.dcf` 烧录到开发板
5. 通过 **UART0 (PB3)** 串口查看 `printf` 输出
6. 用 **万用表** / **逻辑分析仪** 验证引脚电平和时序

## 测试模块

每个测试独立文件，入口函数 `void test_xxx_run(void)`：

| 模块 | 文件 | 引脚 | 状态 |
|---|---|---|---|
| GPIO | `test/test_gpio.c` | PE4 | ⬜ 待测试 |
| Timer | `test/test_timer.c` | TMR1（内部） | ⬜ 待测试 |
| UART1 | `test/test_uart.c` | PA3(TX) / PA4(RX) | ⬜ 待测试 |
| SPI | `test/test_spi.c` | PE5/PE6/PF0/PF1 (bit-bang) | ⬜ 待测试 |
| I2C | `test/test_i2c.c` | PE6/PE7 (bit-bang) | ⬜ 待测试 |

详细计划见 [docs/plan.md](docs/plan.md)。

## 关键约束

- **PB3 / PB4 / PB5 / PG1~PG5** 是 USB 下载 / 程序存储 / 唤醒源，**严禁任何测试代码触碰**
- **TMR0 / TMR2** 已被 main.c 占用（1ms 中断 / 1us tick），**Timer 测试只能用 TMR1**
- 所有测试代码基于 `header/sfr.h` 中的寄存器宏直接操作
- 测试设备：万用表（量电压）、逻辑分析仪（抓波形）

## Git 工作流

- 主分支：`smart_mini_minimax`
- 远程：`git@github.com:zglstudylinux/smart_mini.git`
- 提交节奏：**每个测试 = 1 次 commit + 1 次 push**
- 提交信息规范：`test(xxx): add xxx driver test and report`