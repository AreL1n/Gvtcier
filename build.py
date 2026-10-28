import os
import shutil
import subprocess
import sys
import time
from datetime import datetime
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

BASE = Path(__file__).resolve().parent
MINGW = BASE / "tools" / "Mingw" / "bin"
GXX = MINGW / "g++.exe"
WINDRES = MINGW / "windres.exe"
DIST = BASE / "dist"
BUILD = BASE / ".build"
# SDK 是开发者侧的分发素材库，游戏打包工具从这里取文件。
# 引擎、宿主、启动器、调试器、第三方运行时全都落在这一处
SDK = BASE / "sdk"

ENGINE_EXTRA = [
    "src/bicu/audio/最小音频库.c",
]

TOOL_BASE = {"入口.cpp": "src/bicu/core"}

TOOL_EXES = {"Gvt调试器.exe": "src/bicu/sandbox/沙盒.cpp"}

LAUNCHER_SRC = BASE / "src" / "Lanc.cs"
GVM_SRC = BASE / "src" / "gvm" / "Gvm.cs"
CSC = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Microsoft.NET" / "Framework64" / "v4.0.30319" / "csc.exe"

LUA_EXCLUDE = {"解释器.c", "编译器.c", "onelua.c"}

ENGINE_CPP_INCS = ["-I", "src", "-I", "include", "-I", "src/GvtcierScript", "-I", "Thirdparty", "-I", "tools/Vulkan/Include",
                   "-I", "D:/Code/Local_tool_library/Vulkan/Include",
                   "-I", "D:/Code/Local_tool_library/Vulkan/Include/glslang"]

# Vulkan SDK 的导入库。glslang 走运行时动态加载，不吃这里的 .lib，
# 因为那是 MSVC 编的 C++ 库，MinGW 链不上
VULKAN_LIB_DIR = "D:/Code/Local_tool_library/Vulkan/Lib"
VULKAN_LIBS = ["-lvulkan-1"]

SRC_EXTS = (".cpp", ".cc", ".cxx", ".c")

GREEN = "\033[92m"
YELLOW = "\033[93m"
WHITE = "\033[97m"
RED = "\033[91m"
GRAY = "\033[90m"
RESET = "\033[0m"


def color(text, c):
    if os.environ.get("NO_COLOR") or not sys.stdout.isatty():
        return text
    return f"{c}{text}{RESET}"


STATUS_ORDER = ["编译", "增量", "跳过", "链接", "错误"]

STATUS_PRIORITY = {
    "跳过": 0,
    "增量": 1,
    "编译": 2,
    "链接": 3,
    "错误": 4,
}

STATUS_COLORS = {
    "编译": GREEN,
    "增量": YELLOW,
    "跳过": YELLOW,
    "链接": WHITE,
    "错误": RED,
}


class StatusFolder:
    def __init__(self, threshold=5):
        self.threshold = threshold
        self.run_status = None
        self.run_items = []

    def emit(self, status, filename):
        if status != self.run_status:
            self._flush_run()
            self.run_status = status
            self.run_items = [filename]
        else:
            self.run_items.append(filename)

    def flush(self):
        self._flush_run()

    def _flush_run(self):
        if self.run_status is None:
            return
        n = len(self.run_items)
        if n > self.threshold:
            print(f"  {color('[折叠]', GRAY)} "
                  f"{color(f'已折叠 {n} 个正在[{self.run_status}]的文件', GRAY)}")
        else:
            tag = self._tag(self.run_status)
            for name in self.run_items:
                print(f"  {tag} {name}")
        self.run_status = None
        self.run_items = []

    @staticmethod
    def _tag(status):
        return color(f"[{status}]", STATUS_COLORS[status])


def auto_jobs():
    try:
        total = os.cpu_count() or 1
    except Exception:
        total = 1
    jobs = total - 2
    if jobs < 1:
        jobs = 1
    return jobs


JOBS = auto_jobs()
FOLDER = StatusFolder(threshold=5)


