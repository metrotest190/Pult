/**
 * @file hal_stub.h
 * @brief Управление заглушкой UART из тестов.
 */
#ifndef HAL_STUB_H
#define HAL_STUB_H

#include <stddef.h>
#include <stdint.h>

/* Подать один байт в приём: имитация HAL_UART_RxCpltCallback(). */
void stub_feed_byte(uint8_t byte);

/* Подать буфер целиком. */
void stub_feed(const uint8_t *buf, size_t len);

/* Что ушло в передатчик с последнего вызова HAL_UART_Transmit_IT(). */
const uint8_t *stub_tx(void);
uint16_t stub_tx_len(void);

/* Имитация завершения передачи (HAL_UART_TxCpltCallback). */
void stub_tx_complete(void);

/* Виртуальное время: сдвинуть HAL_GetTick() вперёд. */
void stub_advance_ms(uint32_t ms);

/* Выставить виртуальное время (проверка переполнения счётчика). */
void stub_set_ms(uint32_t ms);

/* Сколько раз вызывались TJC-хуки (в этих тестах — ноль). */
unsigned stub_tjc_rx_calls(void);
unsigned stub_tjc_error_calls(void);

#endif /* HAL_STUB_H */
