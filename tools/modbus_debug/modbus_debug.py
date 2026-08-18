#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
modbus_debug.py — отладчик Modbus RTU slave (пульт разрывной машины).

Работает как Modbus MASTER на COM-порту через USB-RS485 переходник
(4-проводная шина: RX/TX раздельные, переключение направления не нужно).

Пульт: slave 0x0A, 115200 8N1, стек FreeModbus (проект Pult_Encod_Koda).

Карта регистров (из Core/Src/main.c):
  Holding Registers (FC03/FC06/FC16):
    0x1000-0x1001  float перемещение   (IEEE754, старшее слово первым)
    0x1002-0x1003  float сила
    0x1004-0x1005  float деформация
    0x1006-0x1007  float время испытания
    0x2000         uint16 значение дисплея (запись 0..30)
    0x4002         int16  быстрая команда (запись; ±5 как с кнопок)
  Input Registers (FC04):
    0x4000         порт A (кнопки, инвертированные, по маске)
    0x4001         порт B (кнопки, инвертированные, по маске)
    0x4002         энкодер (int8) или быстрая команда, если нажата

Подкоманды:
  list-ports            список COM-портов
  ping                  быстрый опрос holding-регистров (проверка связи)
  read                  чтение holding (float) или input регистров
  write-float           запись float в канал (0..3)
  display <0..30>       запись регистра дисплея 0x2000
  cmd <int>             быстрая команда 0x4002 (0 = отпущена)
  poll                  циклический опрос (таблица + лог)
  raw                   сырые кадры: hex-дамп запроса/ответа + CRC + RTT
  emulate               эмуляция машины: циклическая запись float + опрос кнопок
  test                  автотест стека пульта (pass/fail)
  selftest              самопроверка CRC/парсера без порта

Зависимость: pyserial (pip install pyserial).
"""
import argparse
import math
import struct
import sys
import time

try:
    import serial
    import serial.tools.list_ports as list_ports
except ImportError:
    sys.stderr.write("Ошибка: нужен pyserial — установите: python -m pip install pyserial\n")
    sys.exit(2)

# ---------------------------------------------------------------- Modbus RTU

def crc16(data: bytes) -> int:
    """CRC16-Modbus (полином 0xA001, init 0xFFFF)."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def build_frame(addr: int, pdu: bytes) -> bytes:
    body = bytes([addr]) + pdu
    c = crc16(body)
    return body + bytes([c & 0xFF, (c >> 8) & 0xFF])


class ModbusError(Exception):
    def __init__(self, code: int):
        self.code = code
        super().__init__(f"Modbus exception 0x{code:02X}")

EXC_NAMES = {
    0x01: "ILLEGAL FUNCTION",
    0x02: "ILLEGAL DATA ADDRESS",
    0x03: "ILLEGAL DATA VALUE",
    0x04: "SLAVE DEVICE FAILURE",
    0x05: "ACKNOWLEDGE",
    0x06: "SLAVE DEVICE BUSY",
    0x08: "MEMORY PARITY ERROR",
    0x0A: "GATEWAY PATH UNAVAILABLE",
    0x0B: "GATEWAY TARGET DEVICE FAILED TO RESPOND",
}


