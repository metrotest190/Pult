#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""scenario_DLF.py — сценарий проверки пульта по Modbus RTU (slave 0x0A).

0..--switch (5) с: L, D, F растут одновременно с РАЗНОЙ скоростью.
После: F начинает УМЕНЬШАТЬСЯ, L и D продолжают расти.
Спад F на 2% от максимума -> прошивка фиксирует результат
(регистры 0x3001-0x3007), на экране пульта должны остаться
максимум F и соответствующие L, D.

Все 4 float-канала пишутся ОДНИМ кадром FC16 (0x1000 n=8),
поэтому на экране величины меняются синхронно.

Запуск:  python scenario_DLF.py --port COM13
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from modbus_debug import ModbusRtuMaster, float_to_regs, regs_to_float


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM13")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--addr", type=lambda s: int(s, 0), default=0x0A)
    ap.add_argument("--switch", type=float, default=5.0,
                    help="момент, с которого F начинает падать, с")
    ap.add_argument("--duration", type=float, default=10.0,
                    help="полная длительность сценария, с")
    ap.add_argument("--interval", type=float, default=0.1)
    # скорости роста, ед/с (разные для каждой величины)
    ap.add_argument("--vL", type=float, default=3.0)
    ap.add_argument("--vD", type=float, default=1.5)
    ap.add_argument("--vF", type=float, default=6.0)
    args = ap.parse_args()

    m = ModbusRtuMaster(args.port, args.baud, args.addr, timeout=0.6)
    t0 = time.monotonic()
    print(f"{'t,с':>6} {'L':>9} {'D':>9} {'F':>9}  фаза")
    try:
        while True:
            t = time.monotonic() - t0
            if t > args.duration:
                break
            L = args.vL * t
            D = args.vD * t
            if t <= args.switch:
                F = args.vF * t
                phase = "рост L/D/F"
            else:
                # вершина в момент switch, затем спад со скоростью vF
                F = args.vF * args.switch - args.vF * (t - args.switch)
                phase = "F ПАДАЕТ, L/D растут"
            # один кадр FC16: 0x1000 L, 0x1002 F, 0x1004 D, 0x1006 t
            regs = (float_to_regs(L) + float_to_regs(F)
                    + float_to_regs(D) + float_to_regs(t))
            try:
                m.write_multiple(0x1000, regs)
                print(f"{t:6.1f} {L:9.3f} {D:9.3f} {F:9.3f}  {phase}")
            except Exception as e:
                print(f"{t:6.1f}  ОШИБКА: {e}")
            # шаг с фиксированным периодом
            nxt = t0 + (int(t / args.interval) + 1) * args.interval
            time.sleep(max(nxt - time.monotonic(), 0.002))
    except KeyboardInterrupt:
        pass
    finally:
        m.close()
    print("\nСценарий завершён.")


if __name__ == "__main__":
    sys.exit(main())
