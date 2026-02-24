# Backend (v0) — sample ingest + MP3 canonicalization

This repo is currently mostly product/docs. This `backend/` folder is the first "bare bones" backend utility: **ingest a user recording (audio or video), preserve the original forever, and generate a standardized canonical MP3** for later pitch/time-stretching and instrument replacement.

## What gets stored (filesystem)

Given a `sample_id`, we write under a storage root (default `./storage`):

```
storage/
  samples/<sample_id>/
    original/
      original.<ext>          # exact bytes of the uploaded file (audio OR video)
    canonical/
      canonical.mp3           # standardized MP3 derived from the upload
    logs/
      ffmpeg.log              # ffmpeg output (useful when decode fails)
      probe/                  # optional ffprobe temp outputs
```

Notes:
- **No cleanup**: no trimming, no denoise, no loudness normalization.
- **Provenance stays in the database**: the MP3 is not tagged with provenance; the caller stores provenance records in its DB.
- If FFmpeg cannot decode/transcode, **we still keep the original upload** and return a nonzero exit code.

## Dependencies (WSL / Ubuntu)

- `g++` (already present in most dev setups)
- `ffmpeg` + `ffprobe`

Install FFmpeg (Ubuntu):

```bash
sudo apt-get update
sudo apt-get install -y ffmpeg
```

## Build

```bash
cd backend
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -o ingest_sample ingest_sample.cpp
```

## Run

Audio input:

```bash
./ingest_sample --input /path/to/recording.m4a
```

Video input (audio is extracted automatically; video is ignored for the canonical mp3):

```bash
./ingest_sample --input /path/to/video.mov
```

Custom storage root:

```bash
./ingest_sample --input /path/to/video.mov --storage-root ./storage
```

## Output contract

`ingest_sample` prints **JSON to stdout** with:
- `sample_id`
- where the original was saved (if possible)
- where the canonical MP3 lives (if created)
- status + ffmpeg exit code (on failure)
- selected standardization settings (codec, sample rate, channels)

Exit codes:
- `0`: canonical MP3 created successfully
- `2`: canonical MP3 failed, but original upload was preserved
- `3`: failed before even preserving original

