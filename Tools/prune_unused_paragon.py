#!/usr/bin/env python3
"""Delete ParagonLtBelica / ParagonTwinblast assets not reachable from game BPs."""

from __future__ import annotations

import argparse
import collections
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "Content"

ENTRY_PACKAGES = [
    "ParagonLtBelica/Characters/Heroes/Belica/Meshes/Belica_Drone/Meshes/Belica_Drone",
    "ParagonLtBelica/FX/Particles/Belica/Abilities/Primary/FX/P_BelicaMuzzle",
    "ParagonLtBelica/FX/Particles/Belica/Abilities/Primary/FX/P_BelicaHitWorld",
    "ParagonTwinblast/FX/Particles/Abilities/VortexGrenade/FX/P_TwinBlast_VortexGrenade_ExplodeBallistic",
]

PATH_RE = re.compile(rb"/Game/(Paragon(?:LtBelica|Twinblast)/[A-Za-z0-9_./+-]+)")
ASSET_EXTS = {".uasset", ".umap"}


def normalize_package(pkg: str) -> str:
    pkg = pkg.replace("\\", "/")
    if pkg.startswith("/Game/"):
        pkg = pkg[len("/Game/") :]
    parts = pkg.split("/")
    last = parts[-1]
    if "." in last:
        parts[-1] = last.split(".", 1)[0]
    return "/".join(parts)


def package_to_file(pkg: str) -> Path | None:
    rel = normalize_package(pkg)
    for ext in (".uasset", ".umap"):
        path = CONTENT / f"{rel}{ext}"
        if path.is_file():
            return path
    # Renamed assets sometimes keep a soft path prefix (e.g. Noisy_Turbulence -> Noisy_Turbulence_103).
    parent = CONTENT / Path(rel).parent
    stem = Path(rel).name
    if parent.is_dir():
        candidates = sorted(
            p
            for p in parent.iterdir()
            if p.is_file()
            and p.suffix.lower() in ASSET_EXTS
            and p.stem.startswith(stem)
        )
        if len(candidates) == 1:
            return candidates[0]
        # Prefer exact-prefix numbered rename: stem_NNN
        numbered = [p for p in candidates if re.fullmatch(re.escape(stem) + r"_\d+", p.stem)]
        if len(numbered) == 1:
            return numbered[0]
    return None


def extract_refs(path: Path) -> set[str]:
    data = path.read_bytes()
    found: set[str] = set()
    for match in PATH_RE.finditer(data):
        found.add(normalize_package(match.group(1).decode("ascii", errors="ignore")))
    return found


def build_keep_set() -> tuple[set[Path], list[str]]:
    keep: set[Path] = set()
    missing: list[str] = []
    queue: collections.deque[Path] = collections.deque()

    for entry in ENTRY_PACKAGES:
        path = package_to_file(entry)
        if path is None:
            missing.append(entry)
            continue
        queue.append(path)

    while queue:
        path = queue.popleft()
        if path in keep:
            continue
        keep.add(path)
        for ref in extract_refs(path):
            ref_path = package_to_file(ref)
            if ref_path is None:
                if ref.startswith("Paragon"):
                    missing.append(ref)
                continue
            if ref_path not in keep:
                queue.append(ref_path)

    return keep, sorted(set(missing))


def iter_paragon_assets() -> list[Path]:
    files: list[Path] = []
    for folder in ("ParagonLtBelica", "ParagonTwinblast"):
        base = CONTENT / folder
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if path.is_file() and path.suffix.lower() in ASSET_EXTS:
                files.append(path)
    return files


def remove_empty_dirs(base: Path) -> int:
    removed = 0
    # deepest-first
    dirs = sorted(
        (p for p in base.rglob("*") if p.is_dir()),
        key=lambda p: len(p.parts),
        reverse=True,
    )
    for directory in dirs:
        try:
            next(directory.iterdir())
        except StopIteration:
            directory.rmdir()
            removed += 1
        except OSError:
            pass
    # try removing base subdirs if empty; never remove Content itself
    return removed


def sizeof(files: list[Path] | set[Path]) -> int:
    return sum(p.stat().st_size for p in files if p.exists())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--execute",
        action="store_true",
        help="Actually delete files. Default is dry-run.",
    )
    parser.add_argument(
        "--write-lists",
        action="store_true",
        help="Write keep/delete lists under Tools/.",
    )
    args = parser.parse_args()

    keep, missing = build_keep_set()
    all_assets = iter_paragon_assets()
    delete = sorted(set(all_assets) - keep, key=lambda p: p.as_posix())
    keep_sorted = sorted(keep, key=lambda p: p.as_posix())

    print(f"Root: {ROOT}")
    print(f"Keep:   {len(keep_sorted):5d} files, {sizeof(keep_sorted) / (1024 ** 2):8.1f} MB")
    print(f"Delete: {len(delete):5d} files, {sizeof(delete) / (1024 ** 2):8.1f} MB")
    print(f"Total:  {len(all_assets):5d} Paragon assets")
    if missing:
        print(f"Missing package paths ({len(missing)}):")
        for item in missing[:30]:
            print(f"  - {item}")

    if args.write_lists or args.execute:
        tools = ROOT / "Tools"
        tools.mkdir(exist_ok=True)
        (tools / "paragon_keep.txt").write_text(
            "\n".join(p.relative_to(ROOT).as_posix() for p in keep_sorted) + "\n",
            encoding="utf-8",
        )
        (tools / "paragon_delete.txt").write_text(
            "\n".join(p.relative_to(ROOT).as_posix() for p in delete) + "\n",
            encoding="utf-8",
        )
        print("Wrote Tools/paragon_keep.txt and Tools/paragon_delete.txt")

    if not args.execute:
        print("Dry-run only. Re-run with --execute to delete.")
        return 0

    deleted = 0
    failed = 0
    for path in delete:
        try:
            path.unlink()
            deleted += 1
        except OSError as exc:
            failed += 1
            print(f"FAIL {path}: {exc}", file=sys.stderr)

    emptied = 0
    for folder in ("ParagonLtBelica", "ParagonTwinblast"):
        base = CONTENT / folder
        if base.is_dir():
            emptied += remove_empty_dirs(base)

    print(f"Deleted {deleted} files ({failed} failures). Removed {emptied} empty dirs.")
    remaining = iter_paragon_assets()
    print(
        f"Remaining Paragon assets: {len(remaining)} "
        f"({sizeof(remaining) / (1024 ** 2):.1f} MB)"
    )
    # Verify keep set still present
    missing_keep = [p for p in keep_sorted if not p.exists()]
    if missing_keep:
        print(f"ERROR: {len(missing_keep)} keep files missing after delete!", file=sys.stderr)
        for path in missing_keep[:20]:
            print(f"  - {path}", file=sys.stderr)
        return 2
    print("Keep set intact.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
