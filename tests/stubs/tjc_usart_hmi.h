#ifndef TJC_USART_HMI_STUB_H
#define TJC_USART_HMI_STUB_H

#include <stdint.h>

#include "stm32f1xx_hal.h"

#define TJC_UART_INS USART3

extern uint8_t RxBuffer[1];

void tjc_uart_rx_isr(uint8_t byte);
void tjc_uart_tx_complete_isr(void);
void tjc_uart_error_isr(void);

#endif /* TJC_USART_HMI_STUB_H */
