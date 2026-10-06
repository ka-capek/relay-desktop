"""Download, verify and lay out the Git and GitHub CLI that installers bundle.

Versions, download URLs and SHA-256 digests are pinned in
native/packaging/runtimes.json; a download that does not match is rejected.
The output uses the runtime/ layout that Relay already resolves:

  <output>/gh/<platform>/gh[.exe]
  <output>/git/<platform>/...          (Windows only)
  <output>/licenses/<platform>/...      license texts and source locations

macOS has no official relocatable Git build, so Relay uses the Git from the
Xcode Command Line Tools or Homebrew there and bundles only the GitHub CLI.

  python native/tools/fetch_runtimes.py --platform win-x64 --output runtime
"""
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

MANIFEST = Path(__file__).resolve().parents[1] / "packaging" / "runtimes.json"

# Parts of Git for Windows Relay never runs. Git Credential Manager and its
# .NET runtime are disabled on every network operation Relay performs; the
# Tcl/Tk GUIs, Vim and the documentation are never invoked (Relay passes
# messages with -m and sets GIT_EDITOR=true). Git LFS stays: Git itself runs
# it for any repository that uses LFS, and the system configuration marks
# that filter as required.
GIT_EXCLUDES = (
    "mingw64/bin/git-credential-manager*",
    "mingw64/libexec/git-core/git-credential-manager*",
    "mingw64/bin/Avalonia*.dll",
    "mingw64/bin/Microsoft.*.dll",
    "mingw64/bin/System.*.dll",
    "mingw64/bin/*SkiaSharp*.dll",
    "mingw64/bin/*HarfBuzzSharp*.dll",
    "mingw64/bin/*.deps.json",
    "mingw64/bin/*.runtimeconfig.json",
    "mingw64/bin/tclsh*.exe",
    "mingw64/bin/wish*.exe",
    "mingw64/lib/tcl8*",
    "mingw64/lib/tk8*",
    "mingw64/libexec/git-core/git-gui*",
    "mingw64/share/git-gui",
    "mingw64/share/gitk",
    "mingw64/share/doc",
    "usr/share/doc",
    "usr/share/man",
    "usr/share/info",
    "usr/share/vim",
    "usr/bin/*vim*.exe",
    "usr/bin/view.exe",
    "usr/bin/ex.exe",
    "git-bash.exe",
    "git-cmd.exe",
    "README.portable",
)


def download(url: str, sha256: str, cache: Path) -> Path:
    cache.mkdir(parents=True, exist_ok=True)
    target = cache / url.rsplit("/", 1)[1]
    if not target.is_file() or digest(target) != sha256:
        partial = target.with_suffix(target.suffix + ".part")
        print(f"Downloading {url}", flush=True)
        with urllib.request.urlopen(url, timeout=300) as response, partial.open("wb") as output:
            shutil.copyfileobj(response, output)
        partial.replace(target)
    actual = digest(target)
    if actual != sha256:
        target.unlink(missing_ok=True)
        raise RuntimeError(f"{target.name} has SHA-256 {actual}, expected {sha256}.")
    return target


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            hasher.update(block)
    return hasher.hexdigest()


def extract_zip(archive: Path, destination: Path) -> None:
    with zipfile.ZipFile(archive) as bundle:
        for member in bundle.infolist():
            # Refuse absolute paths and parent traversal before extracting.
            target = (destination / member.filename).resolve()
            if not target.is_relative_to(destination.resolve()):
                raise RuntimeError(f"Refusing unsafe archive member {member.filename}.")
        bundle.extractall(destination)


def extract_portable_git(archive: Path, destination: Path) -> None:
    """PortableGit is a 7-Zip self-extractor; 7z reads it on any platform."""
    seven_zip = shutil.which("7z") or shutil.which("7za")
    if seven_zip:
        subprocess.run([seven_zip, "x", "-y", f"-o{destination}", str(archive)],
                       check=True, stdout=subprocess.DEVNULL)
    elif os.name == "nt":
        subprocess.run([str(archive), "-y", f"-o{destination}"], check=True)
    else:
        raise RuntimeError("Extracting PortableGit needs 7-Zip (7z) on PATH.")


def remove_credential_helper(gitconfig: Path) -> None:
    """Drop the system [credential] section, which selects the excluded
    Credential Manager through a helper-selector dialog. Relay supplies
    GitHub credentials itself; a helper in the user's own configuration
    still applies to other hosts."""
    kept, skipping = [], False
    for line in gitconfig.read_text(encoding="utf-8").splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("["):
            skipping = stripped.lower().startswith("[credential")
        if not skipping:
            kept.append(line)
    gitconfig.write_text("".join(kept), encoding="utf-8")


def remove_excluded(root: Path) -> int:
    removed = 0
    for path in sorted(root.rglob("*"), key=lambda item: len(item.parts), reverse=True):
        relative = path.relative_to(root).as_posix()
        if any(fnmatch.fnmatchcase(relative, pattern) for pattern in GIT_EXCLUDES) and path.exists():
            if path.is_dir() and not path.is_symlink():
                shutil.rmtree(path)
            else:
                path.unlink()
            removed += 1
    return removed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", choices=("win-x64", "mac-arm64"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache", type=Path, help="Keep downloads here between runs.")
    arguments = parser.parse_args()

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    platform = arguments.platform
    output = arguments.output.resolve()
    licenses = output / "licenses" / platform
    licenses.mkdir(parents=True, exist_ok=True)
    notices = []
    with tempfile.TemporaryDirectory(prefix="relay-runtimes-") as scratch_path:
        scratch = Path(scratch_path)
        cache = arguments.cache.resolve() if arguments.cache else scratch / "downloads"
        for tool, folder_name, license_name in (("gh", "gh", "GitHub-CLI-LICENSE.txt"),
                                                ("git", "git", "Git-for-Windows-LICENSE.txt")):
            entry = manifest[tool]
            pinned = entry["platforms"].get(platform)
            if not pinned:
                continue
            archive = download(pinned["url"], pinned["sha256"], cache)
            unpacked = scratch / tool
            if tool == "git":
                extract_portable_git(archive, unpacked)
            else:
                extract_zip(archive, unpacked)
            if not (unpacked / pinned["executable"]).is_file():
                raise RuntimeError(f"{archive.name} does not contain {pinned['executable']}.")
            destination = output / folder_name / platform
            if destination.exists():
                shutil.rmtree(destination)
            if tool == "git":
                removed = remove_excluded(unpacked)
                remove_credential_helper(unpacked / "etc" / "gitconfig")
                shutil.copytree(unpacked, destination, symlinks=True)
                print(f"Git {entry['version']}: removed {removed} unused entries", flush=True)
            else:
                destination.mkdir(parents=True)
                executable = destination / Path(pinned["executable"]).name
                shutil.copy2(unpacked / pinned["executable"], executable)
                executable.chmod(0o755)
            shutil.copy2(unpacked / pinned["licenseFile"], licenses / license_name)
            notices.append(f"{'GitHub CLI' if tool == 'gh' else 'Git for Windows'} {entry['version']}\n"
                           f"  License: {entry['license']} ({license_name})\n"
                           f"  Source: {entry['source']}\n"
                           f"  Binary: {pinned['url']}\n  SHA-256: {pinned['sha256']}\n")
    (licenses / "bundled-runtimes.txt").write_text(
        "Relay bundles these unmodified third-party programs.\n\n" + "\n".join(notices), encoding="utf-8")
    print(f"Runtimes for {platform} are in {output}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:  # Report one clear line instead of a traceback.
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
