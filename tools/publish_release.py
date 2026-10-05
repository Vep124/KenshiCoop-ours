#!/usr/bin/env python3
"""Package OUR KenshiCoop build as a launcher-channel release.

Produces, in --out:
  KenshiCoop-kit-<version>.zip   kit the launcher installs (KenshiCoop/ inside)
  channel.json                   update channel the launcher reads
  SHA256SUMS.txt                 checksums of the two files above
  RELEASE-README.txt             manual upload steps (GitHub / file host / LAN)

Channel modes:
  --repo USER/REPO   launcher uses GitHub releases/latest of that repo
  --base-url URL     static channel: asset is downloaded from URL/<zip name>
  (neither)          static channel with a relative asset path: put channel.json
                     and the zip in the same folder (portable / shared drive)

Examples:
  python tools\\publish_release.py --kit dist\\KenshiCoop-ours-v0.52 --version v0.52 --protocol 56 --out dist\\releases\\v0.52
  python tools\\publish_release.py --kit dist\\KenshiCoop-ours-v0.52 --version v0.52 --protocol 56 --out dist\\releases\\v0.52 --repo USER/KenshiCoop-our
  python tools\\publish_release.py --kit dist\\KenshiCoop-ours-v0.52 --version v0.52 --protocol 56 --out dist\\releases\\v0.52 --base-url https://github.com/USER/REPO/releases/download/v0.52
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import time
import zipfile
from pathlib import Path

REQUIRED = ["KenshiCoop.dll", "KenshiCoop.mod", "RE_Kenshi.json"]
DEFAULT_NAME = "Kenshi Co-op (наш форк, protocol-56)"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Package our KenshiCoop kit + channel.json")
    parser.add_argument("--kit", required=True, type=Path, help="папка с KenshiCoop.dll/.mod/RE_Kenshi.json")
    parser.add_argument("--version", required=True, help="версия релиза, напр. v0.52")
    parser.add_argument("--protocol", required=True, type=int, help="версия сетевого протокола, напр. 56")
    parser.add_argument("--out", required=True, type=Path, help="куда положить релиз")
    parser.add_argument("--repo", default="", help="USER/REPO для GitHub-канала (режим releases/latest)")
    parser.add_argument("--base-url", default="", help="базовый URL для static-канала (без имени файла)")
    parser.add_argument("--name", default=DEFAULT_NAME, help="человекочитаемое имя канала")
    args = parser.parse_args()

    kit = args.kit.resolve()
    out = args.out.resolve()
    missing = [n for n in REQUIRED if not (kit / n).is_file()]
    if missing:
        print(f"ОШИБКА: в {kit} нет файлов: {', '.join(missing)}")
        return 2

    version = args.version.strip()
    zip_name = f"KenshiCoop-kit-{version}.zip"

    out.mkdir(parents=True, exist_ok=True)
    staging = out / "_staging"
    if staging.exists():
        shutil.rmtree(staging)
    mod_dir = staging / "KenshiCoop"
    mod_dir.mkdir(parents=True)

    for f in sorted(kit.iterdir()):
        if f.is_file():
            shutil.copy2(f, mod_dir / f.name)

    dll_sha = sha256_file(mod_dir / "KenshiCoop.dll")
    coop_version = {
        "schema": 1,
        "name": args.name,
        "version": version,
        "protocol": args.protocol,
        "dll_sha256": dll_sha,
        "built_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    (mod_dir / "coop_version.json").write_text(
        json.dumps(coop_version, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    zip_path = out / zip_name
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for f in sorted(mod_dir.rglob("*")):
            if f.is_file():
                z.write(f, arcname=str(Path("KenshiCoop") / f.relative_to(mod_dir)))
    shutil.rmtree(staging)
    zip_sha = sha256_file(zip_path)
    zip_size = zip_path.stat().st_size

    channel: dict = {
        "schema": 1,
        "name": args.name,
        "version": version,
        "protocol": args.protocol,
    }
    if args.repo:
        channel.update({"source": "github", "repo": args.repo.strip()})
        channel["page"] = f"https://github.com/{args.repo.strip()}"
    else:
        base = args.base_url.strip().rstrip("/")
        url = f"{base}/{zip_name}" if base else zip_name
        channel.update(
            {
                "source": "static",
                "asset": {"name": zip_name, "url": url, "sha256": zip_sha, "size": zip_size},
            }
        )
        if base:
            channel["page"] = base

    channel_path = out / "channel.json"
    channel_path.write_text(json.dumps(channel, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    sums = out / "SHA256SUMS.txt"
    sums.write_text(
        f"{zip_sha}  {zip_name}\n{sha256_file(channel_path)}  channel.json\n", encoding="utf-8"
    )

    mode = "github" if args.repo else "static"
    readme = out / "RELEASE-README.txt"
    readme.write_text(
        "\n".join(
            [
                f"KenshiCoop release {version} (protocol {args.protocol})",
                "=" * 60,
                "",
                f"Артефакт:   {zip_name}  ({zip_size} байт)",
                f"Канал:      channel.json (режим: {mode})",
                f"DLL SHA-256:{dll_sha}",
                "",
                "Куда положить файлы:",
                "  * GitHub (рекомендуется): создайте ПУБЛИЧНЫЙ репозиторий, затем",
                f"    Releases -> Draft a new release -> tag {version};",
                f"    приложите {zip_name}; если канал --repo, этого достаточно.",
                "    Для static-канала дополнительно приложите channel.json и укажите",
                "    --base-url на папку загрузки релиза.",
                "  * Любой файлхостинг со прямыми ссылками: залейте zip, соберите",
                "    channel.json с --base-url на прямую ссылку.",
                "  * LAN / общая папка / флешка: положите channel.json рядом с",
                f"    {zip_name}; лаунчер увидит канал, если channel.json лежит около exe",
                "    (или задан KENSHICOOP_CHANNEL=путь\\channel.json).",
                "",
                "ВАЖНО: обе стороны должны стоять на одном протоколе (",
                f"наш билд = {args.protocol}); официальный v0.51 (protocol 55) не соединится.",
                "",
            ]
        ),
        encoding="utf-8",
    )

    print(f"Готово: {zip_path}")
    print(f"        {channel_path}")
    print(f"        {sums}")
    print(f"DLL SHA-256: {dll_sha}")
    print(f"ZIP SHA-256: {zip_sha}")
    return 0


if __name__ == "__main__":
    if sys.stdout is not None:
        try:
            sys.stdout.reconfigure(encoding="utf-8")
        except (AttributeError, ValueError):
            pass
    sys.exit(main())
