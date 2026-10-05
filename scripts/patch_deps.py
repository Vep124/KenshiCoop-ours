"""Patch the vendored KenshiLib_deps for the v100 (VC10) toolchain.

Pinned revision: b566d74^ (e75769b, 2026-05-02) - the last revision whose header
layout matches the v0.51 source (kenshi/CombatClass.h lives at the old path).

The stock deps do not compile as-shipped on VC10:
  1. `enum BuildingDesignation` is defined in BOTH kenshi/Platoon.h and
     kenshi/Building/Building.h; VC10 rejects even identical enum redefinitions.
     -> guard both definitions with one shared include guard.
  2. kenshi/Building/CraftingBuilding.h declares `std::deque<CraftingItem>`
     while CraftingItem is only forward-declared; VC10's deque needs a complete
     type. deque's layout does not depend on T, so a stub keeps member offsets
     intact. The plugin only takes addresses of CraftingBuilding virtuals and
     never touches the deque contents.

Idempotent: safe to run repeatedly.
"""
import re
import sys
from pathlib import Path

DEPS = Path(__file__).resolve().parents[1] / "third_party" / "KenshiLib_deps"
INC = DEPS / "KenshiLib" / "Include" / "kenshi"

GUARD = "KENSHICOOP_BD_ENUM_DEFINED"

ENUM_RE = re.compile(
    r"(enum\s+BuildingDesignation\s*\{.*?\}\s*;)", re.S
)


def guard_enum(path: Path) -> bool:
    text = path.read_text(encoding="utf-8", errors="replace")
    if GUARD in text:
        return False
    m = ENUM_RE.search(text)
    if not m:
        raise SystemExit(f"enum BuildingDesignation not found in {path}")
    replacement = (
        f"#ifndef {GUARD}\n#define {GUARD}\n"
        f"{m.group(1)}\n"
        f"#endif"
    )
    text = text[: m.start(1)] + replacement + text[m.end(1):]
    path.write_text(text, encoding="utf-8")
    return True


def stub_crafting_item(path: Path) -> bool:
    text = path.read_text(encoding="utf-8", errors="replace")
    if "KENSHICOOP_CRAFTINGITEM_STUB" in text:
        return False
    needle = "class CraftingItem;"
    if needle not in text:
        raise SystemExit(f"'class CraftingItem;' not found in {path}")
    stub = (
        "// KENSHICOOP_CRAFTINGITEM_STUB: VC10 std::deque needs a complete type;\n"
        "// deque layout is independent of T and the plugin never touches its contents.\n"
        "class CraftingItem { public: int _kc_stub; };"
    )
    text = text.replace(needle, stub, 1)
    path.write_text(text, encoding="utf-8")
    return True


def main() -> int:
    changed = 0
    for rel in ("Platoon.h", "Building/Building.h"):
        p = INC / rel
        if guard_enum(p):
            changed += 1
            print(f"guarded enum in {rel}")
        else:
            print(f"already guarded: {rel}")
    p = INC / "Building" / "CraftingBuilding.h"
    if stub_crafting_item(p):
        changed += 1
        print("stubbed CraftingItem in Building/CraftingBuilding.h")
    else:
        print("already stubbed: Building/CraftingBuilding.h")
    print(f"patched files: {changed}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
