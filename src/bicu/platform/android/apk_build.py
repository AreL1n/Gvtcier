import argparse
import os
import shutil
import subprocess
import sys
import zipfile

ENGINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROOT = os.path.dirname(ENGINE)
SDK = os.path.join(ROOT, "tools", "AndroidSDK")
BT = os.path.join(SDK, "build-tools", "35.0.0")
PLATFORM_JAR = os.path.join(SDK, "platforms", "android-34", "android.jar")
AAPT2 = os.path.join(BT, "aapt2.exe")
ZIPALIGN = os.path.join(BT, "zipalign.exe")
APKSIGNER = os.path.join(BT, "apksigner.bat")
JAVA = "java"
D8 = os.path.join(BT, "lib", "d8.jar")
TEMPLATE = os.path.join(ENGINE, "platform", "android", "template")

ABI = "arm64-v8a"


def run(cmd, desc, shell=False):
    print("[apk] " + desc)
    r = subprocess.run(cmd, capture_output=True, text=True, errors="ignore", shell=shell)
    if r.returncode != 0:
        print(r.stdout[-6000:])
        print(r.stderr[-6000:])
        print("[apk] FAIL: " + desc)
        sys.exit(1)


def render_template(src_path, dst_path, repl):
    with open(src_path, "r", encoding="utf-8") as f:
        text = f.read()
    for k, v in repl.items():
        text = text.replace(k, v)
    with open(dst_path, "w", encoding="utf-8") as f:
        f.write(text)


LAUNCHER_ICON_SIZES = [
    ("mipmap-mdpi", 48),
    ("mipmap-hdpi", 72),
    ("mipmap-xhdpi", 96),
    ("mipmap-xxhdpi", 144),
    ("mipmap-xxxhdpi", 192),
]


def gen_launcher_icons(src_png, res_dir):
    if not os.path.exists(src_png):
        print("[apk] missing icon: " + src_png)
        sys.exit(1)
    try:
        from PIL import Image
    except ImportError:
        print("[apk] 需要 Pillow 生成应用图标: pip install Pillow")
        sys.exit(1)
    with Image.open(src_png) as im:
        base = im.convert("RGBA")
        for folder, size in LAUNCHER_ICON_SIZES:
            out_dir = os.path.join(res_dir, folder)
            os.makedirs(out_dir, exist_ok=True)
            base.resize((size, size), Image.LANCZOS).save(
                os.path.join(out_dir, "ic_launcher.png"))
    print("[apk] launcher icon <- " + src_png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--demo", required=True)
    ap.add_argument("--lib", required=True)
    ap.add_argument("--package", required=True)
    ap.add_argument("--appname", required=True)
    ap.add_argument("--versioncode", default="1")
    ap.add_argument("--versionname", default="1.0")
    ap.add_argument("--out", default=None)
    ap.add_argument("--assets", default=None)
    ap.add_argument("--icon", default=None)
    args = ap.parse_args()

    demo = os.path.abspath(args.demo)
    out_apk = args.out or os.path.join(demo, args.lib + ".apk")
    so_src = os.path.join(ROOT, ".android", "lib", ABI, "lib" + args.lib + ".so")
    stage = os.path.join(ROOT, ".android", "apkwork")

    if not os.path.exists(so_src):
        print("[apk] missing .so: " + so_src)
        sys.exit(1)

    if os.path.exists(stage):
        shutil.rmtree(stage)
    os.makedirs(os.path.join(stage, "root"))
    os.makedirs(os.path.join(stage, "gen"))
    os.makedirs(os.path.join(stage, "classes"))
    os.makedirs(os.path.join(stage, "obj"))

    ascii_res = os.path.join(stage, "res")
    shutil.copytree(os.path.join(TEMPLATE, "res"), ascii_res)
    render_template(os.path.join(ascii_res, "values", "strings.xml"),
                    os.path.join(ascii_res, "values", "strings.xml"),
                    {"@APP_NAME@": args.appname})

    icon_src = args.icon or os.path.join(ROOT, "assets", "logo", "Gvtcier.png")
    gen_launcher_icons(icon_src, ascii_res)

    ascii_manifest = os.path.join(stage, "AndroidManifest.xml")
    render_template(os.path.join(TEMPLATE, "AndroidManifest.xml"), ascii_manifest, {
        "@PACKAGE@": args.package,
        "@LIBNAME@": args.lib,
        "@VERSION_CODE@": args.versioncode,
        "@VERSION_NAME@": args.versionname,
    })

    run([AAPT2, "compile", "--dir", ascii_res, "-o", os.path.join(stage, "res.zip")],
        "aapt2 compile res")
    run([AAPT2, "link",
         "-o", os.path.join(stage, "base.apk"),
         "-I", PLATFORM_JAR,
         "--manifest", ascii_manifest,
         "--java", os.path.join(stage, "gen"),
         os.path.join(stage, "res.zip")],
        "aapt2 link manifest")

    run([JAVA, "-cp", D8, "com.android.tools.r8.D8",
         "--release", "--min-api", "24",
         "--output", os.path.join(stage, "classes")],
        "d8 (no java sources, native only)")

    unsigned = os.path.join(stage, "unsigned.apk")
    shutil.copy(os.path.join(stage, "base.apk"), unsigned)

    assets_dir = args.assets or os.path.join(demo, "assets")
    so_rel = "lib/%s/lib%s.so" % (ABI, args.lib)
    aligned = os.path.join(stage, "aligned.apk")
    with zipfile.ZipFile(unsigned, "r") as zin:
        names = zin.namelist()
        with zipfile.ZipFile(aligned, "w", zipfile.ZIP_DEFLATED) as zout:
            for n in names:
                info = zin.getinfo(n)
                zout.writestr(info, zin.read(n))
            if os.path.isdir(assets_dir):
                for dirpath, _dirs, files in os.walk(assets_dir):
                    for fn in files:
                        full = os.path.join(dirpath, fn)
                        rel = "assets/" + os.path.relpath(full, assets_dir).replace("\\", "/")
                        if rel not in names:
                            zout.write(full, rel)

    if so_rel not in names:
        with zipfile.ZipFile(aligned, "a", zipfile.ZIP_STORED) as zout:
            zout.write(so_src, so_rel)

    run([ZIPALIGN, "-f", "-p", "4", aligned, os.path.join(stage, "aligned2.apk")], "zipalign")

    ks = os.path.join(ROOT, ".android", "debug.keystore")
    if not os.path.exists(ks):
        keytool = os.path.join(os.environ.get("JAVA_HOME", r"C:\Program Files\Java\jdk-21.0.11"), "bin", "keytool.exe")
        run([keytool, "-genkeypair", "-keystore", ks,
             "-alias", "gvw", "-keyalg", "RSA", "-keysize", "2048",
             "-validity", "10000", "-storepass", "android", "-keypass", "android",
             "-dname", "CN=GvGame"],
            "keytool genkey")
    run([APKSIGNER, "sign", "--ks", ks, "--ks-pass", "pass:android",
         "--key-pass", "pass:android", "--out", out_apk,
         os.path.join(stage, "aligned2.apk")],
        "apksigner sign")
    print("[apk] BUILD OK -> " + out_apk)


if __name__ == "__main__":
    main()
