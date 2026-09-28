#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Глубокий разбор прошивки экрана TJC (.HMI): компоненты, атрибуты, коды.

Кириллица в argv через git-bash пробрасывается битая, поэтому путь к файлу
задаётся литералом в этом файле (или переменной окружения HMI_FW_PATH).

    python -X utf8 hmi_dump.py            # компоненты + xfloat-поля
    python -X utf8 hmi_dump.py --codes     # листинг скриптов страниц
    python -X utf8 hmi_dump.py --hdr       # копнуть .h-экспорт имён
"""
import os
import re
import shutil
import sys

SRC_DIR = r"C:\Users\190\Desktop\Пульт\Большой\Screen"
DEF_HMI = os.path.join(SRC_DIR, "V3.0.HMI")
DEF_HDR = os.path.join(SRC_DIR, "V1.4.1.h")
MIRROR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hmi_fw")

# Типы компонентов TJC/HMI (байт кода -> имя)
TYPE_NAMES = {
    0x00: "page", 0x01: "text(t)", 0x02: "button(b)", 0x03: "xorbutton",
    0x04: "number(n)", 0x05: "picture(p)", 0x06: "crop", 0x07: "crc",
    0x09: "buttonex", 0x0A: "dsq", 0x0B: "timer(tm)", 0x0C: "variable",
    0x0D: "scale", 0x0E: "slider", 0x0F: "gauge", 0x10: "file",
    0x11: "zm", 0x12: "progress", 0x13: "checkbox",
    0x3B: "xfloat(x)", 0x3C: "xstr", 0x3D: "xnum",
    0x6A: "slider2", 0x6D: "checkbox2", 0x74: "pw",
}


def load(path):
    with open(path, "rb") as f:
        return f.read()


def clean(v):
    return v.strip("\x00\x01\x02\x03\x04\x05 .")


def parse_attrs(data):
    """Атрибутные блоки 'att-N.' -> список словарей."""
    blocks = []
    for m in re.finditer(rb"att-(\d+)\x00", data):
        seg = data[m.end():m.end() + 4000]
        end = seg.find(b"codesdown")
        if end < 0:
            end = len(seg)
        parts = [p.decode("ascii", "replace")
                 for p in seg[:end].split(b"\x00") if clean(p.decode("ascii", "replace"))]
        d = {}
        i = 0
        while i + 1 < len(parts):
            k = clean(parts[i])
            if re.fullmatch(r"[a-z_][a-z0-9_]{1,12}", k):
                d[k] = clean(parts[i + 1])
                i += 2
            else:
                i += 1
        d["_off"] = m.start()
        d["_att"] = int(m.group(1))
        blocks.append(d)
    return blocks


def dump_components(blocks):
    hdr = ("name", "type", "vvs1", "vvs0", "vvs", "w", "h", "x", "y",
           "vscope", "font", "txt_maxl", "val")
    print("%-10s %-12s %-5s %-5s %-5s %-5s %-5s %-5s %-5s %-8s %-5s %-8s %s" % hdr)
    for d in blocks:
        nm = d.get("objname", "")
        if not re.fullmatch(r"[a-z]{1,2}\d{1,3}|show|loadpageid|loadcmpid", nm or ""):
            continue
        code = d.get("type", "?")
        tn = TYPE_NAMES.get(ord(code[0]), repr(code[:1])) if code else "?"
        row = [nm, tn, d.get("vvs1", "-"), d.get("vvs0", "-"), d.get("vvs", "-"),
               d.get("w", "-"), d.get("h", "-"), d.get("x", "-"), d.get("y", "-"),
               d.get("vscope", "-"), d.get("font", "-"), d.get("txt_maxl", "-"),
               d.get("val", "-")]
        print("%-10s %-12s %-5s %-5s %-5s %-5s %-5s %-5s %-5s %-8s %-5s %-8s %s"
              % tuple(str(v) for v in row))


def dump_codes(data):
    for m in re.finditer(rb"codes(load|loadend|up|down|unload)-\d*\x00", data):
        seg = data[m.end():m.end() + 2500]
        stop = re.search(rb"codes(loadend|down|up|unload|load)-", seg[1:])
        end = (stop.start() + 1) if stop else 600
        body = seg[:end].decode("latin-1")
        body = re.sub(r"[\x00-\x08\x0b-\x1f\x7f]+", " ", body)
        body = re.sub(r"\s+", " ", body).strip()
        if len(body) < 4:
            continue
        print("--- 0x%08X %s ---" % (m.start(), m.group().rstrip(b"\x00").decode()))
        print(body[:1500])
        print()


def mirror(path):
    os.makedirs(MIRROR, exist_ok=True)
    dst = os.path.join(MIRROR, os.path.basename(path))
    try:
        shutil.copy2(path, dst)
        print(f"Копия: {dst}")
    except OSError as e:
        print(f"Копирование не удалось ({e}) — читаю оригинал напрямую")
    return path


def main():
    args = sys.argv[1:]
    hmi = os.environ.get("HMI_FW_PATH", DEF_HMI)
    if "--dump-hdr" in args:
        p = mirror(DEF_HDR)
        print(open(p, encoding="utf-8", errors="replace").read())
        return 0
    hmi = mirror(hmi)
    data = load(hmi)
    print(f"Файл: {hmi}\nРазмер: {len(data)} байт\n")
    blocks = parse_attrs(data)
    print(f"Атрибутных блоков: {len(blocks)}\n")
    dump_components(blocks)
    x3 = [d for d in blocks if d.get("objname") == "x3"]
    print(f"\nКомпонент 'x3' найден: {bool(x3)}")
    names = sorted({d.get("objname", "") for d in blocks
                    if d.get("objname", "").startswith("x")})
    print(f"Имена x*: {names}")
    if "--codes" in args:
        print("\n================ КОДЫ СТРАНИЦ ================")
        dump_codes(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())