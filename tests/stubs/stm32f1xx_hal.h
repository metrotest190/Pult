/**
 * @file stm32f1xx_hal.h
 * @brief Минимальная заглушка HAL для нативных тестов ne_port.c.
 *
 * Даёт только то, что нужно транспортному слою: типы UART, три функции
 * передачи/приёма и псевдо-критические секции. Никакого железа и CMSIS.
 */
#ifndef STM32F1XX_HAL_STUB_H
#define STM32F1XX_HAL_STUB_H

#include <stdint.h>

typedef enum {
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

typedef struct {
    uint32_t dummy;
} USART_TypeDef;

extern USART_TypeDef stub_usart1;
extern USART_TypeDef stub_usart3;

#define USART1 (&stub_usart1)
#define USART3 (&stub_usart3)

typedef struct {
    USART_TypeDef *Instance;
} UART_HandleTypeDef;

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *pData,
                                      uint16_t Size);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart,
                                       const uint8_t *pData, uint16_t Size);
HAL_StatusTypeDef HAL_UART_AbortReceive_IT(UART_HandleTypeDef *huart);

/* Время для таймаута приёма: подменяется из теста. */
uint32_t HAL_GetTick(void);

/* Тесты однопоточные, поэтому критическая секция — пустышка. */
#define __disable_irq() ((void)0)
#define __enable_irq()  ((void)0)

static inline uint32_t __get_PRIMASK(void)
{
    return 0U;
}

static inline void __set_PRIMASK(uint32_t primask)
{
    (void)primask;
}

/* Имена критических секций из ne_port.c совпадают с Win32 API, а тесты
   линкуются с kernel32 — на время сборки тестов переименовываем.
   В прошивке имена остаются прежними (EnterCriticalSection/ExitCriticalSection). */
#define EnterCriticalSection ne_test_enter_critical_section
#define ExitCriticalSection  ne_test_exit_critical_section

/* Колбэки реализует сам ne_port.c — здесь только объявления. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

#endif /* STM32F1XX_HAL_STUB_H */
