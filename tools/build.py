#!/usr/bin/env python3
# Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
"""Build the Qt-free C++ core and pinned Flutter desktop shell."""
import argparse
import getpass
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
FLUTTER_VERSION = "3.19.6"
VCPKG_VERSION = "2026.10.16"
TRIPLET = "x64-windows-static-md"
KEY_ENV = "SECL_CURSEFORGE_API_KEY"


def run(*cmd, **kwargs):
    subprocess.run([str(x) for x in cmd], check=True, **kwargs)


def output(*cmd):
    return subprocess.check_output([str(x) for x in cmd]).decode("utf-8-sig").strip()


def checkout(name, url, tag, destination, marker):
    if (destination / marker).is_file():
        return destination
    if destination.exists():
        raise SystemExit(f"Incomplete {name} checkout at {destination}; choose another dependency directory.")
    destination.parent.mkdir(parents=True, exist_ok=True)
    # A failed clone never leaves a partially installed SDK in the cache.
    with tempfile.TemporaryDirectory(prefix=f"{name}-", dir=destination.parent) as scratch:
        staged = pathlib.Path(scratch) / "source"
        run("git", "clone", "--depth", "1", "--branch", tag, url, staged)
        staged.rename(destination)
    return destination


def windows_tools(args):
    if platform.machine().lower() not in ("amd64", "x86_64") or struct.calcsize("P") != 8:
        raise SystemExit("Use an x64 Windows machine and 64-bit Python for this Flutter 3.19.6 build.")
    vswhere = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / (
        "Microsoft Visual Studio/Installer/vswhere.exe"
    )
    if not vswhere.is_file():
        raise SystemExit("Install Visual Studio 2022 or its Build Tools with Desktop development with C++ (including CMake and the Windows SDK).")
    vs_path = output(
        vswhere, "-latest", "-products", "*", "-version", "[17.0,18.0)",
        "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "Microsoft.VisualStudio.Component.VC.CMake.Project", "-utf8", "-property", "installationPath",
    )
    if not vs_path:
        raise SystemExit("Visual Studio 2022 needs the x64 C++ and CMake components. Modify its Desktop development with C++ workload.")
    cmake = pathlib.Path(vs_path) / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
    ctest = cmake.with_name("ctest.exe")
    if not cmake.is_file() or not ctest.is_file():
        raise SystemExit("Visual Studio's CMake tools are missing; repair the C++ CMake tools component.")
    if not shutil.which("git"):
        raise SystemExit("Install Git for Windows, then reopen PowerShell so Git is on PATH.")

    default_cache = pathlib.Path(os.environ.get("LOCALAPPDATA", str(pathlib.Path.home()))) / "SECL-Build" / "deps"
    cache = pathlib.Path(args.windows_deps).resolve() if args.windows_deps else default_cache
    flutter = args.flutter or shutil.which("flutter.bat")
    if not flutter:
        sdk = checkout("flutter", "https://github.com/flutter/flutter.git", FLUTTER_VERSION,
                       cache / f"flutter-{FLUTTER_VERSION}", "bin/flutter.bat")
        flutter = str(sdk / "bin/flutter.bat")
    require_flutter(flutter)
    selected_vcpkg = args.vcpkg_root or os.environ.get("VCPKG_ROOT")
    vcpkg = pathlib.Path(selected_vcpkg).resolve() if selected_vcpkg else checkout(
        "vcpkg", "https://github.com/microsoft/vcpkg.git", VCPKG_VERSION,
        cache / f"vcpkg-{VCPKG_VERSION}", "bootstrap-vcpkg.bat",
    )
    toolchain = vcpkg / "scripts/buildsystems/vcpkg.cmake"
    bootstrap = vcpkg / "bootstrap-vcpkg.bat"
    if not toolchain.is_file() or not bootstrap.is_file():
        raise SystemExit("Specify a valid vcpkg checkout with --vcpkg-root.")
    exe = vcpkg / "vcpkg.exe"
    deps_env = dict(os.environ, VCPKG_ROOT=str(vcpkg), VCPKG_DISABLE_METRICS="1")
    if not exe.is_file():
        run(bootstrap, "-disableMetrics", cwd=vcpkg, env=deps_env)
    run(exe, "install", f"curl:{TRIPLET}", f"zlib:{TRIPLET}", "--disable-metrics", cwd=vcpkg, env=deps_env)
    return flutter, cmake, ctest, deps_env, [
        "-G", "Visual Studio 17 2022", "-A", "x64",
        f"-DCMAKE_GENERATOR_INSTANCE={vs_path}",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        f"-DVCPKG_TARGET_TRIPLET={TRIPLET}",
        # vcpkg already selects the curl config. CMake's FindZLIB module provides
        # ZLIB::ZLIB, which both the core and curl's exported target expect.
        "-DCMAKE_FIND_PACKAGE_PREFER_CONFIG=OFF",
    ]