class ModbusRtuMaster:
    """Минимальный Modbus RTU master для одного slave."""

    def __init__(self, port: str, baud: int = 115200, addr: int = 0x0A,
                 timeout: float = 0.6):
        self.addr = addr
        self.timeout = timeout
        self.ser = serial.Serial(port, baud, bytesize=8, parity="N",
                                 stopbits=1, timeout=timeout,
                                 write_timeout=timeout)
        self.ser.reset_input_buffer()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def _read_frame(self, max_len: int = 256) -> bytes:
        """Читает кадр: первый байт ждём с полным таймаутом (это и есть
        время ответа slave), остаток собираем пачками через in_waiting,
        пока данные идут; тишина 50 мс = конец кадра (надёжно для
        переходников с порционной выдачей, напр. PL2303)."""
        buf = bytearray()
        b = self.ser.read(1)
        if not b:
            return bytes(buf)
        buf += b
        silence = 0.05
        deadline = time.monotonic() + silence
        while len(buf) < max_len and time.monotonic() < deadline:
            n = self.ser.in_waiting
            if n:
                chunk = self.ser.read(min(n, max_len - len(buf)))
                if not chunk:
                    break
                buf += chunk
                deadline = time.monotonic() + silence
            else:
                time.sleep(0.005)
        return bytes(buf)

    def transact(self, fc: int, data: bytes = b"", label: str = "",
                 raw: bool = False):
        """Отправляет запрос, возвращает (pdu, rtt). Кидает ModbusError/TimeoutError."""
        pdu = bytes([fc]) + data
        frame = build_frame(self.addr, pdu)
        t0 = time.monotonic()
        self.ser.write(frame)
        resp = self._read_frame()
        rtt = (time.monotonic() - t0) * 1000.0
        if raw:
            print(f"  TX: {frame.hex(' ')}")
        if not resp:
            raise TimeoutError(f"нет ответа ({label}) за {self.timeout:.1f} c")
        if raw:
            print(f"  RX: {resp.hex(' ')}")
        if len(resp) < 4:
            raise TimeoutError(f"короткий/мусорный ответ ({label}): {resp.hex(' ')}")
        if resp[0] != self.addr:
            raise TimeoutError(f"ответ от чужого адреса 0x{resp[0]:02X} (ждём 0x{self.addr:02X})")
        calc = crc16(resp[:-2])
        got = (resp[-2] | (resp[-1] << 8))
        if calc != got:
            raise TimeoutError(f"CRC не сошёлся ({label}): ожид. 0x{calc:04X}, получ. 0x{got:04X}")
        rfc = resp[1]
        if rfc == (fc | 0x80):
            code = resp[2]
            raise ModbusError(code)
        if rfc != fc:
            raise TimeoutError(f"неожиданная функция 0x{rfc:02X} в ответе ({label})")
        return resp[2:-2], rtt

    # --- операции ---
    def read_holding(self, reg: int, n: int = 1, raw: bool = False):
        pdu, rtt = self.transact(0x03, struct.pack(">HH", reg, n), f"FC03 0x{reg:04X} n={n}", raw)
        return pdu[1:], rtt  # отрезаем byte-count

    def read_input(self, reg: int, n: int = 1, raw: bool = False):
        pdu, rtt = self.transact(0x04, struct.pack(">HH", reg, n), f"FC04 0x{reg:04X} n={n}", raw)
        return pdu[1:], rtt  # отрезаем byte-count

    def write_single(self, reg: int, val: int, raw: bool = False):
        return self.transact(0x06, struct.pack(">HH", reg, val & 0xFFFF), f"FC06 0x{reg:04X}={val}", raw)

    def write_multiple(self, reg: int, vals, raw: bool = False):
        data = bytes([reg >> 8, reg & 0xFF, len(vals) >> 8, len(vals) & 0xFF,
                      len(vals) * 2]) + b"".join(struct.pack(">H", v & 0xFFFF) for v in vals)
        return self.transact(0x10, data, f"FC16 0x{reg:04X} n={len(vals)}", raw)


# ---------------------------------------------------------------- карта пульта

FLOAT_CHANNELS = [
    (0x1000, "перемещение"),
    (0x1002, "сила"),
    (0x1004, "деформация"),
    (0x1006, "время испытания"),
]

PORTA_BITS = {
    1: "F2", 2: "F1", 3: "верх.захват-закрыть", 8: "нижн.захват-открыть",
    11: "возврат", 12: "запись нуля", 15: "стоп",
}
PORTB_BITS = {
    3: "нижн.захват-закрыть", 8: "защита", 9: "пуск", 11: "бит11",
    12: "кнопка энкодера", 15: "верх.захват-открыть",
}


def regs_to_float(regs) -> float:
    u = (regs[0] << 16) | regs[1]
    return struct.unpack(">f", struct.pack(">I", u))[0]


def float_to_regs(v: float):
    u = struct.unpack(">I", struct.pack(">f", v))[0]
    return [(u >> 16) & 0xFFFF, u & 0xFFFF]


def decode_buttons(value: int, table: dict) -> str:
    names = [n for bit, n in sorted(table.items()) if value & (1 << bit)]
    return ", ".join(names) if names else "-"


# ---------------------------------------------------------------- команды

