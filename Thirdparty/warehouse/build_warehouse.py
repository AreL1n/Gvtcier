import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
MSVC = Path(r"D:\Code\Local_tool_library\MSVC")
VULKAN_LIB = Path(r"D:\Code\Local_tool_library\Vulkan\Lib")
OUT = ROOT / "sdk"
# 第三方运行时统一放 sdk/Gvtcier_Data，SDK 根目录只留引擎与可执行
LIBS_OUT = OUT / "Gvtcier_Data"
BUILD = ROOT / ".build" / "warehouse"

SOURCES = [
    "warehouse.cpp",
]

GLSLANG_LIBS = [
    "glslang.lib",
    "SPIRV.lib",
    "MachineIndependent.lib",
    "GenericCodeGen.lib",
    "OSDependent.lib",
    "glslang-default-resource-limits.lib",
    "SPIRV-Tools.lib",
    "SPIRV-Tools-opt.lib",
]

INCLUDES = [
    ROOT / "Thirdparty" / "glslang",
    ROOT / "Thirdparty" / "glslang" / "glslang",
    ROOT / "Thirdparty" / "glslang" / "SPIRV",
]


def find_vcvars():
    p = MSVC / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    if not p.exists():
        sys.exit(f"找不到 vcvars64.bat: {p}")
    return p


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    LIBS_OUT.mkdir(parents=True, exist_ok=True)

    vcvars = find_vcvars()
    src_dir = ROOT / "Thirdparty" / "warehouse"

    cmd = f'call "{vcvars}" >nul && cl /nologo /utf-8 /std:c++17 /O2 /EHsc /MD /DWAREHOUSE_BUILD '
    cmd += "/DNDEBUG /D_CRT_SECURE_NO_WARNINGS "
    for inc in INCLUDES:
        cmd += f'/I "{inc}" '
    cmd += f'/I "{src_dir}" '
    cmd += f"/Fo{BUILD}{os.sep} /Fd{BUILD}{os.sep} "
    cmd += f'/LD '
    for s in SOURCES:
        cmd += f'"{src_dir / s}" '
    cmd += f'/Fe:"{LIBS_OUT / "warehouse.dll"}" /link /LIBPATH:"{VULKAN_LIB}" '
    # 导入库和导出文件都是编译中间物，扔进 .build 别占 dist
    cmd += f'/IMPLIB:"{BUILD / "warehouse.lib"}" '
    for lib in GLSLANG_LIBS:
        cmd += f"{lib} "
    cmd += "user32.lib"

    print("[warehouse] 编译中...")
    r = subprocess.run(cmd, shell=True, cwd=str(ROOT))
    if r.returncode != 0:
        sys.exit(f"[warehouse] 编译失败 {r.returncode}")
    # 链接器产出的 .exp 默认落在 dll 边上，挪走
    for d in (LIBS_OUT, OUT):
        exp = d / "warehouse.exp"
        if exp.exists():
            exp.replace(BUILD / "warehouse.exp")
    # dist 根上残留的旧副本一并清掉，免得加载到过时的那份
    stale = OUT / "warehouse.dll"
    if stale.exists():
        stale.unlink()
    print(f"[warehouse] 完成: {LIBS_OUT / 'warehouse.dll'}")


if __name__ == "__main__":
    main()