def require_flutter(flutter):
    version = json.loads(output(flutter, "--version", "--machine"))
    if version["frameworkVersion"] != FLUTTER_VERSION:
        raise SystemExit(f"Use Flutter {FLUTTER_VERSION}. Set --flutter to its executable; the project pins this SDK.")


def build_native(cmake, ctest, native, flags, env, args, key):
    config_env = dict(env)
    if args.embed_curseforge_key:
        config_env[KEY_ENV] = key
    try:
        run(cmake, "-S", ROOT, "-B", native, "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_TESTING=" + ("ON" if args.test else "OFF"),
            "-DSECL_EMBED_CURSEFORGE_KEY=" + ("ON" if args.embed_curseforge_key else "OFF"),
            *flags, env=config_env)
        run(cmake, "--build", native, "--config", "Release", "--parallel", "3", env=env)
        if args.test:
            with tempfile.TemporaryDirectory(prefix="secl-ctest-") as isolated:
                run(ctest, "--test-dir", native, "-C", "Release", "--output-on-failure",
                    env=dict(env, SOLAR_DATA_DIR=isolated))
    finally:
        config_env.pop(KEY_ENV, None)
        # Regenerated on the next configure; do not leave plaintext in a header.
        (native / "core/generated/secl_build_config.hpp").unlink(missing_ok=True)


def zip_windows(bundle, vcpkg_root, embedded):
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    target = dist / ("SECL-windows-x64.zip" if embedded else "SECL-windows-x64-no-curseforge.zip")
    with tempfile.TemporaryDirectory(prefix=".secl-win-", dir=dist) as staging:
        temporary = pathlib.Path(staging) / target.name
        files = [bundle / "secl.exe", *sorted(bundle.glob("*.dll"))]
        files += sorted(p for p in (bundle / "data").rglob("*") if p.is_file())
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in files:
                archive.write(path, pathlib.Path("SECL") / path.relative_to(bundle))
            for name in ("LICENSE", "ADDITIONAL_TERMS.md", "NOTICE"):
                path = ROOT / name
                if path.is_file():
                    archive.write(path, pathlib.Path("SECL") / name)
            for name in ("JSON-LICENSE.MIT", "TINYXML2-LICENSE.txt", "TOMLPLUSPLUS-LICENSE.MIT"):
                archive.write(ROOT / "core/third_party" / name, f"SECL/licenses/{name}")
            for port in ("curl", "zlib"):
                notice = vcpkg_root / "installed" / TRIPLET / "share" / port / "copyright"
                archive.write(notice, f"SECL/licenses/{port}.txt")
        temporary.replace(target)
    print(f"ZIP: {target} ({target.stat().st_size:,} bytes)")


