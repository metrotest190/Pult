/*
 * FreeModbus timer port for STM32F103.
 * TIM3 is configured by CubeMX for a 50 us update period.
 */

#include "port.h"
#include "mb.h"
#include "mbport.h"
#include "main.h"
#include "stm32f1xx_hal.h"

extern TIM_HandleTypeDef *modbusTimer;

/* Shared between the foreground port functions and TIM3 IRQ. */
static volatile uint16_t timerPeriod;
static volatile uint16_t timerCounter;

static void prvvTIMERExpiredISR(void)
{
    if (pxMBPortCBTimerExpired != NULL) {
        (void)pxMBPortCBTimerExpired();
    }
}

BOOL xMBPortTimersInit(USHORT usTimeOut50us)
{
    if ((modbusTimer == NULL) || (usTimeOut50us == 0U)) {
        return FALSE;
    }

    ENTER_CRITICAL_SECTION();
    timerPeriod = usTimeOut50us;
    timerCounter = 0U;
    EXIT_CRITICAL_SECTION();

    return TRUE;
}

void vMBPortTimersEnable(void)
{
    HAL_StatusTypeDef status;

    if (modbusTimer == NULL) {
        Error_Handler();
        return;
    }

    /* Re-arming an RTU timeout must start from a known timer state. Clear both
       peripheral and NVIC pending state before enabling the update interrupt. */
    ENTER_CRITICAL_SECTION();
    status = HAL_TIM_Base_Stop_IT(modbusTimer);
    timerCounter = 0U;
    __HAL_TIM_SET_COUNTER(modbusTimer, 0U);
    __HAL_TIM_CLEAR_FLAG(modbusTimer, TIM_FLAG_UPDATE);
    if (modbusTimer->Instance == TIM3) {
        NVIC_ClearPendingIRQ(TIM3_IRQn);
    }
    EXIT_CRITICAL_SECTION();

    if (status != HAL_OK) {
        Error_Handler();
    }

    if (HAL_TIM_Base_Start_IT(modbusTimer) != HAL_OK) {
        Error_Handler();
    }
}

void vMBPortTimersDisable(void)
{
    if (modbusTimer == NULL) {
        return;
    }

    if (HAL_TIM_Base_Stop_IT(modbusTimer) != HAL_OK) {
        Error_Handler();
    }

    ENTER_CRITICAL_SECTION();
    timerCounter = 0U;
    __HAL_TIM_CLEAR_FLAG(modbusTimer, TIM_FLAG_UPDATE);
    if (modbusTimer->Instance == TIM3) {
        NVIC_ClearPendingIRQ(TIM3_IRQn);
    }
    EXIT_CRITICAL_SECTION();
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if ((modbusTimer == NULL) || (htim == NULL) ||
        (htim->Instance != modbusTimer->Instance)) {
        return;
    }

    timerCounter++;

    if (timerCounter == timerPeriod) {
        /* The RTU state machine normally stops the timer through
           vMBPortTimersDisable() during this callback. Equality prevents a
           repeat expiry if a stop operation failed. */
        prvvTIMERExpiredISR();
    }
}
