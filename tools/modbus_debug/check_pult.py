#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_pult.py — комплексная проверка логики пульта по Modbus RTU.

Проверяет три вещи:
  1. ФИКСАЦИЯ  — рост L/D/F, спад F, порог «испытание идёт», «плато» у
                 максимума, точность зафиксированных L/D, заморозка.
  2. СБРОС     — обнуление результата нажатием кнопки (ждём реальное
                 нажатие и показываем, какая кнопка сработала).
  3. ЭКРАН     — диагностика TJC (0x3000: busy/overflow/err) + визуальная
                 сверка каналов x0..x3 и масштаба (1 знак, прошивка шлёт x10).

Сброс фиксации в прошивке делается ТОЛЬКО кнопкой (ProcessButtons), поэтому
скрипт интерактивный: в нужном месте просит нажать кнопку и ждёт.

Запуск:  python check_pult.py --port COM13
"""
import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from modbus_debug import (ModbusRtuMaster, ModbusError, float_to_regs,
                          regs_to_float, decode_buttons, PORTA_BITS,
                          PORTB_BITS)

RESULTS = []


def check(name, ok, detail=""):
    RESULTS.append((name, bool(ok), detail))
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}"
          + (f"  — {detail}" if detail else ""))


def write_all(m, L, F, D, t):
    """Один кадр FC16: 0x1000 L, 0x1002 F, 0x1004 D, 0x1006 t."""
    m.write_multiple(0x1000, float_to_regs(L) + float_to_regs(F)
                     + float_to_regs(D) + float_to_regs(t))


def read_peak(m):
    r = struct.unpack(">7H", m.read_holding(0x3001, 7)[0])
    return {"locked": r[0], "f": regs_to_float(r[1:3]),
            "l": regs_to_float(r[3:5]), "d": regs_to_float(r[5:7])}


def read_channels(m):
    r = struct.unpack(">8H", m.read_holding(0x1000, 8)[0])
    return {"L": regs_to_float(r[0:2]), "F": regs_to_float(r[2:4]),
            "D": regs_to_float(r[4:6]), "t": regs_to_float(r[6:8])}


def read_tjc_diag(m):
    d = struct.unpack(">H", m.read_holding(0x3000, 1)[0])[0]
    return {"raw": d, "busy": d & 1, "ovf": (d >> 1) & 1, "err": (d >> 8) & 0xFF}


def read_buttons(m):
    a, b, enc = struct.unpack(">3H", m.read_input(0x4000, 3)[0])
    return a, b, enc


def ramp(m, t_run, vL, vD, vF, label, step=0.1):
    """Линейный рост трёх каналов; возвращает (L,F,D) в конце."""
    t0 = time.monotonic()
    L = F = D = 0.0
    while True:
        t = time.monotonic() - t0
        if t >= t_run:
            break
        L, D, F = vL * t, vD * t, vF * t
        write_all(m, L, F, D, t)
        time.sleep(step)
    print(f"    {label}: L={L:.2f} F={F:.2f} D={D:.2f}")
    return L, F, D


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM13")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--addr", type=lambda s: int(s, 0), default=0x0A)
    ap.add_argument("--wait-btn", type=float, default=120.0,
                    help="сколько секунд ждать нажатия кнопки, с")
    ap.add_argument("--skip-reset", action="store_true",
                    help="не ждать нажатия кнопки (этап 5 пропускается)")
    ap.add_argument("--skip-visual", action="store_true",
                    help="не спрашивать про экран (этап 6 только по регистрам)")
    ap.add_argument("--only-reset", action="store_true",
                    help="только этап 5: дождаться сброса кнопкой")
    ap.add_argument("--only-visual", action="store_true",
                    help="только этап 6: установить 11.1/22.2/33.3/44.4 и "
                         "сверить с экраном")
    args = ap.parse_args()

    m = ModbusRtuMaster(args.port, args.baud, args.addr, timeout=0.8)
    try:
        # ---------- 0. Исходное состояние ----------
        print("\n=== 0. Исходное состояние ===")
        p = read_peak(m)
        check("связь с пультом", True, f"slave 0x{m.addr:02X}")
        check("перед тестом фиксации нет", p["locked"] == 0,
              f"0x3001={p['locked']}")
        if p["locked"]:
            print("\nЭКРАН ЗАМОРОЖЕН предыдущим результатом.")
            print("Нажмите ЛЮБУЮ кнопку на пульте, жду сброса...")
            if not wait_reset(m, args.wait_btn):
                return 2
        diag = read_tjc_diag(m)
        check("диагностика TJC без ошибок", diag["raw"] == 0, f"0x3000=0x{diag['raw']:04X}")

        # ---- одиночные этапы: запускаться, не прогоняя весь цикл ----
        if args.only_reset:
            print("\n=== 5. Сброс фиксации ===")
            print("    Нажмите ЛЮБУЮ кнопку на пульте (ждём до "
                  f"{args.wait_btn:.0f} с)...")
            ok = wait_reset(m, args.wait_btn)
            if ok:
                p = read_peak(m)
                check("после сброса флаг фиксации = 0", p["locked"] == 0)
                check("пики обнулены",
                      p["f"] == 0 and p["l"] == 0 and p["d"] == 0,
                      f"F={p['f']} L={p['l']} D={p['d']}")
            return summary()

        if args.only_visual:
            return stage6(m, interactive=not args.skip_visual)

        # ---------- 1. Порог «испытание идёт» (шум у нуля) ----------
        print("\n=== 1. Шум у нуля НЕ должен фиксировать результат ===")
        print("   (F_PEAK_MIN_FORCE = 0.02: пик ниже — испытание не началось)")
        write_all(m, 0.0, 0.01, 0.0, 0.1)
        time.sleep(0.3)
        write_all(m, 0.1, 0.0, 0.1, 0.2)
        time.sleep(0.5)
        p = read_peak(m)
        check("пик F=0.01 -> фиксации нет", p["locked"] == 0,
              f"0x3001={p['locked']}")

        # ---------- 2. «Плато» у максимума: L/D обновляются ----------
        print("\n=== 2. Сила держится у максимума — L/D продолжают "
              "обновляться, фиксации нет ===")
        L, F, D = ramp(m, 3.0, 3.0, 1.5, 6.0, "рост до F=18")
        p_hold = read_peak(m)
        for i in range(15):
            t = 3.0 + 0.1 * (i + 1)
            write_all(m, 3.0 * t, F, 1.5 * t, t)   # F не меняется, L/D растут
            time.sleep(0.1)
        p = read_peak(m)
        check("на плато фиксации нет", p["locked"] == 0, f"0x3001={p['locked']}")
        check("L на плато продолжил обновляться",
              p["l"] > p_hold["l"] + 1.0,
              f"L_пик: {p_hold['l']:.2f} -> {p['l']:.2f}")

        # ---------- 3. Полный цикл: рост -> спад -> фиксация ----------
        print("\n=== 3. Рост -> спад силы -> фиксация результата ===")
        vL, vD, vF, t_rise = 3.0, 1.5, 6.0, 5.0
        L, F, D = ramp(m, t_rise, vL, vD, vF, "рост 5 с")
        exp = {"f": vF * t_rise, "l": vL * t_rise, "d": vD * t_rise}
        p_top = read_peak(m)
        check("на росте фиксации нет", p_top["locked"] == 0)
        check("максимум силы отслеживается",
              abs(p_top["f"] - exp["f"]) < 0.2 * vF,
              f"Fmax={p_top['f']:.2f}, ждём ~{exp['f']:.2f}")

        # спад силы, L и D продолжают расти
        t0 = time.monotonic()
        locked_t = None
        while time.monotonic() - t0 < 6.0:
            t = t_rise + (time.monotonic() - t0)
            Ff = max(0.0, exp["f"] - vF * (time.monotonic() - t0))
            write_all(m, vL * t, Ff, vD * t, t)
            if locked_t is None and read_peak(m)["locked"]:
                locked_t = t
            time.sleep(0.1)
        p = read_peak(m)
        print(f"    фиксация наступила через {locked_t - t_rise:.2f} с после "
              f"вершины" if locked_t else "    фиксация не наступила")
        check("после спада силы фиксация ЕСТЬ", p["locked"] == 1,
              f"0x3001={p['locked']}")
        check("зафиксирован максимум F", abs(p["f"] - exp["f"]) < 0.2 * vF,
              f"{p['f']:.2f} vs {exp['f']:.2f}")
        check("L зафиксирован в точке максимума F",
              abs(p["l"] - exp["l"]) < 0.35 * vL,
              f"{p['l']:.2f} vs {exp['l']:.2f}")
        check("D зафиксирован в точке максимума F",
              abs(p["d"] - exp["d"]) < 0.35 * vD,
              f"{p['d']:.2f} vs {exp['d']:.2f}")

        # ---------- 4. Заморозка: новые данные на экран не проходят ----------
        print("\n=== 4. После фиксации экран заморожен (новые значения игнор) ===")
        before = read_peak(m)
        ch_before = read_channels(m)
        for i in range(20):
            t = 12.0 + 0.1 * i
            write_all(m, 100.0 + i, 999.0 + i, 200.0 + i, t)
            time.sleep(0.05)
        after = read_peak(m)
        ch_after = read_channels(m)
        check("пики не изменились при новых данных",
              (before["f"], before["l"], before["d"])
              == (after["f"], after["l"], after["d"]),
              f"F {before['f']:.2f}->{after['f']:.2f}, "
              f"L {before['l']:.2f}->{after['l']:.2f}, "
              f"D {before['d']:.2f}->{after['d']:.2f}")
        check("при этом текущие регистры 0x1000 обновились",
              ch_after["L"] != ch_before["L"] and ch_after["F"] != ch_before["F"],
              f"L {ch_before['L']:.2f}->{ch_after['L']:.2f}, "
              f"F {ch_before['F']:.2f}->{ch_after['F']:.2f}")
        diag = read_tjc_diag(m)
        check("диагностика TJC без ошибок после фиксации", diag["raw"] == 0,
              f"0x3000=0x{diag['raw']:04X} busy={diag['busy']} "
              f"ovf={diag['ovf']} err={diag['err']}")

        # ---------- 5. Сброс кнопкой ----------
        if args.skip_reset:
            print("\n=== 5. Сброс фиксации — ПРОПУЩЕН (--skip-reset), "
                  "результат на экране остаётся замороженным ===")
            return summary()
        print("\n=== 5. Сброс фиксации ===")
        print("    Нажмите ЛЮБУЮ кнопку на пульте (ждём до "
              f"{args.wait_btn:.0f} с)...")
        if not wait_reset(m, args.wait_btn):
            print("    СБРОС НЕ ДОЖДАЛИСЬ — дальше нечего проверять.")
            return summary()
        p = read_peak(m)
        check("после сброса флаг фиксации = 0", p["locked"] == 0)
        check("пики обнулены", p["f"] == 0 and p["l"] == 0 and p["d"] == 0,
              f"F={p['f']} L={p['l']} D={p['d']}")

        # ---------- 6. Отрисовка текущих значений + каналы x0..x3 ----------
        stage6(m, interactive=not args.skip_visual)
    finally:
        m.close()
    return summary()


def stage6(m, interactive=True):
    """Устанавливает контрольные значения и сверяет их с экраном."""
    print("\n=== 6. Отрисовка текущих значений (сверка с экраном) ===")
    write_all(m, 11.1, 22.2, 33.3, 44.4)
    time.sleep(1.0)
    ch = read_channels(m)
    diag = read_tjc_diag(m)
    check("значения приняты и читаются обратно",
          abs(ch["L"] - 11.1) < 0.01 and abs(ch["F"] - 22.2) < 0.01
          and abs(ch["D"] - 33.3) < 0.01,
          f"L={ch['L']:.2f} F={ch['F']:.2f} D={ch['D']:.2f} t={ch['t']:.2f}")
    check("TJC жив (нет overflow/err после отрисовки)", diag["raw"] == 0,
          f"0x3000=0x{diag['raw']:04X}")
    print("\n    СМОТРИТЕ НА ЭКРАН ПУЛЬТА — должно быть:")
    print("      x0 (перемещение L) = 11.1")
    print("      x1 (сила F)        = 22.2")
    print("      x2 (деформация D)  = 33.3")
    print("      x3 (время)         = 44.4")
    print("    Если где-то в 10 раз больше/меньше — не совпадает число")
    print("    знаков виджета и TJC_VALUE_SCALE (main.c).")
    if not interactive:
        print("    (визуальная сверка пропущена: --skip-visual)")
        return 0
    try:
        ans = input("    Экран совпадает? [y/n] ").strip().lower()
    except EOFError:
        ans = ""
    check("экран показывает те же числа (визуально)", ans == "y",
          f"ответ оператора: {ans or 'нет ответа'}")
    return 0


def wait_reset(m, timeout):
    """Ждёт, пока флаг фиксации станет 0, и печатает нажатую кнопку."""
    t0 = time.monotonic()
    seen = set()
    while time.monotonic() - t0 < timeout:
        try:
            if read_peak(m)["locked"] == 0:
                if seen:
                    print(f"    сброс выполнен, нажатые кнопки: "
                          f"{'; '.join(sorted(seen))}")
                else:
                    print("    сброс выполнен")
                time.sleep(0.4)
                return True
            a, b, enc = read_buttons(m)
            for bit, nm in PORTA_BITS.items():
                if a & (1 << bit):
                    seen.add("A/" + nm)
            for bit, nm in PORTB_BITS.items():
                if b & (1 << bit):
                    seen.add("B/" + nm)
        except (TimeoutError, ModbusError):
            pass
        time.sleep(0.1)
    print(f"    таймаут {timeout:.0f} с: фиксации не снята")
    return False


def summary():
    passed = sum(1 for _, ok, _ in RESULTS if ok)
    print(f"\n=== ИТОГ: {passed}/{len(RESULTS)} проверок пройдено ===")
    for name, ok, detail in RESULTS:
        if not ok:
            print(f"  НЕ ПРОШЛО: {name}  {detail}")
    return 0 if passed == len(RESULTS) else 1


if __name__ == "__main__":
    sys.exit(main())
