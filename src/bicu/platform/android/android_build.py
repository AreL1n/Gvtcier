import argparse
import os
import subprocess
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROOT = os.path.dirname(ENGINE)
SDK = os.path.join(ROOT, "tools", "AndroidSDK")
NDK = os.path.join(SDK, "ndk", "27.0.12077973")

ABI = "arm64-v8a"
API = 24
TOOLCHAIN = os.path.join(NDK, "toolchains", "llvm", "prebuilt", "windows-x86_64")
CLANG = os.path.join(TOOLCHAIN, "bin", "clang++")
TRIPLE = "aarch64-linux-android"

ENGINE_EXCLUDE = {
    "platform/gvw_win32.cpp",
    "platform/android/Gvtcier安卓入口.cpp",
    "core/入口.cpp",
    "core/引擎运行.cpp",
    "core/资源.cpp",
    "core/宽字符.cpp",
    "core/扩展.cpp",
    "core/崩溃报告.cpp",
    "资源.cpp",
    "audio/最小音频库.c",
    "audio/音频.cpp",
    "script/bind/audio/音频.cpp",
    "sandbox/沙盒.cpp",
    "interface/接口层.cpp",
    "interface/界面.cpp",
    "interface/通行证.cpp",
    "render/设置界面.cpp",
    "render/菜单.cpp",
    "rhi/gl/GL后端.cpp",
    "rhi/gl/图形4.cpp",
    "script/bind/net/网络.cpp",
    "net/网络.cpp",
    "net/网络套接字.cpp",
    "net/网络同步.cpp",
    "net/网络节点.cpp",
    "GvtcierScript/解释器.c",
    "GvtcierScript/编译器.c",
}


def discover_engine_srcs():
    out = []
    src = os.path.join(ENGINE)
    for dirpath, _dirnames, filenames in os.walk(src):
        for fn in filenames:
            if not fn.endswith((".cpp", ".c", ".cc", ".cxx")):
                continue
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, src).replace("\\", "/")
            if rel.startswith("GvtcierScript/") and not rel.endswith(".c"):
                continue
            if rel in ENGINE_EXCLUDE:
                continue
            out.append(rel)
    return sorted(out)


def run(cmd, desc):
    print("[android] " + desc)
    r = subprocess.run(cmd, capture_output=True, text=True, errors="ignore")
    if r.returncode != 0:
        print(r.stdout[-8000:])
        print(r.stderr[-8000:])
        print("[android] FAIL: " + desc)
        sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--demo", required=True, help="game source dir")
    ap.add_argument("--lib", required=True, help="output .so base name")
    ap.add_argument("--objdir", default=None)
    ap.add_argument("--outdir", default=None)
    args = ap.parse_args()

    demo = os.path.abspath(args.demo)
    objdir = args.objdir or os.path.join(ROOT, ".android", "obj")
    outdir = args.outdir or os.path.join(ROOT, ".android", "lib", ABI)
    os.makedirs(objdir, exist_ok=True)
    os.makedirs(outdir, exist_ok=True)

    sources = [
        os.path.join(ENGINE, "platform", "android", "Gvtcier安卓入口.cpp"),
        os.path.join(ROOT, ".android", "stb_impl.cpp"),
    ]
    demo_entry = os.path.join(demo, "android_main.cpp")
    if os.path.exists(demo_entry):
        sources.append(demo_entry)
    engine_srcs = [os.path.join(ENGINE, s) for s in discover_engine_srcs()]

    incs = [ENGINE, os.path.join(ENGINE, "rhi"), os.path.join(ENGINE, "platform"),
            os.path.join(ENGINE, "GvtcierScript"), os.path.join(ENGINE, "physics", "third"), demo]

    objs = []
    for src in sources + engine_srcs:
        if not os.path.exists(src):
            continue
        rel = os.path.relpath(src, ENGINE).replace("\\", "_").replace("/", "_")
        obj = os.path.join(objdir, rel + ".o")
        objs.append(obj)
        if os.path.exists(obj) and os.path.getmtime(obj) >= os.path.getmtime(src):
            continue
        cmd = [CLANG,
               "-target", "%s%d" % (TRIPLE, API),
               "-std=c++17", "-O2", "-fPIC", "-ffast-math", "-Wno-c++11-narrowing",
               "-DGVWCIER_USE_GLES", "-D__ANDROID__"]
        for inc in incs:
            cmd += ["-I" + inc]
        cmd += ["-c", src, "-o", obj]
        run(cmd, "compile " + rel)

    so = os.path.join(outdir, "lib" + args.lib + ".so")
    link = [CLANG,
            "-target", "%s%d" % (TRIPLE, API),
            "-shared", "-fPIC",
            "-Wl,-z,max-page-size=16384",
            "-static-libstdc++",
            "-u", "ANativeActivity_onCreate",
            "-landroid", "-lEGL", "-lGLESv3", "-llog",
            "-o", so] + objs
    run(link, "link " + os.path.basename(so))
    print("[android] BUILD OK -> " + so)


if __name__ == "__main__":
    main()
