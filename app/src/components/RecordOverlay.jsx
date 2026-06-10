import React, { useEffect, useRef, useState } from "react";

// Full-screen recording: one button, no clutter.
export default function RecordOverlay({ role, hint, onDone, onCancel }) {
  const [recording, setRecording] = useState(false);
  const [saving, setSaving] = useState(false);
  const [levels, setLevels] = useState(Array(24).fill(2));
  const [error, setError] = useState(null);
  const mediaRef = useRef({});

  useEffect(() => {
    let alive = true;
    (async () => {
      try {
        const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
        if (!alive) { stream.getTracks().forEach((t) => t.stop()); return; }
        const ctx = new AudioContext();
        const source = ctx.createMediaStreamSource(stream);
        const analyser = ctx.createAnalyser();
        analyser.fftSize = 256;
        source.connect(analyser);
        mediaRef.current = { stream, ctx, analyser };

        const buf = new Uint8Array(analyser.frequencyBinCount);
        const tick = () => {
          if (!alive) return;
          analyser.getByteFrequencyData(buf);
          const step = Math.floor(buf.length / 24);
          setLevels(Array.from({ length: 24 }, (_, i) => Math.max(2, (buf[i * step] / 255) * 40)));
          requestAnimationFrame(tick);
        };
        tick();
      } catch (e) {
        setError("microphone unavailable: " + e.message);
      }
    })();
    return () => {
      alive = false;
      const { stream, ctx } = mediaRef.current;
      stream?.getTracks().forEach((t) => t.stop());
      ctx?.close();
    };
  }, []);

  function toggle() {
    const { stream } = mediaRef.current;
    if (!stream) return;
    if (!recording) {
      const rec = new MediaRecorder(stream, { mimeType: "audio/webm;codecs=opus" });
      const chunks = [];
      rec.ondataavailable = (e) => chunks.push(e.data);
      rec.onstop = async () => {
        setSaving(true);
        const blob = new Blob(chunks, { type: "audio/webm" });
        const buf = await blob.arrayBuffer();
        onDone(buf);
      };
      rec.start();
      mediaRef.current.rec = rec;
      setRecording(true);
    } else {
      mediaRef.current.rec?.stop();
      setRecording(false);
    }
  }

  return (
    <div className="record-overlay">
      <div className="col" style={{ alignItems: "center", gap: 8 }}>
        <h2 style={{ margin: 0 }}>record your {role}</h2>
        <span className="sub">{hint}</span>
      </div>

      <div className="record-meter">
        {levels.map((h, i) => (
          <div key={i} className="bar" style={{ height: h }} />
        ))}
      </div>

      {error ? (
        <span className="sub" style={{ color: "var(--danger)" }}>{error}</span>
      ) : saving ? (
        <span className="sub">saving + canonicalizing…</span>
      ) : (
        <button className={`record-btn ${recording ? "recording" : ""}`} onClick={toggle}>
          <div className="inner" />
        </button>
      )}

      <span className="faint">{recording ? "tap to stop" : "tap to record · keep it 0.1–10s"}</span>
      <button className="btn ghost" onClick={onCancel} disabled={saving}>cancel</button>
    </div>
  );
}