def cmd_ping(m, args):
    data, rtt = m.read_holding(0x1000, 8)
    regs = struct.unpack(">8H", data)
    print(f"Связь OK: slave 0x{m.addr:02X}, RTT {rtt:6.2f} мс")
    for (reg, name), r0, r1 in zip(FLOAT_CHANNELS, regs[0::2], regs[1::2]):
        print(f"  0x{reg:04X}  {name:<16} = {regs_to_float([r0, r1]):.3f}")


def cmd_read(m, args):
    if args.kind == "holding":
        data, rtt = m.read_holding(args.reg, args.n)
        print(f"RTT {rtt:6.2f} мс")
        if args.reg >= 0x1000 and args.reg + args.n <= 0x1008:
            regs = struct.unpack(f">{args.n}H", data)
            for i in range(0, args.n - 1, 2):
                r = args.reg + i
                name = "?"
                for (base, nm) in FLOAT_CHANNELS:
                    if r == base:
                        name = nm
                print(f"  0x{r:04X}  {name:<16} = {regs_to_float(regs[i:i+2]):.3f}")
            if args.n % 2:
                print(f"  0x{args.reg+args.n-1:04X}  = 0x{regs[-1]:04X} ({regs[-1]})")
        else:
            for i, v in enumerate(struct.unpack(f">{args.n}H", data)):
                print(f"  0x{args.reg+i:04X}  = 0x{v:04X} ({v})")
    else:
        data, rtt = m.read_input(args.reg, args.n)
        print(f"RTT {rtt:6.2f} мс")
        for i, v in enumerate(struct.unpack(f">{args.n}H", data)):
            r = args.reg + i
            extra = ""
            if r == 0x4000:
                extra = "  кнопки A: " + decode_buttons(v, PORTA_BITS)
            elif r == 0x4001:
                extra = "  кнопки B: " + decode_buttons(v, PORTB_BITS)
            elif r == 0x4002:
                extra = f"  (энкодер/команда: {int(struct.unpack('>h', struct.pack('>H', v))[0])})"
            print(f"  0x{r:04X}  = 0x{v:04X} ({v}){extra}")


def cmd_write_float(m, args):
    regs = float_to_regs(args.value)
    base = 0x1000 + args.channel * 2
    data, rtt = m.write_multiple(base, regs)
    print(f"Записано float {args.value:.3f} в канал {args.channel} (0x{base:04X}), RTT {rtt:.2f} мс")
    # чтение назад для контроля
    data2, rtt2 = m.read_holding(base, 2)
    got = regs_to_float(struct.unpack(">2H", data2))
    ok = math.isclose(got, args.value, rel_tol=1e-5)
    print(f"  Контроль чтения: {got:.6f}  {'OK' if ok else 'НЕ СОВПАЛО'}")


def cmd_display(m, args):
    data, rtt = m.write_single(0x2000, args.value)
    print(f"Дисплей 0x2000 = {args.value}, RTT {rtt:.2f} мс")
    data2, rtt2 = m.read_holding(0x2000, 1)
    got = struct.unpack(">H", data2)[0]
    print(f"  Контроль чтения: {got}  {'OK' if got == args.value else 'НЕ СОВПАЛО'}")


def cmd_cmd(m, args):
    data, rtt = m.write_single(0x4002, args.value & 0xFFFF)
    print(f"Быстрая команда 0x4002 = {args.value}, RTT {rtt:.2f} мс")
    data2, rtt2 = m.read_input(0x4002, 1)
    v = struct.unpack(">H", data2)[0]
    print(f"  Контроль чтения input 0x4002: {v} (int16: {int(struct.unpack('>h', struct.pack('>H', v))[0])})")


