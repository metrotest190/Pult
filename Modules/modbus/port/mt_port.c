/*
 * Additional porting data for the FreeModbus STM32 port.
 */

#include "mt_port.h"
#include "main.h"

/* The counter is read/written with PRIMASK already set. volatile makes the
   shared ISR/foreground intent explicit. */
static volatile uint32_t lockCounter;
static uint32_t savedPrimask;

UART_HandleTypeDef *modbusUart;
TIM_HandleTypeDef *modbusTimer;

void EnterCriticalSection(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    if (lockCounter++ == 0U) {
        savedPrimask = primask;
    }
}

void ExitCriticalSection(void)
{
    /* A mismatched exit would otherwise underflow and keep IRQ disabled for
       2^32 calls. Treat it as a programming error. */
    if (lockCounter == 0U) {
        Error_Handler();
    }

    lockCounter--;
    if (lockCounter == 0U) {
        __set_PRIMASK(savedPrimask);
    }
}

void MT_PORT_SetTimerModule(TIM_HandleTypeDef *timer)
{
    modbusTimer = timer;
}

void MT_PORT_SetUartModule(UART_HandleTypeDef *uart)
{
    modbusUart = uart;
}
