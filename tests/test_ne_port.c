/**
 * @file test_ne_port.c
 * @brief Нативные тесты транспорта NeModbus (ne_port.c) на заглушке HAL.
 *
 * Главное, что проверяется, — накопительный буфер: кадр приходит по одному
 * байту (как на железе от прерываний USART1), а ne_port_poll() вызывается
 * между байтами. Раньше в этом месте пакет терялся.
 *
 * Запуск: ./tests/run_tests.sh
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"
#include "ne_port.h"
#include "ne_protocol.h"
#include "hal_stub.h"

static int failures;
static int checks;

#define CHECK(cond)                                                       \
    do {                                                                  \
        checks++;                                                         \
        if (!(cond)) {                                                    \
            failures++;                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        checks++;                                                         \
        if ((a) != (b)) {                                                 \
            failures++;                                                   \
            printf("FAIL %s:%d: %s == %s (%ld != %ld)\n", __FILE__,       \
                   __LINE__, #a, #b, (long)(a), (long)(b));               \
        }                                                                 \
    } while (0)

static UART_HandleTypeDef ne_uart;

static int rx_calls;
static ne_cmd_t rx_cmd[8];
static float rx_float;

static void on_packet(const ne_packet_t *pkt)
{
    if (rx_calls < (int)(sizeof(rx_cmd) / sizeof(rx_cmd[0]))) {
        rx_cmd[rx_calls] = pkt->cmd;
    }
    if ((pkt->cmd == NE_CMD_SET_TEST_SIGNAL_POSITION_VALUE) &&
        (pkt->data != NULL) && (pkt->data_len == 4)) {
        rx_float = ne_get_f32(pkt->data);
    }
    rx_calls++;
}

/* Небольшой помощник: подать байты, опрашивая транспорт после каждого. */
static void feed_poll_each(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        stub_feed_byte(buf[i]);
        ne_port_poll();
    }
}

static void reset_transport(void)
{
    memset(&ne_uart, 0, sizeof(ne_uart));
    ne_uart.Instance = USART1;
    rx_calls = 0;
    rx_float = 0.0f;
    memset(rx_cmd, 0, sizeof(rx_cmd));
    ne_port_init(&ne_uart, on_packet);
    ne_port_poll();          /* сбросить всё, что осталось от прошлого теста */
    rx_calls = 0;
}

/* Кадр по байту с опросом между байтами должен собраться целиком. */
static void test_byte_by_byte(void)
{
    uint8_t frame[64];
    int len;

    reset_transport();
    len = ne_build_packet(frame, sizeof(frame), NE_CMD_GET_DIAG, NULL, 0);
    CHECK(len > 0);

    feed_poll_each(frame, (size_t)len);

    CHECK_EQ(rx_calls, 1);
    CHECK_EQ(rx_cmd[0], NE_CMD_GET_DIAG);
}

/* Два кадра в потоке, разорванные на куски, — два вызова обработчика. */
static void test_two_frames_split(void)
{
    uint8_t frame_a[64];
    uint8_t frame_b[64];
    uint8_t data[4];
    int len_a;
    int len_b;

    reset_transport();
    ne_put_f32(data, 12.5f);
    len_a = ne_build_packet(frame_a, sizeof(frame_a),
                            NE_CMD_SET_TEST_SIGNAL_POSITION_VALUE, data, 4);
    len_b = ne_build_packet(frame_b, sizeof(frame_b), NE_CMD_GET_HW_CMD, NULL, 0);
    CHECK(len_a > 0);
    CHECK(len_b > 0);

    feed_poll_each(frame_a, 6);                  /* первый кадр наполовину */
    CHECK_EQ(rx_calls, 0);
    feed_poll_each(frame_a + 6, (size_t)len_a - 6);
    CHECK_EQ(rx_calls, 1);
    CHECK(rx_float == 12.5f);

    feed_poll_each(frame_b, (size_t)len_b);      /* второй кадр целиком */
    CHECK_EQ(rx_calls, 2);
    CHECK_EQ(rx_cmd[1], NE_CMD_GET_HW_CMD);
}

/* Мусор перед кадром не должен мешать: ресинхронизация по стартовому слову. */
static void test_garbage_prefix(void)
{
    uint8_t buf[128];
    const uint8_t junk[8] = {0x11, 0x22, 0x33, 0x44, 0x5A, 0x00, 0x7E, 0xA4};
    int len;

    reset_transport();
    memcpy(buf, junk, sizeof(junk));
    len = ne_build_packet(buf + sizeof(junk), sizeof(buf) - sizeof(junk),
                          NE_CMD_GET_MODE, NULL, 0);
    CHECK(len > 0);

    feed_poll_each(buf, sizeof(junk) + (size_t)len);
    CHECK_EQ(rx_calls, 1);
    CHECK_EQ(rx_cmd[0], NE_CMD_GET_MODE);
}

