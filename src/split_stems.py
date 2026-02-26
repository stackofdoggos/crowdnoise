#!/usr/bin/env python3
import json
import re
import subprocess
import sys
from pathlib import Path
from importlib.util import find_spec


def has_stem_files(stem_output_dir: Path) -> bool:
    return any(
        p.is_file() and p.suffix.lower() in {".mp3", ".wav", ".flac"}
        for p in stem_output_dir.iterdir()
    )


def get_audio_metadata(song_path: Path) -> tuple[int, int]:
    try:
        import torchaudio

        info = torchaudio.info(str(song_path))
        sample_rate = int(info.sample_rate) if info.sample_rate else 44100
        num_frames = int(info.num_frames) if info.num_frames else 0
        length_ms = int(round((num_frames / sample_rate) * 1000)) if sample_rate else 0
        return sample_rate, length_ms
    except Exception:
        return 44100, 0


def build_song_id(title: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")
    return slug or "unknown-song"


def write_songdetails(project_root: Path, model_output_dir: Path, song_path: Path) -> None:
    stem_output_dir = model_output_dir / song_path.stem
    if not stem_output_dir.is_dir() or not has_stem_files(stem_output_dir):
        return

    stem_files = sorted(
        p
        for p in stem_output_dir.iterdir()
        if p.is_file() and p.suffix.lower() in {".mp3", ".wav", ".flac"}
    )
    instruments = {
        stem_file.stem: {
            "original_path": str(stem_file.relative_to(project_root)),
        }
        for stem_file in stem_files
    }

    sample_rate, song_length_ms = get_audio_metadata(song_path)
    song_details = {
        "schema_version": 1,
        "song_id": build_song_id(song_path.stem),
        "title": song_path.stem,
        "sr": sample_rate,
        "song_length_ms": song_length_ms,
        "instruments": instruments,
    }
    output_path = stem_output_dir / "songdetails.json"
    output_path.write_text(json.dumps(song_details, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    project_root = Path(__file__).resolve().parents[1]
    input_dir = project_root / "resources"
    output_dir = project_root / "output"
    model_name = "htdemucs_6s"

    if find_spec("demucs") is None:
        print("demucs not found in this Python environment.")
        print("Install with: pip install -U demucs")
        return 1
    if find_spec("torchcodec") is None:
        print("torchcodec not found in this Python environment.")
        print("Install with: pip install -U torchcodec")
        return 1

    if not input_dir.is_dir():
        print(f"Input folder not found: {input_dir}")
        print("Create it and add audio files (.wav/.mp3/.flac), then rerun.")
        return 1

    input_files = [
        p for p in input_dir.iterdir() if p.is_file() and p.name != ".gitkeep"
    ]
    if not input_files:
        print(f"No files found in {input_dir}")
        print("Add audio files and rerun.")
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)
    model_output_dir = output_dir / model_name
    pending_files = []
    for input_file in input_files:
        stem_output_dir = model_output_dir / input_file.stem
        if stem_output_dir.is_dir() and has_stem_files(stem_output_dir):
            print(f"Skipping already-split track: {input_file.name}")
            continue
        pending_files.append(input_file)

    if pending_files:
        cmd = [
            sys.executable,
            "-m",
            "demucs",
            "-n",
            model_name,
            "--mp3",
            "-o",
            str(output_dir),
            *map(str, pending_files),
        ]
        exit_code = subprocess.call(cmd)
        if exit_code != 0:
            return exit_code
    else:
        print("All tracks already split. Nothing to do.")

    for input_file in input_files:
        write_songdetails(project_root, model_output_dir, input_file)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
