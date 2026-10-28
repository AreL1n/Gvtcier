import re
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent

INFO_FILE = BASE / "版本信息.txt"
RC_FILE = BASE / "src" / "res.rc"
CS_FILE = BASE / "src" / "Lanc.cs"

RC_DEFAULTS = {
    "工作室名": "Li-HuaL1n Software",
    "产品名": "Gvtcier",
    "版权": "Copyright (C) 2026 Li-HuaL1n Software",
    "产品版本": "1.6.0.0",
    "文件版本": "1.6.0.0",
    "原始文件名": "Gvtcier.dll/main.exe",
    "内部名称": "Gvtcier",
    "说明": "Gvtcier",
    "语言": "中文(简体:中国大陆)",
    "LANC说明": "Gvtcier 启动面板",
    "商标": "",
}

LANG_MAP = {
    "中文(简体:中国大陆)": (0x804, 0x04B0, 1200),
}


def parse_info(path):
    values = dict(RC_DEFAULTS)
    text = path.read_text(encoding="utf-8-sig")
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        key, _, val = line.partition("=")
        key = key.strip()
        val = val.strip()
        if key in values and val:
            values[key] = val
    return values


def version_tuple(v):
    parts = re.findall(r"\d+", v)
    while len(parts) < 4:
        parts.append("0")
    return ",".join(parts[:4])


def set_value(text, key, value):
    pattern = re.compile(r'(VALUE\s+"%s"\s*,\s*")[^"]*(")' % re.escape(key))
    new, n = pattern.subn(lambda m: m.group(1) + value + m.group(2), text)
    if n == 0:
        raise RuntimeError("res.rc 中未找到字段：" + key)
    return new


def set_optional(text, key, value, anchor):
    pattern = re.compile(r'\s*VALUE\s+"%s"\s*,\s*"[^"]*"\s*\n' % re.escape(key))
    if value:
        if pattern.search(text):
            return pattern.sub(lambda m: '\n            VALUE "%s", "%s"\n' % (key, value), text, count=1)
        return text.replace(anchor, anchor + '\n            VALUE "%s", "%s"' % (key, value), 1)
    return pattern.sub("\n", text)


def build_rc(info):
    text = RC_FILE.read_text(encoding="utf-8")
    fv = version_tuple(info["文件版本"])
    pv = version_tuple(info["产品版本"])

    lang = LANG_MAP.get(info["语言"])
    if lang is None:
        raise RuntimeError("未知的语言写法：" + info["语言"])
    langid, sublang, codepage = lang
    block = "%04x%04x" % (langid, sublang)

    text = re.sub(r"(FILEVERSION\s+)\S+", lambda m: m.group(1) + fv, text, count=1)
    text = re.sub(r"(PRODUCTVERSION\s+)\S+", lambda m: m.group(1) + pv, text, count=1)

    text = re.sub(r'(BLOCK\s+")[0-9a-fA-F]{8}(")', lambda m: m.group(1) + block + m.group(2), text, count=1)
    text = re.sub(r'(VALUE\s+"Translation"\s*,\s*)0x[0-9a-fA-F]+\s*,\s*\d+',
                  lambda m: m.group(1) + "0x%x, %d" % (langid, codepage), text, count=1)

    text = set_value(text, "CompanyName", info["工作室名"])
    text = set_value(text, "ProductName", info["产品名"])
    text = set_value(text, "LegalCopyright", info["版权"])
    text = set_value(text, "FileVersion", info["文件版本"])
    text = set_value(text, "ProductVersion", info["产品版本"])
    text = set_value(text, "FileDescription", info["说明"])
    text = set_value(text, "InternalName", info["内部名称"])
    text = set_value(text, "OriginalFilename", info["原始文件名"])

    text = set_optional(text, "LegalTrademarks", info["商标"],
                        'VALUE "LegalCopyright", "%s"' % info["版权"])

    text = set_language(text, info["语言"])

    return text


def set_language(text, lang):
    if lang not in LANG_MAP:
        raise RuntimeError("未知语言：" + lang + "，可选：" + "/".join(LANG_MAP))
    lang_id, codepage, cp = LANG_MAP[lang]
    block = "%04x%04x" % (lang_id, codepage)
    text = re.sub(r'(BLOCK\s+")[0-9a-fA-F]{8}(")', lambda m: m.group(1) + block + m.group(2), text)
    text = re.sub(r'(VALUE\s+"Translation"\s*,\s*)0x[0-9a-fA-F]+\s*,\s*\d+',
                  lambda m: "%s0x%x, %d" % (m.group(1), lang_id, cp), text)
    return text


def build_cs(info):
    text = CS_FILE.read_text(encoding="utf-8")
    pairs = [
        ("AssemblyCompany", info["工作室名"]),
        ("AssemblyProduct", info["产品名"]),
        ("AssemblyCopyright", info["版权"]),
        ("AssemblyVersion", info["产品版本"]),
        ("AssemblyFileVersion", info["文件版本"]),
        ("AssemblyTitle", info["LANC说明"]),
        ("AssemblyDescription", info["LANC说明"]),
    ]
    for name, value in pairs:
        pattern = re.compile(r'(\[assembly:\s*%s\(")[^"]*("\)\])' % name)
        text, n = pattern.subn(lambda m: m.group(1) + value + m.group(2), text)
        if n == 0:
            raise RuntimeError("Lanc.cs 中未找到特性：" + name)
    return text


def main(argv):
    if not INFO_FILE.exists():
        print("找不到：" + str(INFO_FILE))
        return 1

    info = parse_info(INFO_FILE)
    rc = build_rc(info)
    cs = build_cs(info)

    RC_FILE.write_text(rc, encoding="utf-8")
    CS_FILE.write_text(cs, encoding="utf-8")

    print("已写入版本信息：")
    print("  工作室名 = " + info["工作室名"])
    print("  产品名   = " + info["产品名"])
    print("  版权     = " + info["版权"])
    print("  版本     = " + info["产品版本"])
    print("  更新文件 = src/res.rc, src/Lanc.cs")
    print("[同步成功 | COPY OK]")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
