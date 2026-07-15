#include "include.h"

void *tbl_irq_vector[IRQ_TOTAL_NUM] AT(.buf.irq_tbl);

AT(.com_text.isr)
isr_t register_isr(int vector, isr_t isr)
{
	isr_t old = tbl_irq_vector[vector];
	tbl_irq_vector[vector] = isr;
	return old;
}

AT(.com_text.isr)
void cpu_low_irq_comm(void)
{
	void (*pfnct)(void);
	for (int i = 0; i < IRQ_TOTAL_NUM; i++) {
        if (PICPND & BIT(i)) {
            pfnct = tbl_irq_vector[i];
            if (pfnct) {
                pfnct();				/* call ISR */
            }
        }
	}
}

//timer0 1ms interrupt
AT(.com_text.isr)
void timer0_isr(void)
{
    static uint tick_cnt = 0;
    TMR0CPND = BIT(9);              //Clear Pending
    tick_cnt++;

    if ((tick_cnt % 5) == 0) {      //5ms

    }

    if ((tick_cnt % 1000) == 0) {   //1s
        tick_cnt = 0;
    }
}

void timer0_init(void)
{
    register_isr(IRQ_TMR0_VECTOR, timer0_isr);
	TMR0CON =  BIT(7); //TIE
	TMR0CNT = 0;
	TMR0PR  = 1000 - 1;         //1ms interrupt
	TMR0CON |= BIT(2) | BIT(0); // EN
	PICPR &= ~BIT(IRQ_TMR0_VECTOR);
	PICEN |= BIT(IRQ_TMR0_VECTOR);
}