def discover_engine_srcs():
    out = []
    # Jolt 已经搬进 include，源码跟公共头放一起；SPIRV-Cross 还在 Thirdparty
    roots = [BASE / "src", BASE / "include" / "Jolt", BASE / "Thirdparty" / "SPIRV-Cross"]
    for root in roots:
        if not root.is_dir():
            continue
        for p in sorted(root.rglob("*")):
            if p.suffix not in SRC_EXTS:
                continue
            rel = p.relative_to(BASE).as_posix()
            # include 下只收 Jolt 的源码，别的公共头目录不参与编译
            if rel.startswith("include/") and not rel.startswith("include/Jolt/"):
                continue
            # Thirdparty 下只挑明确要编的那几个，其余一律不碰
            if rel.startswith("Thirdparty/") and not rel.startswith("Thirdparty/SPIRV-Cross/"):
                continue
            # SPIRV-Cross 只要根目录那几个反射/解析源文件，samples 与 tests 全都不要；
            # main.cpp 是它自带的命令行工具入口，带上就会跟沙盒抢 main
            if rel.startswith("Thirdparty/SPIRV-Cross/"):
                if p.parent != (BASE / "Thirdparty" / "SPIRV-Cross"):
                    continue
                if p.name == "main.cpp":
                    continue
            if rel.startswith("src/GvtcierScript/"):
                continue
            if rel.startswith("src/tool/"):
                continue
            if rel == "src/bicu/audio/最小音频库.c":
                continue
            if rel == "src/资源.cpp":
                continue
            if rel == "src/bicu/core/资源_android.cpp":
                continue
            if rel == "src/bicu/script/bind/绑定_android.cpp":
                continue
            if rel == "src/bicu/platform/android/Gvtcier安卓入口.cpp":
                continue
            if p.name in TOOL_BASE:
                continue
            out.append(rel)
    out += ENGINE_EXTRA
    return out


def discover_lua_srcs():
    d = BASE / "src" / "GvtcierScript"
    if not d.is_dir():
        return []
    return [p.relative_to(BASE).as_posix()
            for p in sorted(d.glob("*.c")) if p.name not in LUA_EXCLUDE]


def run(cmd, quiet=True):
    if not quiet:
        print("  " + " ".join(str(a) for a in cmd))
    env = os.environ.copy()
    env["TMP"] = str(BUILD)
    env["TEMP"] = str(BUILD)
    r = subprocess.run([str(a) for a in cmd],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       env=env)
    if r.returncode != 0:
        out = r.stdout.decode("utf-8", errors="replace").replace("\ufffd", "?")
        out = "".join(ch if ord(ch) < 0x2500 else "?" for ch in out)
        log_path = BASE / "build_error.log"
        try:
            log_path.write_text(out, encoding="utf-8")
        except OSError:
            pass
        print(color("[错误]", RED) + " 命令失败:")
        print(out.encode("ascii", errors="replace").decode("ascii"))
        print(color("[编译失败 | BUILD FAIL]", RED) + f" 详细日志: {log_path}")
        sys.exit(1)


def newer(src, obj, build_dir, extra_deps=None, dep_file_required=True):
    if not obj.exists():
        return True
    try:
        src_mtime = Path(src).stat().st_mtime
    except OSError:
        return True
    if src_mtime > obj.stat().st_mtime:
        return True
    if extra_deps and any_dep_newer(extra_deps, obj):
        return True
    if not dep_file_required:
        return False
    dep_file = build_dir / (Path(obj).stem + ".d")
    if not dep_file.exists():
        return True
    try:
        with open(dep_file, encoding="utf-8", errors="ignore") as f:
            for line in f:
                line = line.rstrip()
                if not line or line.endswith(":"):
                    continue
                for tok in line.split():
                    tok = tok.strip()
                    if not tok or tok == "\\":
                        continue
                    try:
                        if Path(tok).stat().st_mtime > obj.stat().st_mtime:
                            return True
                    except OSError:
                        pass
    except OSError:
        return True
    return False


def rc_deps(rc):
    deps = []
    try:
        text = Path(rc).read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return deps
    for tok in text.replace("\n", " ").split():
        t = tok.strip().strip('"')
        if not t or t.endswith(":"):
            continue
        if t.startswith(("RCDATA", "ICON", "RT_MANIFEST", "FONT", "PNG")):
            continue
        if "." not in t:
            continue
        if (BASE / t).exists():
            deps.append(str(BASE / t))
    vein_dir = BASE / "assets" / "Vein"
    if vein_dir.is_dir():
        deps.append(str(BASE / "include" / "vein_gen.gen.h"))
    audio_dir = BASE / "assets" / "Audio"
    if audio_dir.is_dir():
        deps.append(str(BASE / "include" / "audio_gen.gen.h"))
        for sub in ("snd", "long"):
            d = audio_dir / sub
            if not d.is_dir():
                continue
            for p in d.glob("*.wav"):
                deps.append(str(p))
            for p in d.glob("*.mp3"):
                deps.append(str(p))
    return deps