def cmd_poll(m, args):
    log = open(args.log, "w", encoding="utf-8") if args.log else None
    if log:
        log.write("time_ms;rtt_ms;disp;cmd;inA;inB;enc;ch0;ch1;ch2;ch3\n")
    print("Опрос каждые %.1f c. Ctrl+C — выход." % args.interval)
    print(f"{'Время':<12}{'RTT':>6}{'Диспл':>6}{'КнопкиA':<26}{'КнопкиB':<24}{'Передвиж':>10}{'Сила':>10}{'Деформ':>10}{'Время':>10}")
    try:
        while True:
            t0 = time.monotonic()
            try:
                hi, rtt = m.read_holding(0x1000, 8)
                regs = struct.unpack(">8H", hi)
                floats = [regs_to_float(regs[i:i+2]) for i in (0, 2, 4, 6)]
                inp, rtt2 = m.read_input(0x4000, 3)
                ia, ib, enc = struct.unpack(">3H", inp)
                disp, _ = m.read_holding(0x2000, 1)
                disp = struct.unpack(">H", disp)[0]
                ta = time.strftime("%H:%M:%S") + f".{int((time.monotonic()-t0)*1000)%1000:03d}"
                rtt_tot = rtt + rtt2
                print(f"{ta:<12}{rtt_tot:6.1f}{disp:6d} "
                      f"{decode_buttons(ia, PORTA_BITS):<26}{decode_buttons(ib, PORTB_BITS):<24}"
                      f"{floats[0]:10.3f}{floats[1]:10.3f}{floats[2]:10.3f}{floats[3]:10.3f}")
                if log:
                    log.write(f"{int(time.monotonic()*1000)};{rtt_tot:.2f};{disp};{enc};{ia};{ib};{enc};"
                              f"{floats[0]:.6f};{floats[1]:.6f};{floats[2]:.6f};{floats[3]:.6f}\n")
                    log.flush()
            except (TimeoutError, ModbusError) as e:
                print(f"{time.strftime('%H:%M:%S')}  ОШИБКА: {e}")
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nОстановлено.")
    finally:
        if log:
            log.close()


def cmd_raw(m, args):
    print(f"Сырой режим: каждые {args.interval} с отправляем FC03 0x1000 n=8. Ctrl+C — выход.\n")
    try:
        while True:
            try:
                data, rtt = m.transact(0x03, struct.pack(">HH", 0x1000, 8),
                                       "FC03 0x1000 n=8", raw=True)
                print(f"  OK  RTT {rtt:7.2f} мс  байт данных: {len(data)}")
            except TimeoutError as e:
                print(f"  {e}")
            except ModbusError as e:
                print(f"  EXCEPTION: {EXC_NAMES.get(e.code, e.code)}")
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nОстановлено.")


def cmd_emulate(m, args):
    """Эмуляция машины: циклически пишет float-каналы и читает кнопки."""
    log = open(args.log, "w", encoding="utf-8") if args.log else None
    if log:
        log.write("time_ms;rtt_ms;inA;inB;enc;ch0;ch1;ch2;ch3\n")
    n = 0
    dt = args.interval
    print(f"Эмуляция машины: шаг {dt} c, канал0 = {args.pos0} + n*{args.dpos}, "
          f"канал1 = {args.amp}*sin(2π*{args.freq}*t)+{args.offset}. Ctrl+C — выход.")
    try:
        while True:
            t = n * dt
            pos = args.pos0 + n * args.dpos
            force = args.offset + args.amp * math.sin(2 * math.pi * args.freq * t)
            deform = pos * args.deform_k
            vals = [pos, force, deform, t]
            t0 = time.monotonic()
            try:
                rtts = []
                for base, v in zip((0x1000, 0x1002, 0x1004, 0x1006), vals):
                    _, rtt = m.write_multiple(base, float_to_regs(v))
                    rtts.append(rtt)
                inp, rtt = m.read_input(0x4000, 3)
                ia, ib, enc = struct.unpack(">3H", inp)
                rtts.append(rtt)
                ta = time.strftime("%H:%M:%S") + f".{int((time.monotonic()-t0)*1000)%1000:03d}"
                print(f"{ta}  pos={pos:9.3f}  force={force:9.3f}  def={deform:9.3f}  t={t:6.1f}  "
                      f"кнопки A: {decode_buttons(ia, PORTA_BITS):<22} B: {decode_buttons(ib, PORTB_BITS):<20}  "
                      f"RTT {sum(rtts):.1f} мс")
                if log:
                    log.write(f"{int(time.monotonic()*1000)};{sum(rtts):.2f};{ia};{ib};{enc};"
                              f"{pos:.6f};{force:.6f};{deform:.6f};{t:.6f}\n")
                    log.flush()
            except (TimeoutError, ModbusError) as e:
                print(f"{time.strftime('%H:%M:%S')}  ОШИБКА: {e}")
            n += 1
            time.sleep(dt)
    except KeyboardInterrupt:
        print("\nОстановлено.")
    finally:
        if log:
            log.close()


