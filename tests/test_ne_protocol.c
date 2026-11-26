/**
 * @file test_ne_protocol.c
 * @brief Нативные тесты ядра NeModbus (ne_protocol.c).
 *
 * Ядро не зависит от HAL, поэтому собирается и гоняется на ПК:
 *     ./tests/run_tests.sh
 *
 * Проверяется то, на что опирается приём на пульте: CRC, сборка, разбор,
 * ресинхронизация по мусору, отбраковка битых кадров и границы payload.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ne_protocol.h"

static int failures;
static int checks;

#define CHECK(cond)                                                       \
    do {                                                                  \
        checks++;                                                         \
        if (!(cond)) {                                                    \
            failures++;                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        checks++;                                                         \
        if ((a) != (b)) {                                                 \
            failures++;                                                   \
            printf("FAIL %s:%d: %s == %s (%ld != %ld)\n", __FILE__,        \
                   __LINE__, #a, #b, (long)(a), (long)(b));               \
        }                                                                 \
    } while (0)

/* CRC32 как в zlib: контрольный вектор "123456789" -> 0xCBF43926. */
static void test_crc(void)
{
    CHECK_EQ(ne_crc32((const uint8_t *)"123456789", 9), 0xCBF43926u);
    CHECK_EQ(ne_crc32((const uint8_t *)"", 0), 0u);
}

/* Big-endian помощники и float/двойная точность через них. */
static void test_byte_order(void)
{
    uint8_t b[8];

    ne_put_u16(b, 0xA55Au);
    CHECK_EQ(b[0], 0xA5u);
    CHECK_EQ(b[1], 0x5Au);
    CHECK_EQ(ne_get_u16(b), 0xA55Au);

    ne_put_u32(b, 0x01020304u);
    CHECK_EQ(b[0], 0x01u);
    CHECK_EQ(b[3], 0x04u);
    CHECK_EQ(ne_get_u32(b), 0x01020304u);

    ne_put_u64(b, 0x0102030405060708ull);
    CHECK_EQ(ne_get_u64(b), 0x0102030405060708ull);

    ne_put_f32(b, 12.5f);
    CHECK_EQ(b[0], 0x41u);
    CHECK_EQ(b[1], 0x48u);
    CHECK_EQ(ne_get_f32(b), 12.5f);

    ne_put_f64(b, -3.25);
    CHECK(ne_get_f64(b) == -3.25);
}

/* Сборка: длина кадра, поля заголовка, CRC поверх заголовка и данных. */
static void test_build(void)
{
    uint8_t out[256];
    const uint8_t data[4] = {0x41, 0x48, 0x00, 0x00};
    int len = ne_build_packet(out, sizeof(out), NE_CMD_GET_HW_CMD, data, 4);

    CHECK_EQ(len, 18);
    CHECK_EQ(ne_get_u16(out + 0), NE_START_WORD);
    CHECK_EQ(ne_get_u16(out + 2), NE_PROTO_VERSION_1);
    CHECK_EQ(ne_get_u32(out + 4), NE_CMD_GET_HW_CMD);
    CHECK_EQ(ne_get_u16(out + 8), 4);
    CHECK(memcmp(out + 10, data, 4) == 0);
    CHECK_EQ(ne_get_u32(out + 14), ne_crc32(out, 14));

    /* Пустой payload — минимальный кадр 14 байт. */
    CHECK_EQ(ne_build_packet(out, sizeof(out), NE_CMD_GET_MODE, NULL, 0),
             (int)NE_MIN_PACKET_SIZE);

    /* Ограничения: payload > NE_MAX_DATA_LEN и маленький буфер. */
    CHECK_EQ(ne_build_packet(out, sizeof(out), NE_CMD_SET_MODE, NULL,
                             NE_MAX_DATA_LEN + 1u), NE_ERR_TOO_BIG);
    CHECK_EQ(ne_build_packet(out, 10, NE_CMD_SET_MODE, NULL, 4),
             NE_ERR_NO_SPACE);
    CHECK(ne_build_packet(out, NE_MIN_PACKET_SIZE, NE_CMD_SET_MODE, NULL, 0) ==
          (int)NE_MIN_PACKET_SIZE);
}