def sync_shaders():
    table = BASE / "Shader" / "Shader_table.txt"
    if not table.is_file():
        print(color("[错误] 缺少 Shader/Shader_table.txt", RED))
        sys.exit(1)
    rows = []
    seen = {}
    for raw in table.read_text(encoding="utf-8-sig").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 2 or not parts[0].isdigit():
            print(color(f"[错误] Shader_table.txt 行格式无效: {raw}", RED))
            sys.exit(1)
        idx = int(parts[0])
        path = parts[1].replace("\\", "/")
        if idx in seen:
            print(color(f"[错误] Shader 编号冲突 {idx}: {seen[idx]} 与 {path}", RED))
            sys.exit(1)
        if not (BASE / "Shader" / path).is_file():
            print(color(f"[错误] Shader 文件不存在: Shader/{path}", RED))
            sys.exit(1)
        seen[idx] = path
        rows.append((idx, path))
    rows.sort()
    gen = "".join("    { %d, \"Shader/%s\" },\n" % (i, p) for i, p in rows)
    header = ("//说明：本文件由构建时自动生成，请勿手工编辑。\n"
              "//内容是着色器编号与源文件路径的对照表，构建时据此把着色器打进资源段。\n"
              "#pragma once\n"
              "struct GvShaderEntry { int id; const char* file; };\n"
              "static const GvShaderEntry kGvShaders[] = {\n" + gen + "};\n"
              "static const int kGvShaderCount = (int)(sizeof(kGvShaders) / sizeof(kGvShaders[0]));\n")
    h_file = BASE / "include" / "shader_gen.gen.h"
    if not h_file.exists() or h_file.read_text(encoding="utf-8") != header:
        h_file.write_text(header, encoding="utf-8", newline="\n")
        print("[shader] 已更新 shader_gen.gen.h (%d 项)" % len(rows))


def sync_vein():
    vein_dir = BASE / "assets" / "Vein"
    rows = []
    seen = {}
    for f in vein_dir.iterdir():
        parts = f.name.split("_")
        if (f.name.endswith(".png") and len(parts) >= 3
                and parts[0].isdigit() and parts[1].isdigit() and "_vein_" in f.name):
            key = (int(parts[0]), int(parts[1]))
            if key in seen:
                print("[vein] 序号冲突 %d_%d: 保留 %s 丢弃 %s"
                      % (key[0], key[1], seen[key], f.name))
                continue
            seen[key] = f.name
            rows.append((key[0], key[1], f.name))
    rows.sort()
    gen = "".join('    {"%d_%d", "vein_%d_%d", "%s"},\n' % (a, b, a, b, name)
                  for a, b, name in rows)
    header = ("//说明：本文件由构建时自动生成，请勿手工编辑。\n"
              "//内容是 asset 贴图的逻辑名、资源名与源文件对照表，用于贴图打包与按 id 加载。\n"
              "#pragma once\n"
              "struct GvVeinTexEntry { const char* name; const char* res; const char* file; };\n"
              "static const GvVeinTexEntry kGvVeinTexs[] = {\n" + gen + "};\n"
              "static const int kGvVeinTexCount = (int)(sizeof(kGvVeinTexs) / sizeof(kGvVeinTexs[0]));\n")
    h_file = BASE / "include" / "vein_gen.gen.h"
    changed = False
    if not h_file.exists() or h_file.read_text(encoding="utf-8") != header:
        h_file.write_text(header, encoding="utf-8", newline="\n")
        changed = True
    rc_file = BASE / "src" / "vein_gen.rc"
    if rc_file.exists():
        rc_file.unlink()
        changed = True
    old_txt = vein_dir / "vein.txt"
    if old_txt.exists():
        old_txt.unlink()
    return changed


sync_shaders()
sync_vein()


def sync_audio():
    audio_dir = BASE / "assets" / "Audio"
    rows = []
    seen = {}
    for sub, mark in (("snd", "_snd_"), ("long", "_long_")):
        d = audio_dir / sub
        if not d.is_dir():
            continue
        for f in d.iterdir():
            parts = f.name.split("_")
            if (f.suffix.lower() in (".wav", ".mp3") and len(parts) >= 3
                    and parts[0].isdigit() and parts[1].isdigit() and mark in f.name):
                key = (int(parts[0]), int(parts[1]))
                if key in seen:
                    print("[audio] 序号冲突 %d_%d: 保留 %s 丢弃 %s"
                          % (key[0], key[1], seen[key], f.name))
                    continue
                seen[key] = f.name
                rows.append((key[0], key[1], sub + "/" + f.name))
    rows.sort()
    gen = "".join('    {"%d_%d", "audio_%d_%d", "%s"},\n' % (a, b, a, b, name)
                  for a, b, name in rows)
    header = ("//说明：本文件由构建时自动生成，请勿手工编辑。\n"
              "//内容是音频资源的逻辑名、资源名与源文件对照表，用于音频打包与按 id 加载。\n"
              "#pragma once\n"
              "struct GvAudioSndEntry { const char* name; const char* res; const char* file; };\n"
              "static const GvAudioSndEntry kGvAudioSnds[] = {\n" + gen + "};\n"
              "static const int kGvAudioSndCount = (int)(sizeof(kGvAudioSnds) / sizeof(kGvAudioSnds[0]));\n")
    h_file = BASE / "include" / "audio_gen.gen.h"
    changed = False
    if not h_file.exists() or h_file.read_text(encoding="utf-8") != header:
        h_file.write_text(header, encoding="utf-8", newline="\n")
        changed = True
    if changed:
        print("[audio] 已更新 audio_gen.gen.h (%d 项)" % len(rows))