/* Битый кадр отбрасывается, следующий за ним валидный — принимается. */
static void test_bad_crc_then_valid(void)
{
    uint8_t bad[64];
    uint8_t good[64];
    int len_bad;
    int len_good;

    reset_transport();
    len_bad = ne_build_packet(bad, sizeof(bad), NE_CMD_GET_DIAG, NULL, 0);
    len_good = ne_build_packet(good, sizeof(good), NE_CMD_GET_HW_CMD, NULL, 0);
    CHECK(len_bad > 0);
    CHECK(len_good > 0);
    bad[len_bad - 1] ^= 0xFFu;

    feed_poll_each(bad, (size_t)len_bad);
    CHECK_EQ(rx_calls, 0);

    feed_poll_each(good, (size_t)len_good);
    CHECK_EQ(rx_calls, 1);
    CHECK_EQ(rx_cmd[0], NE_CMD_GET_HW_CMD);
}

/* Огрызок кадра (оборванная отправка) не съедает следующий валидный кадр. */
static void test_truncated_then_valid(void)
{
    uint8_t cut[64];
    uint8_t good[64];
    int len_cut;
    int len_good;

    reset_transport();
    len_cut = ne_build_packet(cut, sizeof(cut), NE_CMD_GET_DIAG, NULL, 0);
    len_good = ne_build_packet(good, sizeof(good), NE_CMD_GET_MODE, NULL, 0);
    CHECK(len_cut > 0);
    CHECK(len_good > 0);

    feed_poll_each(cut, 6);                      /* только начало кадра */
    CHECK_EQ(rx_calls, 0);
    feed_poll_each(good, (size_t)len_good);      /* затем целый кадр */

    CHECK_EQ(rx_calls, 1);
    CHECK_EQ(rx_cmd[0], NE_CMD_GET_MODE);
}

/* Кадр, обещающий много данных, но пришедший огрызком, блокирует разбор
   до таймаута простоя; после сброса следующий кадр принимается. */
static void test_stuck_fragment_times_out(void)
{
    uint8_t cut[256];
    uint8_t good[64];
    uint8_t data[NE_MAX_DATA_LEN];
    int len_cut;
    int len_good;

    reset_transport();
    memset(data, 0x5A, sizeof(data));
    len_cut = ne_build_packet(cut, sizeof(cut), NE_CMD_SET_MACHINE_PARAMS,
                              data, NE_MAX_DATA_LEN);
    len_good = ne_build_packet(good, sizeof(good), NE_CMD_GET_DIAG, NULL, 0);
    CHECK(len_cut == (int)(NE_MIN_PACKET_SIZE + NE_MAX_DATA_LEN));
    CHECK(len_good > 0);

    feed_poll_each(cut, NE_HEAD_SIZE);        /* только заголовок, data_len = 128 */
    CHECK_EQ(rx_calls, 0);

    /* Валидный кадр сразу за огрызком: разбор ждёт оставшиеся 132 байта. */
    feed_poll_each(good, (size_t)len_good);
    CHECK_EQ(rx_calls, 0);

    /* Простой больше таймаута — огрызок отброшен. */
    stub_advance_ms(NE_PORT_RX_IDLE_TIMEOUT_MS + 1U);
    ne_port_poll();
    CHECK(ne_port_rx_timeout_count() > 0U);
    CHECK_EQ(rx_calls, 0);

    /* Повторённый запрос после сброса проходит без проблем. */
    feed_poll_each(good, (size_t)len_good);
    CHECK_EQ(rx_calls, 1);
    CHECK_EQ(rx_cmd[0], NE_CMD_GET_DIAG);
}

/* Разность тиков беззнаковая: переполнение HAL_GetTick не выглядит простым. */
static void test_stuck_fragment_across_tick_wrap(void)
{
    uint8_t cut[256];
    uint8_t good[64];
    uint8_t data[NE_MAX_DATA_LEN];
    uint32_t before;
    int len_cut;
    int len_good;

    reset_transport();
    before = ne_port_rx_timeout_count();
    memset(data, 0x5A, sizeof(data));
    len_cut = ne_build_packet(cut, sizeof(cut), NE_CMD_SET_MACHINE_PARAMS,
                              data, NE_MAX_DATA_LEN);
    len_good = ne_build_packet(good, sizeof(good), NE_CMD_GET_DIAG, NULL, 0);
    CHECK(len_cut > 0);
    CHECK(len_good > 0);

    stub_set_ms(0xFFFFFFE0u);
    feed_poll_each(cut, NE_HEAD_SIZE);
    CHECK_EQ(rx_calls, 0);

    /* 64 мс через ноль счётчика: меньше таймаута, сброса быть не должно. */
    stub_advance_ms(64U);
    ne_port_poll();
    CHECK_EQ(ne_port_rx_timeout_count(), before);

    /* Ещё 100 мс после переполнения: сброс срабатывает. */
    stub_advance_ms(NE_PORT_RX_IDLE_TIMEOUT_MS);
    ne_port_poll();
    CHECK_EQ(ne_port_rx_timeout_count(), before + 1U);

    feed_poll_each(good, (size_t)len_good);
    CHECK_EQ(rx_calls, 1);
}