/* Разбор собранного кадра: поля, consumed, payload. */
static void test_parse_roundtrip(void)
{
    uint8_t pkt_buf[256];
    ne_packet_t pkt;
    size_t consumed = 0;
    const uint8_t data[4] = {0x41, 0x48, 0x00, 0x00};
    int len = ne_build_packet(pkt_buf, sizeof(pkt_buf),
                              NE_CMD_SET_TEST_SIGNAL_POSITION_VALUE, data, 4);

    CHECK(len > 0);
    memset(&pkt, 0, sizeof(pkt));
    CHECK_EQ(ne_parse_packet(pkt_buf, (size_t)len, &pkt, &consumed), NE_OK);
    CHECK_EQ(pkt.version, NE_PROTO_VERSION_1);
    CHECK_EQ(pkt.cmd, NE_CMD_SET_TEST_SIGNAL_POSITION_VALUE);
    CHECK_EQ(pkt.data_len, 4);
    CHECK_EQ(consumed, (size_t)len);
    CHECK(pkt.data != NULL);
    CHECK(memcmp(pkt.data, data, 4) == 0);
    CHECK_EQ(ne_get_f32(pkt.data), 12.5f);
}

/* Мусор перед кадром: парсер обязан найти стартовое слово и вернуть смещение. */
static void test_resync_after_garbage(void)
{
    uint8_t buf[256];
    ne_packet_t pkt;
    size_t consumed = 0;
    const uint8_t junk[7] = {0x11, 0x22, 0x33, 0x5A, 0x00, 0x7E, 0xA4};
    const uint8_t a5_only[3] = {0x00, 0xA5, 0x01};   /* 0xA5 без 0x5A */
    int len;

    memcpy(buf, junk, sizeof(junk));
    memcpy(buf + 3, a5_only, sizeof(a5_only));       /* мусор с одиночным 0xA5 */
    len = ne_build_packet(buf + sizeof(junk) + sizeof(a5_only),
                          sizeof(buf) - sizeof(junk) - sizeof(a5_only),
                          NE_CMD_GET_DIAG, NULL, 0);
    CHECK(len > 0);

    const size_t total = sizeof(junk) + sizeof(a5_only) + (size_t)len;
    CHECK_EQ(ne_parse_packet(buf, total, &pkt, &consumed), NE_OK);
    CHECK_EQ(pkt.cmd, NE_CMD_GET_DIAG);
    CHECK_EQ(consumed, total);
    CHECK_EQ(ne_find_start(buf, total), (int)sizeof(junk) + (int)sizeof(a5_only));
}

/* Битые кадры: CRC, версия, слишком длинный payload. */
static void test_rejects(void)
{
    uint8_t buf[256];
    ne_packet_t pkt;
    size_t consumed = 0;
    int len = ne_build_packet(buf, sizeof(buf), NE_CMD_GET_DIAG, NULL, 0);
    CHECK(len > 0);

    /* Испорченный CRC. */
    buf[len - 1] ^= 0xFFu;
    CHECK_EQ(ne_parse_packet(buf, (size_t)len, &pkt, &consumed), NE_ERR_CRC);

    /* Чужая версия — но с верным CRC, иначе тест поймает не ту ошибку. */
    len = ne_build_packet(buf, sizeof(buf), NE_CMD_GET_DIAG, NULL, 0);
    ne_put_u16(buf + 2, NE_PROTO_VERSION_1 + 1u);
    ne_put_u32(buf + len - 4, ne_crc32(buf, (size_t)len - 4));
    CHECK_EQ(ne_parse_packet(buf, (size_t)len, &pkt, &consumed),
             NE_ERR_VERSION);

    /* data_len больше NE_MAX_DATA_LEN. */
    len = ne_build_packet(buf, sizeof(buf), NE_CMD_GET_DIAG, NULL, 0);
    ne_put_u16(buf + 8, NE_MAX_DATA_LEN + 1u);
    CHECK_EQ(ne_parse_packet(buf, (size_t)len, &pkt, &consumed),
             NE_ERR_TOO_BIG);
}

