#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Разбор прошивки экрана TJC (.HMI).

TJC Nextion/HMI-прошивка бинарна, но имена страниц, ID/имена компонентов,
их атрибуты и скрипты событий страниц хранятся в ней открытым ASCII.
Скрипт вытаскивает эти строки и ищет интересующие нас виджеты/атрибуты.

Использование:
    python hmi_analyze.py <путь.HMI> [--grep шаблон] [--ctx 3] [--tokens]
"""
import argparse
import re
import sys

TOKENS = [
    b"preinitialize", b"initialize", b"tup", b"tdown", b"postinitialize",
    b"refresh", b"vvs", b"vvs0", b"vvs1", b"vvs2", b"vvs3",
    b"wco", b"wcf", b"wid", b"bco", b"pco", b"bld", b"brs", b"height",
    b"xfloat", b"xstr", b"xnum", b"nfloat", b"nstr", b"nnum",
    b"page", b"com_", b"baud", b"dims", b"bkcmd", b"sys0", b"sys1", b"sys2",
    b"REC0", b"sendme", b"ref_", b"vis_",
]


def strings(data, minlen=4):
    """ASCII-строки >= minlen с абсолютными смещениями."""
    return [(m.start(), m.group().decode("ascii", "replace"))
            for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, data)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--grep", default=None,
                    help="регулярка: показывать только строки-совпадения")
    ap.add_argument("--ctx", type=int, default=2,
                    help="показывать N соседних строк вокруг совпадения")
    ap.add_argument("--tokens", action="store_true",
                    help="сводка: сколько раз встретились ключевые токены")
    ap.add_argument("--min", type=int, default=4)
    args = ap.parse_args()

    with open(args.path, "rb") as f:
        data = f.read()
    print(f"Файл: {args.path}  размер: {len(data)} байт")

    if args.tokens:
        print("\n=== Сводка по токенам ===")
        for t in TOKENS:
            n = data.count(t)
            if n:
                print(f"  {t.decode():16s} {n}")
        return 0

    strs = strings(data, args.min)
    print(f"Найдено ASCII-строк (len>={args.min}): {len(strs)}")

    if args.grep:
        pat = re.compile(args.grep, re.IGNORECASE)
        hits = [i for i, (_, s) in enumerate(strs) if pat.search(s)]
        print(f"Совпадений по /{args.grep}/: {len(hits)}\n")
        shown = set()
        for i in hits:
            for j in range(max(0, i - args.ctx),
                           min(len(strs), i + args.ctx + 1)):
                if j in shown:
                    continue
                shown.add(j)
                off, s = strs[j]
                mark = ">>" if j == i else "  "
                print(f"{mark} 0x{off:08X}: {s[:160]}")
            print("-" * 40)
        return 0

    for off, s in strs[:400]:
        print(f"0x{off:08X}: {s[:160]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())