#!/usr/bin/env python3
import argparse
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
    parser = argparse.ArgumentParser(description="Download audio from YouTube or Internet Archive.")
    parser.add_argument("query", nargs="*", help="YouTube URL, or song title/artist to search.")
    parser.add_argument("-o", "--output", help="Output filename (e.g. combined_portable.mp3).")
    parser.add_argument("--mvp-demo", action="store_true", help="Also copy output to MVP demo/.")
    args = parser.parse_args()

    query = " ".join(args.query).strip()
    if not query:
        query = input("Song title (and artist) or YouTube URL: ").strip()
    if not query:
        print("Usage: python3 src/scrape_audio.py \"URL or Song Title\" [-o output.mp3]")
        return 1

    if shutil.which("yt-dlp") is None:
        print("yt-dlp not found in PATH. Install with: pip install -U yt-dlp")
        return 1

    project_root = Path(__file__).resolve().parents[1]
    output_dir = project_root / "resources"
    output_dir.mkdir(parents=True, exist_ok=True)

    if args.output:
        out_name = args.output if args.output.endswith(".mp3") else f"{args.output}.mp3"
        output_template = str(output_dir / out_name)
    else:
        output_template = str(output_dir / "%(title)s.%(ext)s")
    # Accept direct YouTube/yt-dlp URLs; otherwise treat as search query
    is_url = "youtube.com" in query or "youtu.be" in query
    url_or_search = query if is_url else "ytsearch1:" + query

    cmd = [
        "yt-dlp",
        url_or_search,
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
    if result != 0:
        print("YouTube download failed. Trying Internet Archive...")
        return _download_archive_audio(query, output_dir)

    if args.mvp_demo:
        if args.output:
            out_name = args.output if args.output.endswith(".mp3") else f"{args.output}.mp3"
            src = output_dir / out_name
            if src.exists():
                mvp_demo = project_root / "MVP demo"
                mvp_demo.mkdir(parents=True, exist_ok=True)
                dest = mvp_demo / src.name
                shutil.copy2(src, dest)
                print(f"Copied to {dest}")
            else:
                print(f"Warning: {src} not found; skipping MVP demo copy.", file=sys.stderr)
        else:
            print("--mvp-demo requires -o/--output to know which file to copy.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