def check_ok(name, ok, detail=""):
    print(f"  [{'OK ' if ok else 'FAIL'}] {name}{('  — ' + detail) if detail else ''}")
    return (name, ok, detail)


def cmd_test(m, args):
    print(f"Автотест стека пульта (slave 0x{m.addr:02X}, {m.ser.port}). "
          f"Пульт должен быть ПОДКЛЮЧЁН.\n")
    results = []

    # 1. Чтение input regs
    try:
        data, rtt = m.read_input(0x4000, 3)
        ia, ib, enc = struct.unpack(">3H", data)
        results.append(check_ok("FC04 чтение input 0x4000-0x4002", True,
                                f"A=0x{ia:04X} B=0x{ib:04X} enc={enc} RTT {rtt:.2f} мс"))
    except (TimeoutError, ModbusError) as e:
        results.append(check_ok("FC04 чтение input 0x4000-0x4002", False, str(e)))

    # 2. Чтение holding (float)
    try:
        data, rtt = m.read_holding(0x1000, 8)
        regs = struct.unpack(">8H", data)
        floats = [regs_to_float(regs[i:i+2]) for i in (0, 2, 4, 6)]
        results.append(check_ok("FC03 чтение holding 0x1000-0x1007 (4 float)", True,
                                " / ".join(f"{f:.3f}" for f in floats)))
    except (TimeoutError, ModbusError) as e:
        results.append(check_ok("FC03 чтение holding 0x1000-0x1007", False, str(e)))

    # 3. Запись float в канал 1 и контроль чтения
    try:
        test_val = 123.456
        m.write_multiple(0x1002, float_to_regs(test_val))
        data, _ = m.read_holding(0x1002, 2)
        got = regs_to_float(struct.unpack(">2H", data))
        ok = math.isclose(got, test_val, rel_tol=1e-5)
        results.append(check_ok("FC16 запись float в 0x1002/03 + контроль", ok,
                                f"{got:.6f} (ждём {test_val})"))
    except (TimeoutError, ModbusError) as e:
        results.append(check_ok("FC16 запись float", False, str(e)))

    # 4. Запись дисплея 0x2000
    try:
        m.write_single(0x2000, 5)
        data, _ = m.read_holding(0x2000, 1)
        got = struct.unpack(">H", data)[0]
        results.append(check_ok("FC06 запись дисплея 0x2000=5 + контроль", got == 5,
                                f"прочитано {got}"))
    except (TimeoutError, ModbusError) as e:
        results.append(check_ok("FC06 запись дисплея", False, str(e)))

    # 5. Быстрая команда 0x4002
    try:
        m.write_single(0x4002, 7)
        data, _ = m.read_input(0x4002, 1)
        v = struct.unpack(">H", data)[0]
        results.append(check_ok("FC06 быстрая команда 0x4002=7 + чтение input", v == 7,
                                f"input 0x4002 = {v}"))
    except (TimeoutError, ModbusError) as e:
        results.append(check_ok("FC06 быстрая команда", False, str(e)))

    # 6. Запись в несуществующий адрес → ожидаем exception 0x02
    try:
        m.write_single(0x5000, 1)
        results.append(check_ok("Запись 0x5000 (нет такого адреса)", False,
                                "неожиданно получен нормальный ответ"))
    except ModbusError as e:
        results.append(check_ok("Запись 0x5000 → exception", e.code == 0x02,
                                EXC_NAMES.get(e.code, f"0x{e.code:02X}")))
    except TimeoutError as e:
        results.append(check_ok("Запись 0x5000 → exception", False, f"таймаут: {e}"))

    # 7. Плохой CRC → не должно быть ответа
    try:
        frame = build_frame(m.addr, bytes([0x03, 0x10, 0x00, 0x00, 0x08]))
        bad = frame[:-2] + bytes([frame[-2] ^ 0xFF, frame[-1]])
        m.ser.reset_input_buffer()
        m.ser.write(bad)
        resp = m.ser.read(64)
        time.sleep(0.01)
        results.append(check_ok("Кадр с битым CRC игнорируется", not resp,
                                f"получено {len(resp)} байт"))
    except Exception as e:
        results.append(check_ok("Кадр с битым CRC игнорируется", False, str(e)))

    # 8. Чужой адрес slave → нет ответа
    try:
        frame = build_frame(m.addr ^ 0x01, bytes([0x03, 0x10, 0x00, 0x00, 0x08]))
        m.ser.reset_input_buffer()
        m.ser.write(frame)
        resp = m.ser.read(64)
        time.sleep(0.01)
        results.append(check_ok("Чужой адрес slave игнорируется", not resp,
                                f"получено {len(resp)} байт"))
    except Exception as e:
        results.append(check_ok("Чужой адрес slave игнорируется", False, str(e)))

    # 9. FC17 Report Slave ID (нужен eMBSetSlaveID до eMBEnable в прошивке)
    try:
        data, rtt = m.transact(0x11, b"", "FC17")
        if len(data) < 2:
            results.append(check_ok("FC17 Report Slave ID", False,
                                    f"пустой payload ({len(data)} б): похоже, "
                                    f"старая прошивка без eMBSetSlaveID"))
        else:
            bc = data[0]
            slave_id = data[1]
            run = data[2] if len(data) > 2 else 0
            ok = bc >= 2 and slave_id == m.addr
            results.append(check_ok("FC17 Report Slave ID", ok,
                                    f"bytecount={bc} slaveID=0x{slave_id:02X} "
                                    f"run=0x{run:02X} RTT {rtt:.2f} мс"))
    except (TimeoutError, ModbusError) as e:
        results.append(check_ok("FC17 Report Slave ID", False, str(e)))

    passed = sum(1 for _, ok, _ in results if ok)
    print(f"\nИтог: {passed}/{len(results)} прошло")
    return 0 if passed == len(results) else 1


