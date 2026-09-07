"""Install the pinned Qt MSVC package despite aqt 3.3.0's old repository layout.

Qt 6.11 splits Windows metadata by compiler. This narrow workaround changes
only that directory suffix; aqt still verifies and extracts the official files.
Upstream issue: https://github.com/miurahr/aqtinstall/issues/1007
Remove the wrapper when upgrading to an aqt release that fixes the issue.
"""
from importlib.metadata import version
from pathlib import Path
import os


def main():
    if version("aqtinstall") != "3.3.0":
        raise RuntimeError("Re-evaluate the Qt layout workaround before changing aqt.")
    from aqt.archives import QtArchives
    from aqt.installer import Cli

    original_extension = QtArchives._arch_ext

    def extension(archives):
        if (archives.os_name == "windows" and archives.target == "desktop"
                and str(archives.version) == "6.11.1"
                and archives.arch == "win64_msvc2022_64"):
            return "_msvc2022_64"
        return original_extension(archives)

    QtArchives._arch_ext = extension
    destination = Path(os.environ["RUNNER_TEMP"]) / "relay-qt"
    result = Cli().run(["install-qt", "windows", "desktop", "6.11.1",
                        "win64_msvc2022_64", "--outputdir", str(destination)])
    if result:
        raise SystemExit(result)
    root = destination / "6.11.1" / "msvc2022_64"
    if not (root / "lib/cmake/Qt6/Qt6Config.cmake").is_file():
        raise RuntimeError(f"Qt installation is incomplete: {root}")
    with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as env:
        env.write(f"QT_ROOT_DIR={root.as_posix()}\n")
    with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8") as paths:
        paths.write(f"{root / 'bin'}\n")


if __name__ == "__main__":
    main()