sync_audio()


def any_dep_newer(deps, obj):
    try:
        obj_mtime = Path(obj).stat().st_mtime
    except OSError:
        return True
    for d in deps:
        try:
            if Path(d).stat().st_mtime > obj_mtime:
                return True
        except OSError:
            pass
    return False


def classify(src, obj, build_dir):
    if not obj.exists():
        return "编译"
    if newer(src, obj, build_dir):
        return "增量"
    # 头文件被别的路径也是通过 -I 找到的，.d 里记的是相对路径，
    # 一旦 include 路径变了，旧依赖表就不可信，宁可重编一次
    df = Path(obj).with_suffix(".d")
    if df.exists():
        try:
            text = df.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return "增量"
        for inc in ("include/Rhidriver/backend.h", "include/Rhidriver/sg/rhi_sg.h"):
            if inc in text and not (BASE / inc).exists():
                return "编译"
    return "跳过"


def engine_incs():
    return list(ENGINE_CPP_INCS)


def compile_one(src, obj, args):
    run([GXX, "-c", "-O2", "-std=c++17", "-finput-charset=UTF-8", "-fexec-charset=UTF-8", "-MMD", "-MP"] + args + ["-o", obj, src])


def obj_path(bdir, src):
    stem = Path(src).stem
    sub = Path(src).parent.as_posix().replace("/", "_").replace(".", "_")
    return bdir / f"{sub}_{stem}.o"


def parallel_compile(tasks, jobs):
    if not tasks:
        return
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        futures = [ex.submit(compile_one, *t) for t in tasks]
        for f in as_completed(futures):
            f.result()


def prepare_variant(tag):
    bdir = BUILD / tag
    bdir.mkdir(parents=True, exist_ok=True)
    defs = ["-DGVTCIER_BUILD_DLL", "-DGV_DEBUG=0", "-DVK_USE_PLATFORM_WIN32_KHR=1"]

    res_res = bdir / "res.res"
    res_o = bdir / "res.o"

    jobs = []

    windres_args = [WINDRES]
    deps = rc_deps(BASE / "src" / "res.rc")
    res_res_status = "编译" if newer(BASE / "src" / "res.rc", res_res, bdir, deps,
                                     dep_file_required=False) else "跳过"
    jobs.append({
        "status": res_res_status,
        "name": "res.rc",
        "action": lambda: run(windres_args + ["src/res.rc", "-O", "coff", "-o", res_res]),
    })

    res_o_status = "编译" if newer(BASE / "src" / "资源.cpp", res_o, bdir,
                                   extra_deps=[res_res], dep_file_required=False) else "跳过"
    jobs.append({
        "status": res_o_status,
        "name": "资源.cpp",
        "action": lambda: run([GXX, "-c", "-O2", "-std=c++17", "-finput-charset=UTF-8", "-fexec-charset=UTF-8", "src/资源.cpp", "-o", res_o]),
    })

    launcher_o = bdir / "launcher_res.o"
    launcher_o_status = "编译" if newer(BASE / "src" / "资源.cpp", launcher_o, bdir,
                                        extra_deps=[res_res], dep_file_required=False) else "跳过"
    jobs.append({
        "status": launcher_o_status,
        "name": "lanc_res.cpp",
        "action": lambda: run([GXX, "-c", "-O2", "-std=c++17", "-finput-charset=UTF-8", "-fexec-charset=UTF-8", "src/资源.cpp", "-o", launcher_o]),
    })

    engine_srcs = discover_engine_srcs()
    lua_srcs = discover_lua_srcs()

    incs = engine_incs()
    for s in engine_srcs:
        obj = obj_path(bdir, s)
        st = classify(s, obj, bdir)
        jobs.append({
            "status": st,
            "name": Path(s).name,
            "action": (lambda s=s, obj=obj: compile_one(s, obj, defs + incs)),
        })

    for s in lua_srcs:
        obj = obj_path(bdir, s)
        st = classify(s, obj, bdir)
        jobs.append({
            "status": st,
            "name": Path(s).name,
            "action": (lambda s=s, obj=obj: compile_one(s, obj, ["-I", "src/GvtcierScript"])),
        })

    for s in sorted(TOOL_BASE):
        rel = str((BASE / TOOL_BASE[s] / s).relative_to(BASE).as_posix())
        obj = obj_path(bdir, rel)
        st = classify(rel, obj, bdir)
        jobs.append({
            "status": st,
            "name": s,
            "action": (lambda s=rel, obj=obj: compile_one(
                s, obj,
                ["-Wall", "-DGVTCIER_BUILD_DLL", "-DGV_DEBUG=0"] + ENGINE_CPP_INCS)),
        })

    utf8_obj = obj_path(bdir, "src/bicu/core/宽字符.cpp")

    tool_objs = {}
    for exe, rel in TOOL_EXES.items():
        obj = obj_path(bdir, rel)
        st = classify(rel, obj, bdir)
        tool_objs[exe] = obj
        jobs.append({
            "status": st,
            "name": Path(rel).name,
            "action": (lambda s=rel, obj=obj: compile_one(
                s, obj,
                ["-O2", "-std=c++17", "-DGVTCIER_BUILD_DLL", "-DGV_DEBUG=0"] + ENGINE_CPP_INCS)),
        })

    jobs.sort(key=lambda j: STATUS_PRIORITY[j["status"]])
    pending = []
    for j in jobs:
        FOLDER.emit(j["status"], j["name"])
        if j["status"] in ("编译", "增量"):
            pending.append(j["action"])

    if pending:
        parallel_actions(pending, JOBS)

    return {
        "bdir": bdir,
        "engine_objs": [obj_path(bdir, s) for s in engine_srcs],
        "lua_objs": [obj_path(bdir, s) for s in lua_srcs],
        "exe_objs": [obj_path(bdir, str((BASE / TOOL_BASE[s] / s).relative_to(BASE).as_posix())) for s in TOOL_BASE] + [utf8_obj],
        "tool_objs": tool_objs,
    }


