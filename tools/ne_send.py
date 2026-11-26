#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Отправитель пакетов NeModbus — прогон контракта на стенде.

Контракт: Modules/ne_protocol/README.md. Формат (big-endian):

    A55A | 0001 | cmd u32 | len u16 | data[len] | crc32

CRC32 — стандартный (zlib), считается по заголовку и данным, без самого поля CRC.

Примеры:
    python -X utf8 tools/ne_send.py --hex set-pos 12.5    # только пакет, без порта
    python -X utf8 tools/ne_send.py -p COM5 set-pos 12.5
    python -X utf8 tools/ne_send.py -p COM5 set-hw -5
    python -X utf8 tools/ne_send.py -p COM5 --read get-pos
"""
import argparse
import struct
import sys
import time
import zlib

START_WORD = 0xA55A
VERSION = 0x0001
MAX_DATA = 128

CMD = {
    "set-pos":    0x00000049,
    "set-strain": 0x0000004A,
    "set-deform": 0x0000009F,
    "set-hw":     0x00000046,
    "get-pos":    0x01000049,
    "get-strain": 0x0100004A,
    "get-deform": 0x0100009F,
    "get-hw":     0x01000046,
    "get-tjc-status":        0x04020000,
    "get-peak-locked":       0x04020001,
    "get-peak-strain":       0x04020002,
    "get-peak-position":     0x04020003,
    "get-peak-deformation":  0x04020004,
}
SET_FLOAT = ("set-pos", "set-strain", "set-deform")
GET_FLOAT = ("get-pos", "get-strain", "get-deform")
PEAK_FLOAT = ("get-peak-strain", "get-peak-position", "get-peak-deformation")


def build(cmd, data=b""):
    """Собрать пакет: заголовок, данные, CRC32."""
    if len(data) > MAX_DATA:
        raise ValueError("payload больше %d байт" % MAX_DATA)
    body = struct.pack(">HHIH", START_WORD, VERSION, cmd, len(data)) + data
    return body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def parse(buf):
    """Первый валидный пакет в буфере -> (cmd, data) или None."""
    i = 0
    while True:
        i = buf.find(b"\xA5\x5A", i)
        if i < 0:
            return None
        chunk = buf[i:]
        if len(chunk) < 10:
            return None
        ver, cmd, ln = struct.unpack(">HIH", chunk[2:10])
        if ver != VERSION or ln > MAX_DATA:
            i += 1
            continue
        total = 10 + ln + 4
        if len(chunk) < total:
            return None
        body = chunk[:10 + ln]
        crc = struct.unpack(">I", chunk[10 + ln:total])[0]
        if (zlib.crc32(body) & 0xFFFFFFFF) != crc:
            i += 1
            continue
        return cmd, chunk[10:10 + ln]


def payload(command, value):
    """Payload по типу команды: float32 / int16 / пусто."""
    if command in SET_FLOAT:
        if value is None:
            sys.exit("%s: нужно значение" % command)
        return struct.pack(">f", value)
    if command == "set-hw":
        if value is None:
            sys.exit("set-hw: нужно целое значение")
        return struct.pack(">h", int(value))
    if value is not None:
        sys.exit("%s: значение не нужно" % command)
    return b""


def describe(cmd, data):
    """Читаемый вид ответа пульта."""
    names = {v: k for k, v in CMD.items()}
    name = names.get(cmd, "0x%08X" % cmd)
    if name in GET_FLOAT + PEAK_FLOAT and len(data) == 4:
        return "%s -> %.4f" % (name, struct.unpack(">f", data)[0])
    if name == "get-hw" and len(data) == 2:
        return "%s -> %d" % (name, struct.unpack(">h", data)[0])
    if name == "get-peak-locked" and len(data) == 2:
        return "%s -> %d" % (name, struct.unpack(">H", data)[0])
    if name == "get-tjc-status" and len(data) == 2:
        v = struct.unpack(">H", data)[0]
        flags = [flag for bit, flag in ((0x01, "tx_busy"), (0x02, "tx_overflow"),
                                       (0x04, "rx_overflow")) if v & bit]
        return "%s -> 0x%04X (%s)" % (name, v, ", ".join(flags) or "ok")
    return "%s, data=%s" % (name, data.hex(" ").upper())


def main():
    ap = argparse.ArgumentParser(
        description="Отправитель пакетов NeModbus (USART1, 115200 8N1)")
    ap.add_argument("command", choices=sorted(CMD))
    ap.add_argument("value", nargs="?", type=float, help="значение для set-*")
    ap.add_argument("-p", "--port", help="COM-порт, например COM5")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--hex", action="store_true",
                    help="только напечатать пакет, порт не открывать")
    ap.add_argument("--read", action="store_true",
                    help="дождаться ответа и разобрать его")
    ap.add_argument("-t", "--timeout", type=float, default=1.0,
                    help="ожидание ответа, с (по умолчанию 1.0)")
    ap.add_argument("--repeat", type=int, default=1,
                    help="отправить пакет N раз (для set-*)")
    args = ap.parse_args()

    cmd = CMD[args.command]
    pkt = build(cmd, payload(args.command, args.value))
    print("tx: %s" % pkt.hex(" ").upper())

    if args.hex:
        return
    if not args.port:
        sys.exit("нужен --port (или --hex, чтобы только напечатать пакет)")

    try:
        import serial
    except ImportError:
        sys.exit("нет pyserial: python -m pip install pyserial")

    with serial.Serial(args.port, args.baud, timeout=0.05) as port:
        for _ in range(max(1, args.repeat)):
            port.write(pkt)
            port.flush()
        if not args.read:
            return

        deadline = time.time() + args.timeout
        buf = b""
        while time.time() < deadline:
            buf += port.read(port.in_waiting or 1)
            got = parse(buf)
            if got:
                print("rx: %s" % describe(*got))
                return
        sys.exit("ответа нет за %.1f с" % args.timeout)


if __name__ == "__main__":
    main()
