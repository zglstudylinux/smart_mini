#include "include.h"

// 中断向量回调表：每个 IRQ 号对应一个函数指针，放在专用 RAM 段 .buf.irq_tbl
void *tbl_irq_vector[IRQ_TOTAL_NUM] AT(.buf.irq_tbl);

// 注册/替换某个向量号的 ISR，返回旧回调（便于测试结束后恢复）
AT(.com_text.isr)
isr_t register_isr(int vector, isr_t isr)
{
	isr_t old = tbl_irq_vector[vector];
	tbl_irq_vector[vector] = isr;
	return old;
}

// 低优先级中断统一入口：扫描 PICPND 挂起位，命中则调用已注册的 ISR
AT(.com_text.isr)
void cpu_low_irq_comm(void)
{
	void (*pfnct)(void);
	for (int i = 0; i < IRQ_TOTAL_NUM; i++) {
        if (PICPND & BIT(i)) {          // 该向量有中断挂起
            pfnct = tbl_irq_vector[i];
            if (pfnct) {
                pfnct();				/* call ISR */
            }
        }
	}
}

// Timer0 1ms 周期中断服务程序（详见 docs/periph_timer.md）
// 该 ISR 由 cpu_low_irq_comm() 在检测到 PICPND[IRQ_TMR0_VECTOR]=1 时回调
AT(.com_text.isr)
void timer0_isr(void)
{
    static uint tick_cnt = 0;
    // 【必做】写 TMR0CPND[9]=TPCLR 清溢出挂起（手册 §4.2）。
    // Timer0/1/2 的 TPND 在 CON bit9，只能通过 CPND 写 1 清除，
    // 若不清则退出 ISR 后会被立即再次触发（死循环进中断）。
    TMR0CPND = BIT(9);              //Clear Pending (TPCLR)
    tick_cnt++;

    if ((tick_cnt % 5) == 0) {      //5ms 节拍钩子（预留骨架，当前无动作）

    }

    if ((tick_cnt % 1000) == 0) {   //1s 节拍钩子：每 1000 次(=1s)回绕计数
        tick_cnt = 0;
    }
}

// 初始化 Timer0 为 1ms 周期中断（系统节拍）。时钟源 tmr_inc=1MHz（见 main.c 时钟配置）
void timer0_init(void)
{
    register_isr(IRQ_TMR0_VECTOR, timer0_isr);      // 注册向量回调
	TMR0CON =  BIT(7);          // TIE=1：先使能溢出中断（此时尚未启动计数）
	TMR0CNT = 0;                // 计数值清零
	TMR0PR  = 1000 - 1;         // 周期 = PR+1 = 1000 tick = 1ms（tmr_inc 1MHz）
	TMR0CON |= BIT(2) | BIT(0); // INCSEL=01(计数 tmr_inc 上升沿) + TMREN=1(启动)
	PICPR &= ~BIT(IRQ_TMR0_VECTOR);  // 优先级：清 0 = 高优先级
	PICEN |= BIT(IRQ_TMR0_VECTOR);   // 使能 Timer0 中断向量
}