def parallel_actions(actions, jobs):
    if not actions:
        return
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        futures = [ex.submit(a) for a in actions]
        for f in as_completed(futures):
            f.result()


def link_variant(tag, out_dir, dll_name, info):
    bdir = info["bdir"]
    out_dir.mkdir(parents=True, exist_ok=True)

    FOLDER.emit("链接", f"{dll_name}.dll")
    run([GXX, "-shared", "-O2", "-std=c++17",
         "-static-libgcc", "-static-libstdc++", "-static"] +
        info["engine_objs"] + info["lua_objs"] + [bdir / "res.res", bdir / "res.o",
                               "-o", out_dir / f"{dll_name}.dll",
                               "-Wl,--out-implib," + str(bdir / f"{dll_name}.lib"),
                               "-L", VULKAN_LIB_DIR] + VULKAN_LIBS + [
                               "-luser32", "-lgdi32", "-lopengl32", "-lcomdlg32", "-lgdiplus", "-lcomctl32",
                               "-lole32", "-lmsimg32", "-limm32", "-ldbghelp", "-lws2_32", "-lpsapi"])


def link_exe(out_dir, dll_name, info):
    bdir = info["bdir"]
    FOLDER.emit("链接", "main.exe")
    run([GXX, "-O2", "-std=c++17", "-municode", "-static-libgcc", "-static-libstdc++", "-static"] +
        info["exe_objs"] + [bdir / "res.res", bdir / "res.o",
                            "-o", out_dir / "main.exe",
                            "-luser32", "-lgdi32", "-lopengl32", "-lgdiplus", "-lole32", "-lws2_32"])


def link_launcher(out_dir, info):
    FOLDER.emit("链接", "Lanc.exe")
    if not CSC.exists():
        print(color(f"[跳过] 未找到 csc.exe，跳过 Lanc.exe 构建：{CSC}", YELLOW))
        return
    if not LAUNCHER_SRC.exists():
        print(color(f"[跳过] 未找到 {LAUNCHER_SRC}", YELLOW))
        return
    run([CSC, "-nologo", "-target:winexe", "-platform:anycpu", "-optimize+",
         "-win32icon:" + str(BASE / "assets" / "logo" / "Gvtcier.ico"),
         "-out:" + str(out_dir / "Lanc.exe"),
         str(LAUNCHER_SRC)],
        quiet=True)


def link_gvm(out_dir, info):
    FOLDER.emit("链接", "Gvm.exe")
    if not CSC.exists():
        print(color(f"[跳过] 未找到 csc.exe，跳过 Gvm.exe 构建：{CSC}", YELLOW))
        return
    if not GVM_SRC.exists():
        print(color(f"[跳过] 未找到 {GVM_SRC}", YELLOW))
        return
    run([CSC, "-nologo", "-target:winexe", "-platform:anycpu", "-optimize+",
         "-win32icon:" + str(BASE / "assets" / "logo" / "Gvtcier.ico"),
         "-r:System.Windows.Forms.dll", "-r:System.Drawing.dll",
         "-out:" + str(out_dir / "Gvm.exe"),
         str(GVM_SRC)],
        quiet=True)


