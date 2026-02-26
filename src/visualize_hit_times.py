#!/usr/bin/env python3
"""
Visualize detected kick/hats hit times against audio waveforms.

Example:
  python3 src/visualize_hit_times.py \
    --kick-times "output/trackDecomp/Kanye West - Homecoming/kick_times.csv" \
    --hats-times "output/trackDecomp/Kanye West - Homecoming/hats_times.csv" \
    --audio "output/htdemucs_6s/Kanye West - Homecoming/drums.mp3"
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def _load_times(csv_path: Path) -> list[float]:
    if not csv_path.exists():
        raise FileNotFoundError(f"CSV not found: {csv_path}")

    times: list[float] = []
    with csv_path.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if "time_seconds" not in (reader.fieldnames or []):
            raise RuntimeError(
                f"CSV missing 'time_seconds' column: {csv_path}\n"
                "Expected columns: index,time_seconds"
            )
        for row in reader:
            raw = (row.get("time_seconds") or "").strip()
            if not raw:
                continue
            times.append(float(raw))
    return times


def _load_wave(audio_path: Path) -> tuple[list[float], int]:
    if not audio_path.exists():
        raise FileNotFoundError(f"Audio file not found: {audio_path}")

    try:
        import librosa
    except Exception as e:  # noqa: BLE001
        raise RuntimeError(
            "librosa is required to load audio.\n"
            "Install with:\n"
            "  python3 -m pip install librosa\n"
        ) from e

    y, sr = librosa.load(str(audio_path), sr=None, mono=True)
    return y.tolist(), int(sr)


def _plot_wave_with_hits(
    ax,
    samples: list[float],
    sr: int,
    *,
    title: str,
    kick_times: list[float],
    hats_times: list[float],
    max_seconds: float,
) -> None:
    x = [i / sr for i in range(len(samples))]
    ax.plot(x, samples, linewidth=0.5, alpha=0.85)

    visible_kicks = [t for t in kick_times if t <= max_seconds]
    visible_hats = [t for t in hats_times if t <= max_seconds]

    for i, t in enumerate(visible_kicks):
        ax.axvline(
            x=t,
            color="tab:red",
            linewidth=1.0,
            alpha=0.7,
            label="kick hits" if i == 0 else None,
        )
    for i, t in enumerate(visible_hats):
        ax.axvline(
            x=t,
            color="tab:blue",
            linewidth=1.0,
            alpha=0.6,
            label="hats hits" if i == 0 else None,
        )

    ax.set_title(title)
    ax.set_ylabel("amp")
    ax.grid(alpha=0.2)

    handles, labels = ax.get_legend_handles_labels()
    if labels:
        ax.legend(loc="upper right")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Plot kick/hats hit timestamps on top of audio waveforms."
    )
    parser.add_argument("--kick-times", type=Path, required=True, help="Path to kick_times.csv")
    parser.add_argument("--hats-times", type=Path, required=True, help="Path to hats_times.csv")
    parser.add_argument(
        "--audio",
        type=Path,
        required=True,
        help="Reference audio path (usually Demucs drums stem).",
    )
    parser.add_argument(
        "--kick-audio",
        type=Path,
        default=None,
        help="Optional kick stem audio path. Defaults to sibling kick.mp3 if present.",
    )
    parser.add_argument(
        "--hats-audio",
        type=Path,
        default=None,
        help="Optional hats stem audio path. Defaults to sibling hats.mp3 if present.",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Optional path to save a PNG (e.g. output/plots/homecoming_hits.png).",
    )
    parser.add_argument(
        "--no-show",
        action="store_true",
        help="Do not open interactive plot window.",
    )
    parser.add_argument(
        "--max-seconds",
        type=float,
        default=15.0,
        help="Maximum time window to visualize, in seconds (default: 15).",
    )
    args = parser.parse_args(argv)

    try:
        import matplotlib.pyplot as plt
    except Exception as e:  # noqa: BLE001
        raise RuntimeError(
            "matplotlib is required for visualization.\n"
            "Install with:\n"
            "  python3 -m pip install matplotlib\n"
        ) from e

    kick_times = _load_times(args.kick_times.resolve())
    hats_times = _load_times(args.hats_times.resolve())

    # Default to sibling stems next to the CSV files (trackDecomp/<song>/kick.mp3/hats.mp3).
    kick_audio = args.kick_audio.resolve() if args.kick_audio else args.kick_times.resolve().parent / "kick.mp3"
    hats_audio = args.hats_audio.resolve() if args.hats_audio else args.hats_times.resolve().parent / "hats.mp3"

    panels: list[tuple[str, Path]] = [("Reference audio (drums)", args.audio.resolve())]
    if kick_audio.exists():
        panels.append(("Kick stem", kick_audio))
    if hats_audio.exists():
        panels.append(("Hats stem", hats_audio))

    fig, axes = plt.subplots(
        nrows=len(panels),
        ncols=1,
        figsize=(16, 3.5 * len(panels)),
        sharex=False,
    )
    if len(panels) == 1:
        axes = [axes]

    for ax, (title, audio_path) in zip(axes, panels):
        samples, sr = _load_wave(audio_path)
        audio_seconds = len(samples) / float(sr) if sr else 0.0
        max_seconds = min(float(args.max_seconds), audio_seconds)
        _plot_wave_with_hits(
            ax,
            samples,
            sr,
            title=f"{title}: {audio_path.name}",
            kick_times=kick_times,
            hats_times=hats_times,
            max_seconds=max_seconds,
        )
        ax.set_xlim(left=0.0, right=max_seconds)

    axes[-1].set_xlabel("time (seconds)")
    fig.tight_layout()

    if args.out is not None:
        out_path = args.out.resolve()
        out_path.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(str(out_path), dpi=150)
        print(f"Saved plot: {out_path}")

    if not args.no_show:
        plt.show()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
