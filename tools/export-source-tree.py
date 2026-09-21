#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], text=True).strip()


def tracked_files() -> list[str]:
    data = subprocess.check_output(["git", "ls-files", "-z"])
    return [item.decode("utf-8") for item in data.split(b"\0") if item]


def is_text(data: bytes) -> bool:
    if b"\0" in data:
        return False
    try:
        data.decode("utf-8")
        return True
    except UnicodeDecodeError:
        return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--ref", required=True)
    args = parser.parse_args()

    root = Path(git("rev-parse", "--show-toplevel"))
    commit = git("rev-parse", "HEAD")
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)

    with output.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("KRISCC EXACT SOURCE TREE\n")
        handle.write(f"REPOSITORY: {args.repository}\n")
        handle.write(f"REF: {args.ref}\n")
        handle.write(f"COMMIT: {commit}\n")
        handle.write("FORMAT: tracked UTF-8 text files inline; symlinks and binary files represented by markers\n\n")

        for relative in tracked_files():
            path = root / relative
            handle.write(f"===== FILE: {relative} =====\n")

            if path.is_symlink():
                handle.write(f"[[SYMLINK -> {os.readlink(path)}]]\n\n")
                continue

            data = path.read_bytes()
            if not is_text(data):
                digest = hashlib.sha256(data).hexdigest()
                handle.write(f"[[BINARY FILE OMITTED size={len(data)} sha256={digest}]]\n\n")
                continue

            text = data.decode("utf-8")
            handle.write(text)
            if text and not text.endswith("\n"):
                handle.write("\n")
            handle.write(f"===== END FILE: {relative} =====\n\n")

    os.chmod(output, 0o600)
    return 0


if __name__ == "__main__":
    sys.exit(main())