def link_tool_exes(out_dir, info):
    bdir = info["bdir"]
    extra_libs = {
        "Gvt调试器.exe": ["-lgdiplus", "-lgdi32", "-lole32", "-lshell32",
                          "-L", str(bdir), "-lGvtcier"],
    }
    for exe, obj in info["tool_objs"].items():
        FOLDER.emit("链接", exe)
        run([GXX, "-O2", "-std=c++17",
             "-static-libgcc", "-static-libstdc++", "-static", obj,
             "-o", out_dir / exe, "-luser32"] + extra_libs.get(exe, []))


def kill_running_products():
    targets = ["main.exe", "Lanc.exe", "Gvm.exe", "Gvt调试器.exe"]
    killed = []
    for name in targets:
        r = subprocess.run(["taskkill", "/F", "/IM", name],
                           capture_output=True, text=True, errors="replace")
        if r.returncode == 0:
            killed.append(name)
    if killed:
        print(color(f"[关闭] 已结束正在运行的产物: {', '.join(killed)}", YELLOW))
    time.sleep(0.3)


def unblock_exes():
    import ctypes
    for exe in SDK.rglob("*.exe"):
        ctypes.windll.kernel32.DeleteFileW(str(exe) + ":Zone.Identifier")


DATA_DIRNAME = "Gvtcier_Data"

# 与后端无关的通用运行时。左边是仓库里的源文件，右边是它在 Gvtcier_Data 下的落地名。
# warehouse.dll 由它自己的构建脚本直接产出到 Gvtcier_Data，不走这张表；
# GL 用系统 opengl32.dll、VK 用显卡驱动装的 vulkan-1.dll，这两个都不该由我们分发
THIRD_PARTY_RUNTIME = {
    BASE / "Thirdparty" / "openal" / "OpenAL32.dll": "OpenAL32.dll",
    BASE / "Thirdparty" / "openal" / "wrap_oal.dll": "wrap_oal.dll",
}


def collect_third_party():
    data_dir = SDK / DATA_DIRNAME
    data_dir.mkdir(parents=True, exist_ok=True)
    for src, name in THIRD_PARTY_RUNTIME.items():
        if not src.exists():
            print(color(f"[警告] 缺少运行时源文件 {src.name}（应在 Thirdparty/openal）", YELLOW))
            continue
        dst = data_dir / name
        if (not dst.exists()) or src.stat().st_mtime > dst.stat().st_mtime:
            shutil.copy2(src, dst)


def build_tool():
    """把 Gvtcier_tool.cs 编成 SDK 里的 Gvtcier_tool.exe。

    C# 走系统自带的 .NET Framework，编出来是单个 exe，不需要运行时解压，
    也就没有 PyInstaller onefile 那种临时目录报错。
    """
    src = BASE / "tool" / "Gvtcier_tool" / "Gvtcier_tool.cs"
    out = SDK / "Gvtcier_tool.exe"
    if not src.exists():
        print(color(f"[警告] 找不到 {src}，跳过打包工具构建", YELLOW))
        return True
    if out.exists() and out.stat().st_mtime >= src.stat().st_mtime:
        return True
    if not CSC.exists():
        print(color(f"[警告] 未找到 csc.exe，跳过 Gvtcier_tool.exe 构建：{CSC}", YELLOW))
        return True

    print("  [前置] Gvtcier_tool.exe（游戏打包工具）")
    # run 失败会自己 sys.exit，这里不需要再判返回值
    run([str(CSC), "-nologo", "-target:winexe", "-platform:anycpu", "-optimize+",
         "-codepage:65001",
         "-r:System.Windows.Forms.dll", "-r:System.Drawing.dll",
         "-win32icon:" + str(BASE / "assets" / "logo" / "Gvtcier.ico"),
         "-out:" + str(out), str(src)],
        quiet=True)
    return True


def build_warehouse():
    script = BASE / "Thirdparty" / "warehouse" / "build_warehouse.py"
    if not script.exists():
        return True
    dll = SDK / DATA_DIRNAME / "warehouse.dll"
    src = BASE / "Thirdparty" / "warehouse" / "warehouse.cpp"
    if dll.exists() and src.exists() and dll.stat().st_mtime >= src.stat().st_mtime:
        return True
    print("  [前置] warehouse.dll（着色器编译器）")
    return subprocess.call([sys.executable, str(script)], cwd=str(BASE)) == 0