/* Неполный кадр и отсутствие стартового слова. */
static void test_incomplete(void)
{
    uint8_t buf[256];
    ne_packet_t pkt;
    size_t consumed = 0;
    uint8_t data[16];
    int len;

    memset(data, 0x5A, sizeof(data));
    len = ne_build_packet(buf, sizeof(buf), NE_CMD_SET_MODE, data, 16);
    CHECK(len > 0);

    /* Меньше заголовка. */
    CHECK_EQ(ne_parse_packet(buf, 5, &pkt, &consumed), NE_ERR_INCOMPLETE);
    /* Заголовок целиком, данные не все. */
    CHECK_EQ(ne_parse_packet(buf, (size_t)len - 2u, &pkt, &consumed),
             NE_ERR_INCOMPLETE);
    /* Заголовок есть, но и байта данных не пришло. */
    CHECK_EQ(ne_parse_packet(buf, NE_HEAD_SIZE, &pkt, &consumed),
             NE_ERR_INCOMPLETE);

    /* Мусор без стартового слова. */
    memset(buf, 0x11, 32);
    CHECK_EQ(ne_parse_packet(buf, 32, &pkt, &consumed), NE_ERR_NO_START);
    CHECK_EQ(ne_find_start(buf, 32), -1);
    CHECK_EQ(ne_find_start((const uint8_t *)"\xA5", 1), -1);
}

/* Два кадра подряд: разбор по consumed не должен зацикливаться. */
static void test_two_packets(void)
{
    uint8_t buf[256];
    ne_packet_t pkt;
    size_t consumed = 0;
    int len1 = ne_build_packet(buf, sizeof(buf), NE_CMD_GET_DIAG, NULL, 0);
    int len2 = ne_build_packet(buf + len1, sizeof(buf) - (size_t)len1,
                               NE_CMD_GET_MODE, NULL, 0);

    CHECK(len1 > 0);
    CHECK(len2 > 0);

    CHECK_EQ(ne_parse_packet(buf, (size_t)(len1 + len2), &pkt, &consumed), NE_OK);
    CHECK_EQ(pkt.cmd, NE_CMD_GET_DIAG);
    CHECK_EQ(consumed, (size_t)len1);

    CHECK_EQ(ne_parse_packet(buf + consumed, (size_t)len2, &pkt, &consumed),
             NE_OK);
    CHECK_EQ(pkt.cmd, NE_CMD_GET_MODE);
}

/* Максимальный payload 128 байт должен проходить без обрезки. */
static void test_max_payload(void)
{
    uint8_t buf[256];
    uint8_t data[NE_MAX_DATA_LEN];
    ne_packet_t pkt;
    size_t consumed = 0;
    int len;

    for (uint16_t i = 0; i < NE_MAX_DATA_LEN; i++) {
        data[i] = (uint8_t)i;
    }
    len = ne_build_packet(buf, sizeof(buf), NE_CMD_SET_MACHINE_PARAMS, data,
                          NE_MAX_DATA_LEN);
    CHECK_EQ(len, (int)(NE_MIN_PACKET_SIZE + NE_MAX_DATA_LEN));
    CHECK_EQ(ne_parse_packet(buf, (size_t)len, &pkt, &consumed), NE_OK);
    CHECK_EQ(pkt.data_len, NE_MAX_DATA_LEN);
    CHECK_EQ(memcmp(pkt.data, data, NE_MAX_DATA_LEN), 0);
}

int main(void)
{
    test_crc();
    test_byte_order();
    test_build();
    test_parse_roundtrip();
    test_resync_after_garbage();
    test_rejects();
    test_incomplete();
    test_two_packets();
    test_max_payload();

    if (failures != 0) {
        printf("\n%d из %d проверок провалено\n", failures, checks);
        return 1;
    }
    printf("OK: %d проверок\n", checks);
    return 0;
}
