# Audio Pipeline Technical Specification

u can just give a link or a yt video title if u run it in console by hitting play.

## Purpose

This document describes the current audio processing workflow for the MVP (piano + drums), how to run it, and what each script in `src/` is responsible for.

## End-to-End Workflow

The full workflow is orchestrated by `src/run_workflow.py`:

1. Download source audio into `resources/` using a song query.
2. Split stems with Demucs (`htdemucs_6s`) into `output/htdemucs_6s/<song>/`.
3. Run drum decomposition and piano-to-MIDI in parallel:
   - drum decomposition + one-shots + hit timestamps
   - piano stem to MIDI conversion
4. Generate hit visualization plots.

## How To Run `run_workflow.py`

### Interactive mode (prompts in terminal)

```bash
python3 src/run_workflow.py
```

Prompts:
- Song title/query
- Visualizer max seconds (default `15`)

### Non-interactive mode

```bash
python3 src/run_workflow.py "Piano And Drum" --max-seconds 15
```

## Output Structure

### Demucs stems

- `output/htdemucs_6s/<song>/bass.mp3`
- `output/htdemucs_6s/<song>/drums.mp3`
- `output/htdemucs_6s/<song>/other.mp3`
- `output/htdemucs_6s/<song>/vocals.mp3`
- `output/htdemucs_6s/<song>/guitar.mp3` (if produced)
- `output/htdemucs_6s/<song>/piano.mp3` (if produced)
- `output/htdemucs_6s/<song>/songdetails.json`

### Track decomposition (instrument-scoped)

- `output/trackDecomp/<song>/drum/kick.mp3`
- `output/trackDecomp/<song>/drum/hats.mp3`
- `output/trackDecomp/<song>/drum/kick_times.csv`
- `output/trackDecomp/<song>/drum/hats_times.csv`
- `output/trackDecomp/<song>/drum/kick_one_shot.mp3`
- `output/trackDecomp/<song>/drum/hats_one_shot.mp3`
- `output/trackDecomp/<song>/piano/piano.mid`

### Visualizations

- `output/plots/<song-slug>_hits.png`

## Script Responsibilities (`src/`)

### `src/run_workflow.py`
- Orchestrates the full pipeline.
- Supports interactive prompts or CLI args.
- Runs:
  - `scrape_audio.py`
  - `split_stems.py`
  - `isolate_drums.py` (with `--export-hits`)
  - `basic_pitch_to_midi.py`
  - `visualize_hit_times.py`
- Ensures outputs are written to the expected folders.

### `src/scrape_audio.py`
- Downloads audio from YouTube via `yt-dlp`.
- Falls back to Internet Archive when YouTube fetch fails.
- Writes MP3s to `resources/`.

### `src/split_stems.py`
- Splits all unsplit files in `resources/` using Demucs model `htdemucs_6s`.
- Skips `.gitkeep`.
- Skips files that already have stem outputs.
- Writes MP3 stems to `output/htdemucs_6s/<song>/`.
- Generates `songdetails.json` per song.

### `src/isolate_drums.py`
- Takes a drum/percussion input (typically Demucs `drums.mp3`).
- Produces kick/hats stems using filtering + optional gating.
- With `--export-hits`, also writes hit timestamps and one-shot samples.
- Writes into `output/trackDecomp/<song>/drum/`.

### `src/basic_pitch_to_midi.py`
- Converts audio to MIDI using Spotify Basic Pitch.
- Python requirement: `3.10+`.
- Default output is `output/trackDecomp/<song>/piano/piano.mid` when no `--out` is provided.

### `src/visualize_hit_times.py`
- Reads `kick_times.csv` and `hats_times.csv`.
- Overlays hit markers on reference/stem waveforms.
- Default window is first `15` seconds (or audio length if shorter).
- Can display interactively or save a PNG.

## Environment Notes

- Python: `3.10+` supported across current scripts.
- Common dependencies used in this pipeline include:
  - `demucs`
  - `torchcodec`
  - `basic-pitch`
  - `librosa`
  - `matplotlib`
  - `scipy`
  - `soundfile`
- `ffmpeg` is required for MP3 encoding/decoding paths.
