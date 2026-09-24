"""Local Windows source installer; invoked by install-windows.ps1 after VS setup."""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import time
import uuid


def run(*args: object, **kwargs) -> None:
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def release_crt_directory(redist: Path) -> Path:
    """Use the active VS redist root, allowing newer VC toolset folder names."""
    candidates = [path for path in (redist / "x64").glob("Microsoft.VC*.CRT")
                  if path.is_dir() and (path / "msvcp140.dll").is_file()
                  and (path / "vcruntime140.dll").is_file()]
    if len(candidates) != 1:
        raise RuntimeError(f"Expected one complete x64 Visual Studio release CRT under {redist}.")
    return candidates[0]


def require_app_closed() -> None:
    output = subprocess.check_output(["tasklist.exe", "/FI", "IMAGENAME eq Relay.exe", "/FO", "CSV", "/NH"],
                                     text=True, errors="replace", timeout=15)
    if any(row and row[0].lower() == "relay.exe" for row in csv.reader(output.splitlines())):
        raise RuntimeError("Quit Relay before installing an update.")


def create_start_menu_shortcut(destination: Path) -> None:
    # Paths are passed as data, never inserted into PowerShell source.
    environment = os.environ.copy()
    environment["RELAY_INSTALL_TARGET"] = str(destination / "Relay.exe")
    script = ("$ErrorActionPreference = 'Stop'; "
              "$shell = New-Object -ComObject WScript.Shell; "
              "$path = Join-Path ([Environment]::GetFolderPath('Programs')) 'Relay Native.lnk'; "
              "$shortcut = $shell.CreateShortcut($path); "
              "$shortcut.TargetPath = $env:RELAY_INSTALL_TARGET; "
              "$shortcut.WorkingDirectory = Split-Path $env:RELAY_INSTALL_TARGET; "
              "$shortcut.IconLocation = $env:RELAY_INSTALL_TARGET; $shortcut.Save()")
    run("powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script, env=environment, timeout=20)


def install_staged(stage: Path, destination: Path) -> Path | None:
    """Rename on one volume; retain a prior app and roll back a failed publish."""
    if not (stage / "Relay.exe").is_file():
        raise RuntimeError("The staged application is incomplete.")
    if destination.is_symlink():
        raise RuntimeError("The install destination must not be a symbolic link.")
    if destination.exists() and not (destination / "Relay.exe").is_file():
        raise RuntimeError("The destination exists but is not a Relay installation. Choose an empty location.")
    backup = None
    if destination.exists():
        backup = destination.with_name(
            f"{destination.name} previous {dt.datetime.now():%Y%m%d-%H%M%S}-{uuid.uuid4().hex[:8]}")
        destination.rename(backup)
    try:
        stage.rename(destination)
    except OSError:
        if backup:
            backup.rename(destination)
        raise
    return backup


def smoke_test(executable: Path) -> float:
    environment = os.environ.copy()
    for name in ("QT_QPA_PLATFORM", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                 "QML2_IMPORT_PATH", "QML_IMPORT_PATH"):
        environment.pop(name, None)
    # The staged app must find Qt and the CRT alongside itself, never the SDK.
    environment["PATH"] = os.pathsep.join((str(executable.parent),
        str(Path(os.environ["WINDIR"]) / "System32"), os.environ["WINDIR"]))
    started = time.perf_counter()
    run(executable, "--smoke-test", env=environment, timeout=30)
    return round(time.perf_counter() - started, 3)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--qt-dir", type=Path)
    parser.add_argument("--install-directory", type=Path)
    parser.add_argument("--no-open", action="store_true")
    args = parser.parse_args()
    if sys.platform != "win32" or platform.machine().upper() != "AMD64":
        parser.error("This installer requires Windows x64.")
    if sys.version_info[:2] != (3, 12):
        parser.error("Use Python 3.12 for the pinned build tools.")
    source = args.source.resolve(strict=True)
    if not (source / "native/src/main.cpp").is_file():
        parser.error("--source must be a Relay checkout.")
    destination = (args.install_directory or Path(os.environ["LOCALAPPDATA"]) / "Programs/Relay Native").absolute()
    if destination.is_symlink() or destination.resolve().is_relative_to(source) or source.is_relative_to(destination.resolve()):
        parser.error("Use an application directory separate from the source checkout, without a symbolic link.")
    compiler = Path(os.environ["ProgramFiles"]) / "LLVM/bin/clang++.exe"
    if not compiler.is_file() or not shutil.which("link.exe") or not os.environ.get("VCToolsRedistDir"):
        parser.error("Install LLVM 20 and run install-windows.ps1 to prepare the Visual Studio x64 environment.")
    compiler_version = subprocess.check_output([str(compiler), "--version"], text=True)
    if "clang version 20." not in compiler_version:
        parser.error("LLVM 20 is required by the Windows build.")
    for tool, package in (("git", "Git.Git"), ("gh", "GitHub.cli")):
        if not shutil.which(tool):
            parser.error(f"Install {tool}: winget install --id {package} -e; then reopen PowerShell.")
    require_app_closed()
    cache = Path(os.environ["LOCALAPPDATA"]) / "RelayNativeInstaller"
    cache.mkdir(parents=True, exist_ok=True)
    lock = cache / "install.lock"
    try:
        lock_fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        parser.error(f"Another installation is running. If interrupted, remove {lock} and retry.")
    try:
        with tempfile.TemporaryDirectory(prefix="work-", dir=cache) as work_path:
            work = Path(work_path)
            tools = cache / "tools"
            run(sys.executable, "-m", "venv", tools)
            python = tools / "Scripts/python.exe"
            run(python, "-m", "pip", "install", "--disable-pip-version-check",
                "cmake==3.31.6", "ninja==1.13.2", "aqtinstall==3.3.0")
            qt = args.qt_dir
            if not qt:
                qt = cache / "Qt/6.11.1/msvc2022_64"
                if not (qt / "lib/cmake/Qt6/Qt6Config.cmake").is_file():
                    sdk = work / "Qt"
                    run(python, source / "native/tools/install_ci_qt_windows.py", "--output-dir", sdk)
                    (cache / "Qt").mkdir(exist_ok=True)
                    if (cache / "Qt/6.11.1").exists():
                        raise RuntimeError("Cached Qt is incomplete; move it aside and retry.")
                    (sdk / "6.11.1").rename(cache / "Qt/6.11.1")
            qt = qt.resolve(strict=True)
            cmake = tools / "Scripts/cmake.exe"
            environment = os.environ.copy()
            environment["PATH"] = str(tools / "Scripts") + os.pathsep + environment["PATH"]
            build = work / "build"
            run(cmake, "-S", source, "-B", build, "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release", "-DRELAY_BUILD_TESTS=OFF",
                f"-DCMAKE_CXX_COMPILER={compiler.as_posix()}",
                "-DCMAKE_CXX_COMPILER_TARGET=x86_64-pc-windows-msvc",
                "-DCMAKE_LINKER_TYPE=MSVC", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
                f"-DCMAKE_PREFIX_PATH={qt.as_posix()}", "-DRELAY_QT_MIN_VERSION=6.11.1", env=environment)
            run(cmake, "--build", build, "--parallel", "3", env=environment)
            destination.parent.mkdir(parents=True, exist_ok=True)
            # Place staging beside the destination so publication is a rename.
            with tempfile.TemporaryDirectory(prefix=".relay-install-", dir=destination.parent) as stage_path:
                stage = Path(stage_path) / "app"
                run(cmake, "--install", build, "--prefix", stage, env=environment)
                run(qt / "bin/windeployqt.exe", "--release", "--no-translations",
                    "--no-compiler-runtime", "--dir", stage, stage / "Relay.exe", env=environment)
                crt = release_crt_directory(Path(os.environ["VCToolsRedistDir"]))
                for dll in crt.glob("*.dll"):
                    shutil.copy2(dll, stage / dll.name)
                licenses = stage / "resources/licenses"
                licenses.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source / "LICENSE", licenses / "Relay-MIT.txt")
                for name in ("LGPL-3.0.txt", "GPL-3.0.txt"):
                    shutil.copy2(source / "native/licenses" / name, licenses / name)
                for directory in ("LICENSES", "sbom"):
                    if (qt / directory).is_dir():
                        shutil.copytree(qt / directory, licenses / f"Qt-{directory}")
                (licenses / "Qt-source.txt").write_text(
                    "Qt 6.11.1 is dynamically linked, unmodified, under LGPLv3.\n"
                    "Corresponding source: https://download.qt.io/archive/qt/6.11/6.11.1/single/\n"
                    "Git and GitHub CLI are external dependencies.\n"
                    "The Microsoft Visual C++ runtime is redistributed with this local build.\n", encoding="utf-8")
                duration = smoke_test(stage / "Relay.exe")
                report = {"qt": "6.11.1", "platform": "windows-x64", "signed": False,
                          "installed_file_bytes": sum(p.stat().st_size for p in stage.rglob("*") if p.is_file()),
                          "smoke_seconds_including_250ms_exit_delay": duration,
                          "git_bundled": False, "github_cli_bundled": False}
                (stage / "installation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
                require_app_closed()
                backup = install_staged(stage, destination)
            print(f"Installed: {destination / 'Relay.exe'}")
            if backup:
                print(f"Previous app: {backup}")
            print("Git and GitHub CLI must remain installed. This local build is unsigned.")
            try:
                create_start_menu_shortcut(destination)
            except (OSError, subprocess.SubprocessError) as error:
                print(f"Installed successfully, but the Start menu shortcut could not be created: {error}", file=sys.stderr)
            if not args.no_open:
                os.startfile(str(destination / "Relay.exe"))
    finally:
        os.close(lock_fd)
        lock.unlink()


if __name__ == "__main__":
    main()
