#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 B612S <sem.alimov@inbox.ru>

"""Write a de-duplicated compilation database for the `lint` target.

Several CMake targets compile the same core sources (luma, luma_testcore,
tst_media_open).  run-clang-tidy visits each file once, but clang-tidy itself
would then analyze every matching entry in the original database, reporting the
same diagnostics two or three times.  Keep the first entry per source file.

Usage: prepare-tidy-db.py <compile_commands.json> <output.json>
"""

import json
import os
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <compile_commands.json> <output.json>",
              file=sys.stderr)
        return 2

    with open(sys.argv[1], encoding="utf-8") as source:
        entries = json.load(source)

    unique = {}
    for entry in entries:
        directory = entry.get("directory", "")
        path = os.path.normpath(os.path.join(directory, entry["file"]))
        unique.setdefault(path, entry)

    output_dir = os.path.dirname(os.path.abspath(sys.argv[2]))
    os.makedirs(output_dir, exist_ok=True)
    with open(sys.argv[2], "w", encoding="utf-8") as target:
        json.dump(list(unique.values()), target, indent=1)
        target.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
