/**
 * @file hal_stub.c
 * @brief Реализация заглушки HAL: один байт приёма, буфер передачи.
 */
#include <stdio.h>
#include <stdlib.h>

#include "stm32f1xx_hal.h"
#include "tjc_usart_hmi.h"
#include "hal_stub.h"

USART_TypeDef stub_usart1;
USART_TypeDef stub_usart3;
uint8_t RxBuffer[1];

/* Куда HAL складывает принятый байт и по какому handle дёргать колбэк. */
static uint8_t *rx_target;
static UART_HandleTypeDef *rx_handle;

static uint8_t tx_buf[512];
static uint16_t tx_len;

static unsigned tjc_rx_calls;
static unsigned tjc_error_calls;

static uint32_t tick_ms;

uint32_t HAL_GetTick(void)
{
    return tick_ms;
}

void stub_advance_ms(uint32_t ms)
{
    tick_ms += ms;
}

void stub_set_ms(uint32_t ms)
{
    tick_ms = ms;
}

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *pData,
                                      uint16_t Size)
{
    (void)Size;
    rx_handle = huart;
    rx_target = pData;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart,
                                       const uint8_t *pData, uint16_t Size)
{
    (void)huart;
    tx_len = (Size <= sizeof(tx_buf)) ? Size : (uint16_t)sizeof(tx_buf);
    for (uint16_t i = 0; i < tx_len; i++) {
        tx_buf[i] = pData[i];
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_AbortReceive_IT(UART_HandleTypeDef *huart)
{
    (void)huart;
    return HAL_OK;
}

/* В этих тестах работает только USART1, поэтому TJC-хуки только считаются. */
void tjc_uart_rx_isr(uint8_t byte)
{
    (void)byte;
    tjc_rx_calls++;
}

void tjc_uart_tx_complete_isr(void)
{
}

void tjc_uart_error_isr(void)
{
    tjc_error_calls++;
}

void Error_Handler(void)
{
    printf("FAIL: Error_Handler() вызван\n");
    exit(1);
}

void stub_feed_byte(uint8_t byte)
{
    if (rx_target == NULL) {
        printf("FAIL: приём не взведён (HAL_UART_Receive_IT не вызывался)\n");
        exit(1);
    }
    *rx_target = byte;
    HAL_UART_RxCpltCallback(rx_handle);
}

void stub_feed(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        stub_feed_byte(buf[i]);
    }
}

const uint8_t *stub_tx(void)
{
    return tx_buf;
}

uint16_t stub_tx_len(void)
{
    return tx_len;
}

void stub_tx_complete(void)
{
    HAL_UART_TxCpltCallback(rx_handle);
}

unsigned stub_tjc_rx_calls(void)
{
    return tjc_rx_calls;
}

unsigned stub_tjc_error_calls(void)
{
    return tjc_error_calls;
}
