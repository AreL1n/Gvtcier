import argparse
import os
import re
import struct
import sys
import zlib

ENGINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROOT = os.path.dirname(ENGINE)
RC = os.path.join(ENGINE, "res.rc")

GPK_MAGIC = 0x4B415047
GPK_VERSION = 1

RES_ALIAS = {
    "FONT_REGULAR": "FONT_REGULAR.otf",
    "FONT_BOLD": "FONT_BOLD.otf",
}


def build_gpk(entries):
    blobs = [(store, open(real, "rb").read()) for real, store in entries]
    toc_size = 12
    for store, _data in blobs:
        toc_size += 2 + len(store.encode("utf-8")) + 20
    offset = toc_size
    toc = struct.pack("<III", GPK_MAGIC, GPK_VERSION, len(blobs))
    for store, data in blobs:
        name = store.encode("utf-8")
        toc += struct.pack("<H", len(name)) + name
        toc += struct.pack("<QQI", offset, len(data), zlib.crc32(data) & 0xFFFFFFFF)
        offset += len(data)
    return toc + b"".join(data for _store, data in blobs)


def parse_rc():
    entries = []
    pat = re.compile(r'^([A-Za-z0-9_]+)\s+RCDATA\s+"([^"]+)"')
    for rc_name in ("res.rc", "vein_gen.rc", "audio_gen.rc"):
        rc_path = os.path.join(os.path.dirname(RC), rc_name)
        try:
            with open(rc_path, "r", encoding="utf-8") as f:
                for line in f:
                    m = pat.match(line.strip())
                    if m:
                        entries.append((m.group(1), m.group(2)))
        except OSError:
            pass
    return entries


def engine_entries():
    out = []
    for res_name, src_rel in parse_rc():
        src = os.path.join(ROOT, src_rel.replace("/", os.sep))
        if not os.path.isfile(src):
            print("[res] 缺失 " + src_rel)
            continue
        out.append((src, os.path.basename(src_rel)))
        logical = res_name.lower()
        out.append((src, logical))
        ext = os.path.splitext(src_rel)[1].lower()
        if ext:
            out.append((src, logical + ext))
        if res_name in RES_ALIAS:
            out.append((src, RES_ALIAS[res_name]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="output gpk path")
    ap.add_argument("--meta", default=None, help="meta.json to embed")
    ap.add_argument("--entry", action="append", default=[],
                    help="realpath|storename")
    ap.add_argument("--no-engine", action="store_true", help="skip engine resources")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    entries = []
    seen = set()

    def add(real, store):
        real = os.path.abspath(real)
        if store in seen:
            return
        if not os.path.isfile(real):
            print("[res] 缺失 " + real)
            return
        seen.add(store)
        entries.append((real, store))

    if args.meta:
        add(args.meta, "meta.json")
    for item in args.entry:
        real, _, store = item.partition("|")
        add(real, store)
    if not args.no_engine:
        for real, store in engine_entries():
            add(real, store)

    if not entries:
        print("[res] 无可打包内容")
        return 1

    data = build_gpk(entries)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "wb") as f:
        f.write(data)
    print("[res] 生成 %s %d 字节, 含 %d 项" % (out, len(data), len(entries)))
    for _real, store in entries:
        print("      " + store)
    return 0


if __name__ == "__main__":
    sys.exit(main())
