import React, { useEffect, useRef, useState } from "react";
import { api } from "../api.js";
import { parseMidi } from "../midi.js";

export default function PianoRoll({ midiPath }) {
  const ref = useRef(null);
  const [notes, setNotes] = useState(null);
  const [error, setError] = useState(null);

  useEffect(() => {
    if (!midiPath) return;
    let alive = true;
    api.readBuffer(midiPath)
      .then((buf) => {
        if (!alive) return;
        setNotes(parseMidi(buf));
      })
      .catch((e) => alive && setError(String(e)));
    return () => { alive = false; };
  }, [midiPath]);

  useEffect(() => {
    const canvas = ref.current;
    if (!canvas || !notes || !notes.length) return;
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth;
    const h = canvas.clientHeight;
    canvas.width = w * dpr;
    canvas.height = h * dpr;
    const ctx = canvas.getContext("2d");
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, w, h);

    const maxT = Math.max(...notes.map((n) => n.end));
    const pitches = notes.map((n) => n.pitch);
    const lo = Math.min(...pitches) - 2;
    const hi = Math.max(...pitches) + 2;

    for (const n of notes) {
      const x = (n.start / maxT) * w;
      const nw = Math.max(2, ((n.end - n.start) / maxT) * w);
      const y = h - ((n.pitch - lo) / (hi - lo)) * h;
      const alpha = 0.35 + 0.65 * (n.velocity / 127);
      ctx.fillStyle = `rgba(155, 200, 231, ${alpha})`;
      ctx.fillRect(x, y - 2, nw, 3.5);
    }
  }, [notes]);

  if (!midiPath) return null;
  if (error) return <div className="faint">couldn't parse midi: {error}</div>;

  return (
    <div className="col" style={{ gap: 8 }}>
      <canvas ref={ref} style={{ width: "100%", height: 120, display: "block", borderRadius: 8, background: "#050507", border: "1px solid var(--border)" }} />
      <span className="faint">{notes ? `${notes.length} notes detected by basic pitch` : "parsing midi…"}</span>
    </div>
  );
}
