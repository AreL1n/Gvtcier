import argparse
import json
import os
import struct
import sys
import zlib

GLAC_MAGIC = 0x43414C47
GLAC_VERSION = 1

ALLOWED_EXT = {".json", ".png", ".wav", ".mp3", ".obj", ".glb", ".txt", ".mp4", ".gvts", ".gmap", ".ghook"}


def build_glc(meta_json, entries):
    blobs = [(store, open(real, "rb").read()) for real, store in entries]
    meta = meta_json.encode("utf-8")
    toc_size = 12 + len(meta) + 4
    for store, _data in blobs:
        toc_size += 2 + len(store.encode("utf-8")) + 20
    offset = toc_size
    toc = struct.pack("<III", GLAC_MAGIC, GLAC_VERSION, len(meta)) + meta
    toc += struct.pack("<I", len(blobs))
    for store, data in blobs:
        name = store.encode("utf-8")
        toc += struct.pack("<H", len(name)) + name
        toc += struct.pack("<QQI", offset, len(data), zlib.crc32(data) & 0xFFFFFFFF)
        offset += len(data)
    return toc + b"".join(data for _store, data in blobs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="output glc path")
    ap.add_argument("--meta", default=None, help="mod.json to embed")
    ap.add_argument("--entry", action="append", default=[], help="realpath|storename")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    entries = []
    seen = set()

    def add(real, store):
        real = os.path.abspath(real)
        if store in seen:
            return
        if not os.path.isfile(real):
            print("[mod] 缺失 " + real)
            return
        ext = os.path.splitext(store)[1].lower()
        if ext not in ALLOWED_EXT:
            print("[mod] 扩展名不在白名单 " + store)
            return
        seen.add(store)
        entries.append((real, store))

    meta_text = "{}"
    if args.meta and os.path.isfile(args.meta):
        with open(args.meta, encoding="utf-8") as f:
            meta_text = f.read().strip() or "{}"
    for item in args.entry:
        real, _, store = item.partition("|")
        add(real, store)

    if not entries:
        print("[mod] 无可打包内容")
        return 1

    data = build_glc(meta_text, entries)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "wb") as f:
        f.write(data)
    print("[mod] 生成 %s %d 字节, 含 %d 项" % (out, len(data), len(entries)))
    for _real, store in entries:
        print("      " + store)
    return 0


if __name__ == "__main__":
    sys.exit(main())