/* Счётчик CRC-ошибок реальный, а не заглушка. */
static void test_crc_counter(void)
{
    uint8_t bad[64];
    int len_bad;

    reset_transport();
    len_bad = ne_build_packet(bad, sizeof(bad), NE_CMD_GET_DIAG, NULL, 0);
    CHECK(len_bad > 0);
    bad[len_bad - 1] ^= 0xFFu;

    feed_poll_each(bad, (size_t)len_bad);
    CHECK_EQ(rx_calls, 0);
    CHECK_EQ(ne_port_crc_error_count(), 1U);
}

/* Чужая версия и слишком большой data_len считаются отдельно от CRC. */
static void test_frame_error_counter(void)
{
    uint8_t bad[64];
    uint8_t huge[64];
    uint32_t crc_before;
    int len_bad;

    reset_transport();
    crc_before = ne_port_crc_error_count();
    len_bad = ne_build_packet(bad, sizeof(bad), NE_CMD_GET_DIAG, NULL, 0);
    CHECK(len_bad > 0);
    memcpy(huge, bad, (size_t)len_bad);

    bad[3] = 0x02u;                       /* версия 0x0002 вместо 0x0001 */
    feed_poll_each(bad, (size_t)len_bad);
    CHECK_EQ(ne_port_frame_error_count(), 1U);

    /* Остаток отклонённого кадра может дать повторный счёт — чистим его. */
    stub_advance_ms(NE_PORT_RX_IDLE_TIMEOUT_MS + 1U);
    ne_port_poll();

    huge[8] = 0xFFu;                      /* data_len = 0xFFFF > 128 */
    huge[9] = 0xFFu;
    feed_poll_each(huge, (size_t)len_bad);
    CHECK_EQ(ne_port_frame_error_count(), 2U);

    CHECK_EQ(ne_port_crc_error_count(), crc_before);
    CHECK_EQ(rx_calls, 0);
}

/* Переполнение кольца приёма считается и не роняет транспорт. */
static void test_rx_overflow_counter(void)
{
    reset_transport();
    for (int i = 0; i < 600; i++) {
        stub_feed_byte(0x11);                    /* мусор без стартового слова */
    }
    CHECK(ne_port_rx_overflow_count() > 0U);

    /* После опроса мусор выброшен, счётчик не мешает приёму кадра. */
    ne_port_poll();
    uint8_t frame[64];
    int len = ne_build_packet(frame, sizeof(frame), NE_CMD_GET_DIAG, NULL, 0);
    CHECK(len > 0);
    feed_poll_each(frame, (size_t)len);
    CHECK_EQ(rx_calls, 1);
}

/* Передача: пакет уходит целиком, занятый передатчик даёт NE_ERR_NO_SPACE. */
static void test_send_path(void)
{
    uint8_t expected[64];
    uint8_t payload[4];
    int len;

    reset_transport();
    ne_put_f32(payload, -1.5f);
    len = ne_build_packet(expected, sizeof(expected),
                          NE_CMD_GET_TEST_SIGNAL_POSITION_VALUE, payload, 4);
    CHECK(len > 0);

    CHECK_EQ(ne_port_send(NE_CMD_GET_TEST_SIGNAL_POSITION_VALUE, payload, 4),
             len);
    CHECK_EQ(stub_tx_len(), (uint16_t)len);
    CHECK(memcmp(stub_tx(), expected, (size_t)len) == 0);

    /* Передатчик занят — второй ответ не ставится в очередь. */
    CHECK_EQ(ne_port_send(NE_CMD_GET_HW_CMD, payload, 4), NE_ERR_NO_SPACE);

    stub_tx_complete();
    CHECK(ne_port_send(NE_CMD_GET_HW_CMD, payload, 4) > 0);
}

int main(void)
{
    test_byte_by_byte();
    test_two_frames_split();
    test_garbage_prefix();
    test_bad_crc_then_valid();
    test_truncated_then_valid();
    test_stuck_fragment_times_out();
    test_stuck_fragment_across_tick_wrap();
    test_crc_counter();
    test_frame_error_counter();
    test_rx_overflow_counter();
    test_send_path();

    if (failures != 0) {
        printf("\n%d из %d проверок провалено\n", failures, checks);
        return 1;
    }
    printf("OK: %d проверок\n", checks);
    return 0;
}
