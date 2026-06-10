# crowd·noise desktop

electron + react desktop app that drives the full crowd·noise pipeline end-to-end, with every step (including song scraping) visible in the UI.

## run it

```bash
cd app
npm install
npm run dev
```

requirements (already set up in this repo):
- `python3` (3.10+) with `demucs`, `librosa`, `scipy`, `soundfile`, `basic-pitch`, `yt-dlp`
- `ffmpeg` / `ffprobe` on PATH
- native CLIs built: `bin/repeat_sample_at_times_cli` (`make`) and `src/ingest_sample`

use a different python with `CROWDNOISE_PYTHON=/path/to/python3 npm run dev`.

## the flow (all testable from the UI)

1. **new project** — type a song name or youtube url. the app runs, with live logs:
   - `src/scrape_audio.py` (yt-dlp, internet archive fallback)
   - `src/split_stems.py` (demucs `htdemucs_6s` → 6 stems)
   - `src/decompose_drums.py` (drums → kick / snare / hats + hit times + one-shots)
   - `src/basic_pitch_to_midi.py` (piano stem → midi, when present)
2. **project** — play every stem, see pipeline status, piano roll of the midi,
   album art that unlocks with progress.
3. **drum lab** — per drum part: hit timeline, isolated stem, reference one-shot.
   record with the mic (full-screen one-button screen) or upload a file. samples are
   ingested via `src/ingest_sample` (original preserved + canonical mp3), volume-matched
   with `src/match_sample_gain.py`, and placed at every hit with
   `bin/repeat_sample_at_times_cli`. then build "remade drums", which swaps into the song.
4. **the remake** — final mix of all active tracks (ffmpeg), original-vs-remake compare.
5. **provenance** — rewind any sound: placed track → canonical mp3 → exact original
   recording bytes → source. plus full replacement history from `Song_State.json`.
6. **community** — leaderboard, voting, difficulty board, friend discovery (local mock),
   and sample cards minted from your real recorded samples with rarity tiers.

## where things land

- scraped audio: `resources/<song>.mp3`
- stems: `output/htdemucs_6s/<song>/`
- drum decomposition: `output/trackDecomp/<song>/drum/`
- per-project app data (samples, placed tracks, `Song_State.json`, `remake.mp3`): `projects/<song>/`

## drum decomposition fidelity

`src/decompose_drums.py` replaces the old band-pass-only `isolate_drums.py` approach:
hpss strips tonal bleed, onsets are detected per frequency band, and every hit is
validated with full-spectrum features using thresholds adaptive to the song (so dark,
lo-fi hats still classify). layered hits (kick + hat on the same beat) land in both
classes. stems are rebuilt with time-frequency masks that only open around each class's
classified hits, which removes inter-class bleed. it also adds **snare** as a third part.
csv outputs stay compatible with the existing native placement tools.
