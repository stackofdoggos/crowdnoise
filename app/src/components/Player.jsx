import React, { useEffect, useRef, useState } from "react";
import { mediaUrl, fmtTime } from "../api.js";

// Only one player audible at a time keeps the lab usable.
let activeAudio = null;

export default function Player({ src, label, volume = 1, compact = false, refreshKey = 0 }) {
  const audioRef = useRef(null);
  const [playing, setPlaying] = useState(false);
  const [progress, setProgress] = useState(0);
  const [duration, setDuration] = useState(0);

  useEffect(() => {
    const audio = new Audio();
    audio.preload = "metadata";
    audioRef.current = audio;
    audio.addEventListener("timeupdate", () => {
      setProgress(audio.currentTime);
    });
    audio.addEventListener("loadedmetadata", () => setDuration(audio.duration));
    audio.addEventListener("ended", () => setPlaying(false));
    audio.addEventListener("pause", () => setPlaying(false));
    audio.addEventListener("play", () => setPlaying(true));
    return () => {
      audio.pause();
      audio.src = "";
      if (activeAudio === audio) activeAudio = null;
    };
  }, []);

  useEffect(() => {
    const audio = audioRef.current;
    if (!audio || !src) return;
    audio.src = `${mediaUrl(src)}?v=${refreshKey}`;
    setProgress(0);
    setPlaying(false);
  }, [src, refreshKey]);

  useEffect(() => {
    if (audioRef.current) audioRef.current.volume = volume;
  }, [volume]);

  function toggle() {
    const audio = audioRef.current;
    if (!audio || !src) return;
    if (audio.paused) {
      if (activeAudio && activeAudio !== audio) activeAudio.pause();
      activeAudio = audio;
      audio.play().catch(() => {});
    } else {
      audio.pause();
    }
  }

  function seek(e) {
    const audio = audioRef.current;
    if (!audio || !duration) return;
    const rect = e.currentTarget.getBoundingClientRect();
    const frac = Math.min(1, Math.max(0, (e.clientX - rect.left) / rect.width));
    audio.currentTime = frac * duration;
  }

  if (!src) return <span className="faint">{label || "no audio"}</span>;

  return (
    <div className="player">
      <button className={`play-btn ${playing ? "playing" : ""}`} onClick={toggle} title={label}>
        {playing ? "❚❚" : "▶"}
      </button>
      {!compact && (
        <>
          <div className="seek" onClick={seek}>
            <div className="fill" style={{ width: `${duration ? (progress / duration) * 100 : 0}%` }} />
          </div>
          <span className="time">{fmtTime(progress)} / {fmtTime(duration)}</span>
        </>
      )}
    </div>
  );
}
