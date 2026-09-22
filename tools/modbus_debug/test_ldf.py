#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_ldf.py — сценарий проверки пульта: рост L, D, F с разными скоростями,
спад F через N секунд и проверка фиксации результата на экране пульта.

Маппинг на карту регистров пульта (slave 0x0A, 115200 8N1):
  0x1000-01  L — перемещение   -> поле x0 на экране
  0x1002-03  F — сила          -> поле x1
  0x1004-05  D — деформация    -> поле x2
  0x1006-07  время испытания   -> поле x3
  0x3001     флаг фиксации (1 = зафиксирован)
  0x3002-07  зафиксированные Fmax, L, D

Профиль:
  фаза «рост»  0..rise: L = vL*t, D = vD*t, F = vF*t   (разные скорости)
  фаза «спад»  rise..:  F убывает с vFall, L и D продолжают расти
После спада экран пульта должен показать ЗАФИКСИРОВАННЫЕ Fmax, L, D.

Все три канала пишутся ОДНИМ кадром FC16 (0x1000..0x1007), чтобы значения
на экране не рассинхронизировались.

Запуск:
  python test_ldf.py --port COM13
  python test_ldf.py --port COM13 --vL 2 --vD 5 --vF 20 --rise 5 --hold 12
"""
import argparse
import math
import struct
import sys
import time

from modbus_debug import (ModbusRtuMaster, ModbusError, float_to_regs,
                          regs_to_float)


def read_peak(m):
    """Возвращает (locked, fmax, l_at_peak, d_at_peak)."""
    data, rtt = m.read_holding(0x3001, 7)
    r = struct.unpack(">7H", data)
    return (r[0], regs_to_float(r[1:3]), regs_to_float(r[3:5]),
            regs_to_float(r[5:7]), rtt)


def write_channels(m, l, f, d, t):
    """Один кадр FC16: L, F, D, время (8 регистров с 0x1000)."""
    regs = float_to_regs(l) + float_to_regs(f) + float_to_regs(d) + float_to_regs(t)
    _, rtt = m.write_multiple(0x1000, regs)
    return rtt


def main():
    ap = argparse.ArgumentParser(description="Тест роста/спада L, D, F и фиксации")
    ap.add_argument("--port", default="COM13")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--addr", type=lambda s: int(s, 0), default=0x0A)
    ap.add_argument("--vL", type=float, default=2.0, help="скорость L, ед/с")
    ap.add_argument("--vD", type=float, default=5.0, help="скорость D, ед/с")
    ap.add_argument("--vF", type=float, default=20.0, help="скорость F, ед/с")
    ap.add_argument("--rise", type=float, default=5.0, help="время роста F, с")
    ap.add_argument("--vFall", type=float, default=25.0, help="скорость спада F, ед/с")
    ap.add_argument("--interval", type=float, default=0.1, help="шаг записи, с")
    ap.add_argument("--hold", type=float, default=12.0,
                    help="сколько секунд держать зафиксированный результат на экране")
    ap.add_argument("--log", default="test_ldf.csv")
    args = ap.parse_args()

    m = ModbusRtuMaster(args.port, args.baud, args.addr, 0.6)
    log = open(args.log, "w", encoding="utf-8")
    log.write("t;L;F;D;locked;Fmax;L_peak;D_peak;rtt_ms\n")
    rc = 0
    try:
        locked, fmax, lp, dp, _ = read_peak(m)
        print(f"Состояние пульта перед тестом: фиксация = {'ЗАФИКСИРОВАНА' if locked else 'нет'}")
        if locked:
            print("ЭКРАН ЗАМОРОЖЕН предыдущим результатом. Нажмите на пульте "
                  "«ПУСК» (PB9) или «запись нуля» (PA12) и повторите запуск.")
            return 2

        print(f"Старт: vL={args.vL:g} vD={args.vD:g} vF={args.vF:g} ед/с | "
              f"спад F с t={args.rise:g} c (vFall={args.vFall:g}) | шаг {args.interval:g} с")
        print(f"{'t,с':>6}{'L':>9}{'F':>9}{'D':>9}   {'фаза':<6}{'фикс':>5}"
              f"{'Fmax':>9}{'Lпик':>9}{'Dпик':>9}{'RTT':>7}")

        t0 = time.monotonic()
        t_end_fall = args.rise + args.vF * args.rise / args.vFall + 1.0
        last_peak_read = 0.0
        locked_now, fmax_l = 0.0, 0.0
        peak_shown = (0.0, 0.0)
        while True:
            t = time.monotonic() - t0
            l = args.vL * t
            d = args.vD * t
            if t < args.rise:
                f = args.vF * t
                phase = "рост"
            else:
                f = max(0.0, args.vF * args.rise - args.vFall * (t - args.rise))
                phase = "спад" if f > 0.0 else "ноль"

            rtt = write_channels(m, l, f, d, t)

            # состояние фиксации читаем реже, чтобы не тормозить цикл записи
            if t - last_peak_read >= 0.5 or phase == "ноль":
                last_peak_read = t
                locked_now, fmax_l, lp, dp, rtt2 = read_peak(m)
                rtt += rtt2
                if locked_now:
                    peak_shown = (fmax_l, lp)
            print(f"{t:6.2f}{l:9.2f}{f:9.2f}{d:9.2f}   {phase:<6}{locked_now:>5}"
                  f"{fmax_l:9.2f}{lp:9.2f}{dp:9.2f}{rtt:7.1f}")
            log.write(f"{t:.3f};{l:.4f};{f:.4f};{d:.4f};{locked_now};"
                      f"{fmax_l:.4f};{lp:.4f};{dp:.4f};{rtt:.1f}\n")
            log.flush()

            if phase == "ноль" and t >= t_end_fall:
                break
            time.sleep(args.interval)

        # === Итог: пульт должен показать зафиксированный результат ===
        time.sleep(1.0)
        locked, fmax, lp, dp, _ = read_peak(m)
        exp_f = args.vF * args.rise
        exp_l = args.vL * args.rise
        exp_d = args.vD * args.rise
        print("\n--- ИТОГ ---")
        print(f"  Фиксация на экране : {'ДА (значения заморожены)' if locked else 'НЕТ — ОШИБКА'}")
        print(f"  Fmax (экран x1)    : {fmax:9.3f}   ожидание ~{exp_f:.3f}")
        print(f"  L    (экран x0)    : {lp:9.3f}   ожидание ~{exp_l:.3f}")
        print(f"  D    (экран x2)    : {dp:9.3f}   ожидание ~{exp_d:.3f}")
        if not locked:
            print("  РЕЗУЛЬТАТ НЕ ЗАФИКСИРОВАН")
            rc = 1
        else:
            for nm, got, exp in (("Fmax", fmax, exp_f), ("L", lp, exp_l), ("D", dp, exp_d)):
                if not math.isclose(got, exp, rel_tol=0.05, abs_tol=1.0):
                    print(f"  ВНИМАНИЕ: {nm} отличается от ожидаемого более чем на 5%")
                    rc = 1
            # L и D в момент пика не должны «уехать» в значения конца теста
            print(f"  L, D в момент пика зафиксированы (не значения конца теста): "
                  f"{'ДА' if lp < args.vL * t_end_fall * 0.9 else 'НЕТ'}")
        print(f"\nДержу результат на экране {args.hold:g} с — сверьте значения x0/x1/x2 с пультом.")
        t_hold = time.monotonic()
        while time.monotonic() - t_hold < args.hold:
            time.sleep(0.2)
        print("Готово. Сброс фиксации — кнопка «ПУСК» или «запись нуля» на пульте.")
    finally:
        log.close()
        m.close()
    return rc


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ModbusError as e:
        sys.stderr.write(f"Modbus exception: {e}\n")
        sys.exit(1)
    except TimeoutError as e:
        sys.stderr.write(f"Таймаут связи: {e}\n")
        sys.exit(1)
