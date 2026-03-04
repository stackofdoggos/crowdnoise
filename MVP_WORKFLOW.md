# CrowdNoise MVP Workflow

**Source:** [YouTube video](https://www.youtube.com/watch?v=dXDsd4Yn_qs)
FOLLOW ALL THESE COMMANDS FOR THE MVP!!! THIS IS SPLITTING KANYE WEST'S I WONDER

---

## Commands

```bash
# 1. Download audio → resources/ and MVP demo/
python3 src/scrape_audio.py "https://www.youtube.com/watch?v=dXDsd4Yn_qs" -o combined_portable.mp3 --mvp-demo

# 2. Split stems → MVP demo/original_*.mp3
python3 src/split_stems.py --mvp-demo combined_portable

# 3. Isolate kick + hats from drums → MVP demo/drum/
python3 src/isolate_drums.py output/htdemucs_6s/combined_portable/drums.mp3 --export-hits --out-dir output/trackDecomp --mvp-demo
```

1. **Download** — Fetches audio from YouTube into `resources/` and `MVP demo/`.
2. **Split stems** — Demucs separates drums, bass, other, etc.; copies stems to `MVP demo/original_*.mp3`.
3. **Isolate drums** — Extracts kick/hats, hit timestamps (`kick_times.csv`, `hats_times.csv`), one-shot samples; copies to `MVP demo/drum/`.

---

## 4. Record your samples

Record two sounds to replace kick and hats:

- **Kick** — a low punch (table thump, body hit, etc.)
- **Hats** — a bright hit (finger snap, keys, pen tap, etc.)

**Reference one-shots** (in `MVP demo/drum/`): `kick_one_shot.mp3`, `hats_one_shot.mp3` — use these as examples for length and tone.

**Record with:**

- Phone voice memo or computer mic
- Or use `ingest_sample` to standardize: `./ingest_sample --input recording.m4a --storage-root ./storage --sample-id my_kick` → canonical MP3 in `storage/samples/my_kick/canonical/`

Save as `my_kick.mp3` and `my_hats.mp3` in `MVP demo/`.

---

## 5. Place samples over the decomp tracks

Build the repeat tool, then put your samples at the hit times. Volume is auto-matched to the original kick/hats:

```bash
make
DURATION=$(ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 "MVP demo/combined_portable.mp3")

# Match gain to reference one-shot, then place kick
KICK_GAIN=$(python3 src/match_sample_gain.py --reference "MVP demo/drum/kick_one_shot.mp3" --sample "MVP demo/my_kick.mp3" --times "MVP demo/drum/kick_times.csv")
./bin/repeat_sample_at_times_cli \
  --times "MVP demo/drum/kick_times.csv" \
  --sample "MVP demo/my_kick.mp3" \
  --out "MVP demo/kick_track.mp3" \
  --length-seconds "$DURATION" \
  --gain "$KICK_GAIN"

# Match gain to reference one-shot, then place hats
HATS_GAIN=$(python3 src/match_sample_gain.py --reference "MVP demo/drum/hats_one_shot.mp3" --sample "MVP demo/my_hats.mp3" --times "MVP demo/drum/hats_times.csv")
./bin/repeat_sample_at_times_cli \
  --times "MVP demo/drum/hats_times.csv" \
  --sample "MVP demo/my_hats.mp3" \
  --out "MVP demo/hats_track.mp3" \
  --length-seconds "$DURATION" \
  --gain "$HATS_GAIN"

# Mix kick + hats → remade_drums.mp3
ffmpeg -y -hide_banner -loglevel error \
  -i "MVP demo/kick_track.mp3" \
  -i "MVP demo/hats_track.mp3" \
  -filter_complex "amix=inputs=2:duration=longest,volume=3.5" \
  -c:a libmp3lame -q:a 2 "MVP demo/remade_drums.mp3"
```

---

## 6. Build the final mix

```bash
./pipeline_test "MVP demo"
```

Writes `Song_Details.json`, `Song_State.json`, and `MVP demo/combined_from_state.mp3`.
