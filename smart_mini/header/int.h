#ifndef _INTERRUPTS_H
#define _INTERRUPTS_H

#define IRQ_BT_VECTOR                   1
#define IRQ_SW_VECTOR                   2
#define IRQ_TMR0_VECTOR                 3
#define IRQ_TMR1_VECTOR                 4
#define IRQ_TMR2_VECTOR                 5
#define IRQ_IRRX_VECTOR                 6
#define IRQ_USB_VECTOR                  7
#define IRQ_SD_VECTOR                   8
#define IRQ_AUBUF_VECTOR                9
#define IRQ_SDADC_VECTOR                10
#define IRQ_AUDEC_VECTOR                11      //mp3, sbc interrupt
#define IRQ_CVSD_VECTOR                 12
#define IRQ_PIANO_VECTOR                13
#define IRQ_UART_VECTOR                 14
#define IRQ_HUART_VECTOR                15
#define IRQ_TMR3_VECTOR                 16
#define IRQ_TMR4_VECTOR                 17
#define IRQ_TMR5_VECTOR                 18
#define IRQ_SRC_VECTOR                  19
#define IRQ_SPI_VECTOR                  20
#define IRQ_KEY_VECTOR                  21
#define IRQ_BTMDM_VECTOR                22
#define IRQ_FMDET_VECTOR                23      //FMDET, TK
#define IRQ_AUDMA_VECTOR                24
#define IRQ_GPDMA_VECTOR                25
#define IRQ_PORT_VECTOR                 26
#define IRQ_I2S_VECTOR                  27
#define IRQ_SARADC_VECTOR               28
#define IRQ_RTC_VECTOR                  29
#define IRQ_TOTAL_NUM                   30      //max irq

extern void *tbl_irq_vector[IRQ_TOTAL_NUM];
typedef  void (*isr_t)(void);
isr_t register_isr(int vector, isr_t isr);

void soft_interrupt_init(void);
void timer0_init(void);
void timer1_init(void);
void uart2_init(void);
void src_interrupt_init(void);

#endif // _INTERRUPTS_H