def finish_windows(native, mode, embedded, vcpkg_root):
    bundle = ROOT / "app/build/windows/x64/runner" / mode
    required = ["secl.exe", "solar_core.dll", "flutter_windows.dll", "data/icudtl.dat"]
    if mode == "Release":
        required.append("data/app.so")
    for name in required:
        path = bundle / name
        if not path.is_file() or not path.stat().st_size:
            raise SystemExit(f"Windows build output is missing: {path}")
    packaged = bundle / "solar_core.dll"
    expected = native / "core/Release/solar_core.dll"
    if hashlib.sha256(packaged.read_bytes()).digest() != hashlib.sha256(expected.read_bytes()).digest():
        raise SystemExit("The packaged DLL does not match the just-built native core.")
    run(sys.executable, ROOT / "tools/core_smoke.py", packaged,
        "--expect-embedded", "yes" if embedded else "no")
    print("Built:", bundle / "secl.exe")
    if mode == "Release":
        zip_windows(bundle, vcpkg_root, embedded)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--flutter", default=os.environ.get("FLUTTER"))
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--debug", action="store_true", help="build the Debug desktop app")
    parser.add_argument("--embed-curseforge-key", action="store_true",
                        help="embed SECL_CURSEFORGE_API_KEY in the Release native core")
    parser.add_argument("--vcpkg-root", help="existing Windows vcpkg checkout")
    parser.add_argument("--windows-deps", help="cache directory for the pinned Windows SDKs")
    args = parser.parse_args()
    if args.embed_curseforge_key and args.debug:
        parser.error("--embed-curseforge-key requires a Release build.")
    key = os.environ.pop(KEY_ENV, "")
    system = platform.system()
    if system == "Windows":
        flutter, cmake, ctest, env, flags = windows_tools(args)
        native = ROOT / "build/native-windows-x64"
    else:
        flutter, cmake, ctest, env, flags = args.flutter or "flutter", "cmake", "ctest", os.environ.copy(), []
        require_flutter(flutter)
        native = ROOT / "build/native"
        if system == "Darwin":
            flags = ["-DCMAKE_OSX_ARCHITECTURES=" + platform.machine(), "-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0"]
    if args.embed_curseforge_key:
        if not key and system == "Windows" and sys.stdin.isatty():
            key = getpass.getpass("CurseForge API Key (input hidden): ")
        if not re.fullmatch(r"[!-~]{1,512}", key):
            parser.error(f"Set {KEY_ENV} to a valid Key before embedding it.")
    build_native(cmake, ctest, native, flags, env, args, key)
    del key

    app = ROOT / "app"
    run(flutter, "pub", "get", cwd=app, env=env)
    if args.test:
        run(flutter, "analyze", "--no-pub", cwd=app, env=env)
        run(flutter, "test", "--no-pub", cwd=app, env=env)
    mode = "Debug" if args.debug else "Release"
    target = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}[system]
    flutter_env = dict(env)
    if system == "Windows":
        flutter_env["SECL_PREBUILT_WINDOWS_CORE"] = (native / "core/Release/solar_core.dll").as_posix()
    run(flutter, "build", target, "--" + mode.lower(), "--no-pub", cwd=app, env=flutter_env)
    if system == "Windows":
        finish_windows(native, mode, args.embed_curseforge_key, pathlib.Path(env["VCPKG_ROOT"]))
    elif system == "Darwin":
        bundle = app / "build/macos/Build/Products" / mode / "SECL.app"
        dest = bundle / "Contents/Frameworks/libsolar_core.dylib"
        shutil.copy2(native / "core/libsolar_core.dylib", dest)
        run("codesign", "--force", "--sign", "-", dest)
        entitlements = "DebugProfile.entitlements" if args.debug else "Release.entitlements"
        run("codesign", "--force", "--deep", "--sign", "-", "--entitlements",
            app / "macos/Runner" / entitlements, bundle)
        print("Built:", bundle)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        # Command arguments never contain the Key.
        raise SystemExit(f"Build step failed (exit {error.returncode}): {error.cmd[0]}") from None