def build_win():
    print("编译开始，请注意代码与架构一致性，不要引入独特的东西，注意阅读文档！")
    kill_running_products()

    SDK.mkdir(parents=True, exist_ok=True)
    (SDK / DATA_DIRNAME).mkdir(parents=True, exist_ok=True)

    if not build_warehouse():
        print(color("[错误] warehouse.dll 构建失败，着色器无法编译", RED))
        sys.exit(1)
    if not build_tool():
        print(color("[错误] Gvtcier_tool.exe 构建失败", RED))
        sys.exit(1)

    rel_info = prepare_variant("rel")
    FOLDER.flush()

    link_variant("rel", SDK, "Gvtcier", rel_info)
    link_exe(SDK, "Gvtcier", rel_info)
    link_launcher(SDK, rel_info)
    link_gvm(SDK, rel_info)
    link_tool_exes(SDK, rel_info)

    collect_third_party()

    FOLDER.flush()
    unblock_exes()
    print(color("[编译成功 | BUILD OK]", GREEN))
    print("编译开始，请注意代码与架构一致性，不要引入独特的东西，注意阅读文档！")


ANDROID_DIR = BASE / "src" / "bicu" / "platform" / "android"


def build_android(demo_dir):
    import json
    demo = Path(demo_dir).resolve()
    if not demo.is_dir():
        print(color(f"[错误] Demo 目录不存在: {demo}", RED))
        sys.exit(1)
    cfg_path = demo / "android.json"
    if not cfg_path.is_file():
        print(color(f"[错误] 缺少 {cfg_path}", RED))
        sys.exit(1)
    with open(cfg_path, encoding="utf-8") as f:
        cfg = json.load(f)

    appname = cfg.get("appname", demo.name)
    lib = cfg.get("lib", demo.name)
    package = cfg.get("package", "com.Li.HuaL1n.Gvtcier." + lib)
    versioncode = str(cfg.get("versionCode", "1"))
    versionname = str(cfg.get("versionName", "1.0"))

    assets = demo / "assets"
    assets.mkdir(parents=True, exist_ok=True)
    gpk_path = assets / "game.gpk"

    meta = {
        "name": appname,
        "description": cfg.get("description", ""),
        "author": cfg.get("author", ""),
    }
    meta_path = demo / ".gpk_meta.json"
    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False)

    pack_args = []
    cover = demo / "cover.png"
    if cover.is_file():
        pack_args += ["--entry", f"{cover}|cover.png"]
    for real, store in cfg.get("gpak", []):
        rp = (demo / real).resolve()
        pack_args += ["--entry", f"{rp}|{store}"]

    run_script([sys.executable, str(ANDROID_DIR / "pack_res.py"),
                "--out", str(gpk_path),
                "--meta", str(meta_path)] + pack_args,
               "pack gpk")
    meta_path.unlink(missing_ok=True)

    run_script([sys.executable, str(ANDROID_DIR / "android_build.py"),
                "--demo", str(demo), "--lib", lib],
               "compile .so")

    run_script([sys.executable, str(ANDROID_DIR / "apk_build.py"),
                "--demo", str(demo), "--lib", lib,
                "--package", package, "--appname", appname,
                "--versioncode", versioncode, "--versionname", versionname],
               "build apk")

    print(color("[编译成功 | BUILD OK]", GREEN))


def build_pak(demo_dir):
    import json
    demo = Path(demo_dir).resolve()
    if not demo.is_dir():
        print(color(f"[错误] Demo 目录不存在: {demo}", RED))
        sys.exit(1)
    cfg_path = demo / "android.json"
    cfg = {}
    if cfg_path.is_file():
        with open(cfg_path, encoding="utf-8") as f:
            cfg = json.load(f)

    name = cfg.get("appname", demo.name)
    version = ""
    try:
        version = (BASE / "version.txt").read_text(encoding="utf-8-sig").splitlines()[0].strip()
    except (OSError, IndexError):
        version = "unknown"
    tier = getattr(build_pak, "tier", "")
    licensee = getattr(build_pak, "licensee", "")
    meta = {
        "name": name,
        "description": cfg.get("description", ""),
        "author": cfg.get("author", ""),
        "engine_version": version,
        "built_at": datetime.now().isoformat(timespec="seconds"),
        "license_tier": tier,
        "licensee": licensee,
    }
    meta_path = demo / ".gpk_meta.json"
    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False)

    pack_args = []
    cover = demo / "cover.png"
    if cover.is_file():
        pack_args += ["--entry", f"{cover}|cover.png"]
    vein_dir = BASE / "assets" / "Vein"
    if vein_dir.is_dir():
        used = {}
        for f in vein_dir.glob("*.png"):
            parts = f.name.split("_")
            if (f.name.endswith(".png") and len(parts) >= 3
                    and parts[0].isdigit() and parts[1].isdigit() and "_vein_" in f.name):
                key = f"{int(parts[0])}_{int(parts[1])}"
                if key in used:
                    continue
                used[key] = f.name
                pack_args += ["--entry", f"{f}|vein_{key}"]
    audio_dir = BASE / "assets" / "Audio"
    if audio_dir.is_dir():
        used = {}
        for sub, mark in (("snd", "_snd_"), ("long", "_long_")):
            d = audio_dir / sub
            if not d.is_dir():
                continue
            for f in d.iterdir():
                parts = f.name.split("_")
                if (f.suffix.lower() in (".wav", ".mp3") and len(parts) >= 3
                        and parts[0].isdigit() and parts[1].isdigit() and mark in f.name):
                    key = f"{int(parts[0])}_{int(parts[1])}"
                    if key in used:
                        continue
                    used[key] = f.name
                    pack_args += ["--entry", f"{f}|audio_{key}"]
    for real, store in cfg.get("gpak", []):
        rp = (demo / real).resolve()
        pack_args += ["--entry", f"{rp}|{store}"]

    out = demo / f"{name}.gpk"
    run_script([sys.executable, str(ANDROID_DIR / "pack_res.py"),
                "--out", str(out), "--meta", str(meta_path),
                "--no-engine"] + pack_args,
               "pack gpk")
    meta_path.unlink(missing_ok=True)

    games_dir = DIST / "Games" / name
    games_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(out, games_dir / f"{name}.gpk")
    print(color(f"[打包成功 | PACK OK] {games_dir / (name + '.gpk')}", GREEN))