def cmd_selftest(args):
    print("Самопроверка CRC16-Modbus и парсера (без порта):\n")
    results = []
    # эталонные векторы CRC16-Modbus
    vec = [
        (bytes.fromhex("01 03 00 00 00 0A"), 0xCDC5),
        (b"123456789", 0x4B37),
        (bytes.fromhex("0A 04 40 00 00 03"), None),  # только сверка TX/RX round-trip ниже
    ]
    ok_all = True
    for data, expect in vec[:2]:
        got = crc16(data)
        ok = got == expect
        ok_all &= ok
        results.append(check_ok(f"CRC16({data.hex(' ')}) = 0x{got:04X}", ok, f"ждём 0x{expect:04X}"))
    # round-trip: сборка кадра FC03, извлечение PDU по длине
    frame = build_frame(0x0A, bytes([0x03, 0x10, 0x00, 0x00, 0x08]))
    body = frame[:-2]
    pdu_len = 5 + (0x10 >> 8) * 0  # для запроса FC03 длина PDU известна: 5 байт
    pdu = body[1:]
    ok = pdu == bytes([0x03, 0x10, 0x00, 0x00, 0x08])
    results.append(check_ok("Сборка кадра запроса FC03", ok, frame.hex(" ")))
    # парсер ответа: FC03 с 4 регистрами (float 1.0)
    resp_regs = float_to_regs(1.0) + float_to_regs(-2.5)
    resp = bytes([0x0A, 0x03, 8]) + b"".join(struct.pack(">H", r) for r in resp_regs)
    resp += bytes([crc16(resp) & 0xFF, (crc16(resp) >> 8) & 0xFF])
    ok = crc16(resp[:-2]) == ((resp[-2]) | (resp[-1] << 8))
    results.append(check_ok("Валидация CRC ответа", ok))
    f0 = regs_to_float(struct.unpack(">4H", resp[3:11])[0:2])
    f1 = regs_to_float(struct.unpack(">4H", resp[3:11])[2:4])
    results.append(check_ok("Декодирование float из ответа", f0 == 1.0 and f1 == -2.5,
                            f"{f0} / {f1}"))
    passed = sum(1 for _, ok, _ in results if ok)
    print(f"\nИтог: {passed}/{len(results)} прошло")
    return 0 if passed == len(results) else 1


def cmd_list_ports(args):
    ports = list(list_ports.comports())
    if not ports:
        print("COM-портов не найдено.")
        return 0
    for p in ports:
        print(f"{p.device:<6} {p.description}  [{p.hwid}]")
    return 0


