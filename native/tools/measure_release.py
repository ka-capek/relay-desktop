#!/usr/bin/env python3
"""Record reproducible Relay package size and process-memory measurements."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
from typing import Any


SCENARIOS = ("idle", "repository-open", "large-diff", "long-history")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_size(path: Path) -> dict[str, int | None]:
    apparent = 0
    allocated = 0
    allocated_available = True
    files = 0
    directories = 0
    seen_inodes: set[tuple[int, int]] = set()

    def add(candidate: Path) -> None:
        nonlocal apparent, allocated, allocated_available, files, directories
        stat = candidate.lstat()
        identity = (stat.st_dev, stat.st_ino)
        if identity in seen_inodes:
            return
        seen_inodes.add(identity)
        apparent += stat.st_size
        if hasattr(stat, "st_blocks"):
            allocated += stat.st_blocks * 512
        else:
            allocated_available = False
        if candidate.is_dir() and not candidate.is_symlink():
            directories += 1
        else:
            files += 1

    add(path)
    if path.is_dir():
        for directory, child_directories, child_files in os.walk(path, followlinks=False):
            directory_path = Path(directory)
            for name in child_directories:
                add(directory_path / name)
            for name in child_files:
                add(directory_path / name)
    return {
        "apparent_bytes": apparent,
        "allocated_bytes": allocated if allocated_available else None,
        "files": files,
        "directories": directories,
    }


def macos_memory(pids: list[int]) -> dict[str, Any]:
    with tempfile.NamedTemporaryFile(prefix="relay-footprint-", suffix=".json", delete=False) as output:
        footprint_path = Path(output.name)
    try:
        command = ["/usr/bin/footprint"]
        for pid in pids:
            command.extend(("--pid", str(pid)))
        command.extend(("--json", str(footprint_path)))
        completed = subprocess.run(command, check=False, capture_output=True, text=True, timeout=60)
        if completed.returncode != 0:
            raise RuntimeError(f"footprint failed ({completed.returncode}): {completed.stderr.strip()}")
        raw = json.loads(footprint_path.read_text(encoding="utf-8"))
    finally:
        footprint_path.unlink(missing_ok=True)

    processes = []
    for process in raw.get("processes", []):
        auxiliary = process.get("auxiliary", {})
        dirty = sum(
            int(category.get("dirty", 0))
            for category in process.get("categories", {}).values()
            if isinstance(category, dict)
        )
        processes.append(
            {
                "pid": process.get("pid"),
                "name": process.get("name"),
                "physical_footprint_bytes": auxiliary.get("phys_footprint", process.get("footprint")),
                "physical_footprint_peak_bytes": auxiliary.get("phys_footprint_peak"),
                "dirty_bytes": dirty,
                "translated": process.get("translated"),
            }
        )
    return {
        "tool": "footprint",
        "metric": "physical_footprint",
        "processes": processes,
        "aggregate_physical_footprint_bytes": sum(
            int(process.get("physical_footprint_bytes") or 0) for process in processes
        ),
        "aggregate_dirty_bytes": sum(int(process.get("dirty_bytes") or 0) for process in processes),
        "tool_total_footprint_bytes": raw.get("total footprint"),
        "warnings": raw.get("warnings", []),
        "errors": raw.get("errors", []),
    }


def windows_memory(pids: list[int]) -> dict[str, Any]:
    ids = ",".join(str(pid) for pid in pids)
    script = (
        f"@(Get-Process -Id {ids} | Select-Object Id,ProcessName,PrivateMemorySize64,"
        "WorkingSet64,PeakWorkingSet64) | ConvertTo-Json -Compress"
    )
    completed = subprocess.run(
        ["powershell.exe", "-NoLogo", "-NoProfile", "-NonInteractive", "-Command", script],
        check=False,
        capture_output=True,
        text=True,
        timeout=60,
    )
    if completed.returncode != 0:
        raise RuntimeError(f"Get-Process failed ({completed.returncode}): {completed.stderr.strip()}")
    raw = json.loads(completed.stdout)
    if isinstance(raw, dict):
        raw = [raw]
    processes = [
        {
            "pid": process["Id"],
            "name": process["ProcessName"],
            "private_bytes": process["PrivateMemorySize64"],
            "working_set_bytes": process["WorkingSet64"],
            "peak_working_set_bytes": process["PeakWorkingSet64"],
        }
        for process in raw
    ]
    return {
        "tool": "Get-Process",
        "metric": "private_bytes",
        "processes": processes,
        "aggregate_private_bytes": sum(int(process["private_bytes"]) for process in processes),
        "aggregate_working_set_bytes": sum(int(process["working_set_bytes"]) for process in processes),
    }


def memory(pids: list[int]) -> dict[str, Any] | None:
    if not pids:
        return None
    system = platform.system()
    if system == "Darwin":
        return macos_memory(pids)
    if system == "Windows":
        return windows_memory(pids)
    raise RuntimeError("Memory measurement is implemented only for native macOS and Windows hosts")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", required=True, choices=SCENARIOS)
    parser.add_argument("--stage", required=True, type=Path, help="Installed app bundle or staged install directory")
    parser.add_argument("--artifact", type=Path, help="DMG or NSIS artifact")
    parser.add_argument("--pid", action="append", type=int, default=[], help="Process ID; repeat for a multi-process baseline")
    parser.add_argument("--fixture", default="", help="Stable fixture/repository identifier used for this scenario")
    parser.add_argument("--notes", default="")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--force", action="store_true", help="Replace an existing measurement record")
    arguments = parser.parse_args()

    stage = arguments.stage.resolve(strict=True)
    artifact = arguments.artifact.resolve(strict=True) if arguments.artifact else None
    output = arguments.output.resolve()
    if output.exists() and not arguments.force:
        parser.error(f"output exists (use --force to replace it): {output}")
    if any(pid <= 0 for pid in arguments.pid):
        parser.error("--pid values must be positive")

    record: dict[str, Any] = {
        "schema_version": 1,
        "recorded_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "scenario": arguments.scenario,
        "fixture": arguments.fixture or None,
        "notes": arguments.notes or None,
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "version": platform.version(),
            "machine": platform.machine(),
            "platform": platform.platform(),
            "python": platform.python_version(),
        },
        "stage": {"path": str(stage), **tree_size(stage)},
        "artifact": None,
        "memory": memory(arguments.pid),
        "invocation": {
            "cwd": str(Path.cwd()),
            "argv": [sys.executable, *sys.argv],
            "pids": arguments.pid,
        },
        "metric_policy": {
            "macos": "footprint auxiliary.phys_footprint plus dirty category bytes",
            "windows": "Get-Process PrivateMemorySize64 plus WorkingSet64",
            "warning": "Do not compare summed RSS with physical footprint/private bytes.",
        },
    }
    if artifact:
        artifact_stat = artifact.stat()
        record["artifact"] = {
            "path": str(artifact),
            "bytes": artifact_stat.st_size,
            "sha256": sha256(artifact),
        }

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f".{output.name}.{os.getpid()}.tmp")
    temporary.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, output)
    print(output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"measurement failed: {error}", file=sys.stderr)
        raise SystemExit(1)
