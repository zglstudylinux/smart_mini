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
│   ├── test_gpio.md                   # GPIO 测试报告 ✅
│   ├── test_timer.md                  # Timer 测试报告 ✅
│   ├── test_uart.md                   # UART 测试报告 ❌ 跳过（PA 引脚不可用）
│   ├── test_spi.md                    # SPI 测试报告 ❌ 跳过（引脚冲突）
│   └── test_i2c.md                    # I2C 测试报告 ✅
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
| GPIO | `test/test_gpio.c` | PE4 | ✅ 通过 (commit a01473b) |
| Timer | `test/test_timer.c` | TMR1（内部） | ✅ 通过 (commit 5a658e3) |
| UART1 | `test/test_uart.c` | PA3/PA4 或 PA6/PA7 | ❌ 跳过 — **PA0-PA7 全部 GPIO 不翻转**（物理问题） |
| SPI | `test/test_spi.c` | 未实现 | ❌ 跳过 — 仅剩可用引脚与 USB / SPI-Flash 冲突 |
| I2C | `test/test_i2c.c` | PE6(SCL) / PE7(SDA)（硬件 IIC） | ✅ 通过 (commit c1b1c60) |

详细计划见 [docs/plan.md](docs/plan.md)。

**跳过说明**：UART1 和 SPI 因引脚受限无法测试。PA 端口 0-7 全部 GPIO 输出无信号（用户实测），导致 UART1 唯一可用引脚（PA3/PA4 或 PA6/PA7）失效。SPI0 仅剩的可用 Group（G1=PG 端口=SPI-Flash，G3=PB3/PB4=USB）都有致命冲突。其他可选的 UART/SPI 资源（如 SPI1、UART2）需要进一步诊断。

测试顺序：GPIO → Timer → I2C（中间跳过 UART1 和 SPI）。

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