def main():
    ap = argparse.ArgumentParser(
        description="Отладчик Modbus RTU slave (пульт разрывной машины). "
                    "Master на COM-порту, 4-проводный RS485.")
    ap.add_argument("--port", default="COM12", help="COM-порт (по умолчанию COM12)")
    ap.add_argument("--baud", type=int, default=115200, help="скорость (по умолчанию 115200)")
    ap.add_argument("--addr", type=lambda s: int(s, 0), default=0x0A,
                    help="адрес slave (по умолчанию 0x0A)")
    ap.add_argument("--timeout", type=float, default=0.6, help="таймаут ответа, с (по умолчанию 0.6)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("list-ports", help="список COM-портов")
    sub.add_parser("ping", help="проверка связи + чтение float-каналов")

    p = sub.add_parser("read", help="чтение регистров")
    p.add_argument("kind", choices=["holding", "input"], help="пространство регистров")
    p.add_argument("reg", type=lambda s: int(s, 0), help="адрес (0x1000 или 4096)")
    p.add_argument("n", type=int, nargs="?", default=1, help="количество регистров")

    p = sub.add_parser("write-float", help="запись float в канал графика")
    p.add_argument("channel", type=int, choices=[0, 1, 2, 3], help="канал: 0=перемещение, 1=сила, 2=деформация, 3=время")
    p.add_argument("value", type=float, help="значение")

    p = sub.add_parser("display", help="запись регистра дисплея 0x2000 (0..30)")
    p.add_argument("value", type=int)

    p = sub.add_parser("cmd", help="быстрая команда 0x4002 (int16; 0 = отпущена)")
    p.add_argument("value", type=int)

    p = sub.add_parser("poll", help="циклический опрос (таблица + лог)")
    p.add_argument("--interval", type=float, default=0.5, help="период, с (по умолчанию 0.5)")
    p.add_argument("--log", help="файл лога (CSV, utf-8)")

    p = sub.add_parser("raw", help="сырые кадры: hex TX/RX, CRC, RTT")
    p.add_argument("--interval", type=float, default=1.0, help="период, с (по умолчанию 1)")

    p = sub.add_parser("emulate", help="эмуляция машины: циклическая запись float + опрос кнопок")
    p.add_argument("--interval", type=float, default=0.1, help="период, с (по умолчанию 0.1)")
    p.add_argument("--pos0", type=float, default=0.0, help="начальное перемещение")
    p.add_argument("--dpos", type=float, default=0.5, help="прирост перемещения за шаг")
    p.add_argument("--amp", type=float, default=100.0, help="амплитуда силы, Н")
    p.add_argument("--freq", type=float, default=0.2, help="частота силы, Гц")
    p.add_argument("--offset", type=float, default=500.0, help="смещение силы, Н")
    p.add_argument("--deform-k", type=float, default=0.6, help="деформация = перемещение * k")
    p.add_argument("--log", help="файл лога (CSV, utf-8)")

    p = sub.add_parser("test", help="автотест стека пульта (нужен подключённый пульт)")
    sub.add_parser("selftest", help="проверка CRC/парсера без порта")

    args = ap.parse_args()

    if args.cmd == "list-ports":
        return cmd_list_ports(args)
    if args.cmd == "selftest":
        return cmd_selftest(args)

    try:
        m = ModbusRtuMaster(args.port, args.baud, args.addr, args.timeout)
    except serial.SerialException as e:
        sys.stderr.write(f"Не удалось открыть {args.port}: {e}\n")
        return 1
    try:
        if args.cmd == "ping":
            cmd_ping(m, args)
        elif args.cmd == "read":
            cmd_read(m, args)
        elif args.cmd == "write-float":
            cmd_write_float(m, args)
        elif args.cmd == "display":
            cmd_display(m, args)
        elif args.cmd == "cmd":
            cmd_cmd(m, args)
        elif args.cmd == "poll":
            cmd_poll(m, args)
        elif args.cmd == "raw":
            cmd_raw(m, args)
        elif args.cmd == "emulate":
            cmd_emulate(m, args)
        elif args.cmd == "test":
            return cmd_test(m, args)
        return 0
    except (TimeoutError, ModbusError) as e:
        sys.stderr.write(f"ОШИБКА: {e}\n")
        return 1
    finally:
        m.close()


if __name__ == "__main__":
    sys.exit(main())