def build_mod(mod_dir):
    import json
    mod = Path(mod_dir).resolve()
    if not mod.is_dir():
        print(color(f"[错误] 模组目录不存在: {mod}", RED))
        sys.exit(1)

    meta_path = mod / "mod.json"
    meta = {}
    if meta_path.is_file():
        with open(meta_path, encoding="utf-8") as f:
            meta = json.load(f)
    name = meta.get("name") or mod.name
    if not meta.get("name"):
        meta["name"] = name
    meta.setdefault("version", "")
    meta.setdefault("author", "")
    meta.setdefault("description", "")

    tmp_meta = mod / ".glc_meta.json"
    with open(tmp_meta, "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False)

    pack_args = []
    for f in sorted(mod.rglob("*")):
        if not f.is_file():
            continue
        rel = f.relative_to(mod).as_posix()
        if rel in ("mod.json", ".glc_meta.json"):
            continue
        if f.suffix.lower() == ".glc" or f.suffix.lower() == ".disabled":
            continue
        pack_args += ["--entry", f"{f}|{rel}"]

    out = mod / f"{name}.glc"
    run_script([sys.executable, str(ANDROID_DIR / "pack_mod.py"),
                "--out", str(out), "--meta", str(tmp_meta)] + pack_args,
               "pack glc")
    tmp_meta.unlink(missing_ok=True)

    game_dir = getattr(build_mod, "game", None)
    games_root = DIST / "Games"
    if game_dir:
        games_dir = games_root / game_dir
    else:
        found = sorted(p for p in games_root.iterdir() if p.is_dir()) if games_root.is_dir() else []
        if len(found) != 1:
            print(color(f"[错误] 无法确定目标游戏，请用 --game <游戏ID> 指定（现有: {[p.name for p in found]}）", RED))
            out.unlink(missing_ok=True)
            sys.exit(1)
        games_dir = found[0]
    if not games_dir.is_dir():
        print(color(f"[错误] 目标游戏目录不存在: {games_dir}", RED))
        out.unlink(missing_ok=True)
        sys.exit(1)

    mod_out = games_dir / "mod"
    mod_out.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(out, mod_out / out.name)
    print(color(f"[模组打包成功 | MOD OK] {mod_out / out.name}", GREEN))


def run_script(cmd, name):
    r = subprocess.run([str(a) for a in cmd])
    if r.returncode != 0:
        print(color(f"[{name}] 失败 | BUILD FAIL", RED))
        sys.exit(1)


def main():
    args = sys.argv[1:]
    target = args[0] if args else "-win"
    if target in ("-and", "--and", "--android"):
        if len(args) < 2:
            print(color("[错误] 用法: build.py -and <Demo目录>", RED))
            sys.exit(1)
        build_android(args[1])
    elif target in ("-pak", "--pak"):
        if len(args) < 2:
            print(color("[错误] 用法: build.py -pak <Demo目录> [--tier 授权档位] [--licensee 被授权方]", RED))
            sys.exit(1)
        rest = args[2:]
        for i, a in enumerate(rest):
            if a in ("--tier", "--licensee") and i + 1 < len(rest):
                setattr(build_pak, a[2:], rest[i + 1])
        build_pak(args[1])
    elif target in ("-mod", "--mod"):
        if len(args) < 2:
            print(color("[错误] 用法: build.py -mod <模组目录> [--game 游戏ID]", RED))
            sys.exit(1)
        rest = args[2:]
        for i, a in enumerate(rest):
            if a == "--game" and i + 1 < len(rest):
                setattr(build_mod, "game", rest[i + 1])
        build_mod(args[1])
    else:
        build_win()


if __name__ == "__main__":
    main()