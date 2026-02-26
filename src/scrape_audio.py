#!/usr/bin/env python3
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from urllib.parse import quote_plus
from urllib.request import urlopen


def _safe_filename(name: str) -> str:
    cleaned = re.sub(r'[\\/:*?"<>|]+', "_", name).strip()
    return cleaned or "archive_audio"


def _download_archive_audio(query: str, output_dir: Path) -> int:
    search_query = f"({query}) AND mediatype:audio"
    search_url = (
        "https://archive.org/advancedsearch.php?"
        f"q={quote_plus(search_query)}&fl[]=identifier&fl[]=title"
        "&rows=5&page=1&output=json"
    )

    try:
        with urlopen(search_url, timeout=30) as response:
            data = json.load(response)
    except Exception as exc:
        print(f"Internet Archive search failed: {exc}")
        return 1

    docs = data.get("response", {}).get("docs", [])
    if not docs:
        print("Internet Archive: no results found.")
        return 1

    identifier = docs[0].get("identifier")
    title = docs[0].get("title") or identifier
    if not identifier:
        print("Internet Archive: missing identifier in search result.")
        return 1

    metadata_url = f"https://archive.org/metadata/{identifier}"
    try:
        with urlopen(metadata_url, timeout=30) as response:
            meta = json.load(response)
    except Exception as exc:
        print(f"Internet Archive metadata fetch failed: {exc}")
        return 1

    files = meta.get("files", [])
    if not files:
        print("Internet Archive: no files available for this item.")
        return 1

    def is_mp3(file_item: dict) -> bool:
        name = file_item.get("name", "").lower()
        fmt = str(file_item.get("format", "")).lower()
        return name.endswith(".mp3") or "mp3" in fmt

    mp3_files = [f for f in files if is_mp3(f)]
    if not mp3_files:
        print("Internet Archive: no MP3 files found for this item.")
        return 1

    chosen = mp3_files[0]
    file_name = chosen.get("name")
    if not file_name:
        print("Internet Archive: chosen file missing name.")
        return 1

    download_url = f"https://archive.org/download/{identifier}/{file_name}"
    target_path = output_dir / f"{_safe_filename(title)}.mp3"

    try:
        with urlopen(download_url, timeout=60) as response, open(target_path, "wb") as out_file:
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                out_file.write(chunk)
    except Exception as exc:
        print(f"Internet Archive download failed: {exc}")
        return 1

    print(f"Downloaded from Internet Archive: {target_path.name}")
    return 0


def main() -> int:
    if len(sys.argv) < 2:
        query = input("Song title (and artist): ").strip()
        if not query:
            print("Usage: python3 src/youtube_audio.py \"Song Title Artist\"")
            return 1
    else:
        query = " ".join(sys.argv[1:]).strip()

    if shutil.which("yt-dlp") is None:
        print("yt-dlp not found in PATH. Install with: pip install -U yt-dlp")
        return 1

    if not query:
        print("Please provide a non-empty song title.")
        return 1

    project_root = Path(__file__).resolve().parents[1]
    output_dir = project_root / "resources"
    output_dir.mkdir(parents=True, exist_ok=True)

    output_template = str(output_dir / "%(title)s.%(ext)s")
    cmd = [
        "yt-dlp",
        "ytsearch1:" + query,
        "--no-playlist",
        "--js-runtimes",
        "node,deno",
        "-x",
        "--audio-format",
        "mp3",
        "-o",
        output_template,
    ]

    result = subprocess.call(cmd)
    if result == 0:
        return 0

    print("YouTube download failed. Trying Internet Archive...")
    return _download_archive_audio(query, output_dir)


if __name__ == "__main__":
    raise SystemExit(main())
