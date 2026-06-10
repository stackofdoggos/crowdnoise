import React, { useEffect, useRef } from "react";

// Draws hit times as ticks on a timeline strip.
export default function HitTimeline({ times = [], duration = 0, color = "#9be7a2" }) {
  const ref = useRef(null);

  useEffect(() => {
    const canvas = ref.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth;
    const h = canvas.clientHeight;
    canvas.width = w * dpr;
    canvas.height = h * dpr;
    const ctx = canvas.getContext("2d");
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, w, h);

    const total = duration || (times.length ? Math.max(...times) + 1 : 1);

    // baseline
    ctx.strokeStyle = "#2a2a33";
    ctx.beginPath();
    ctx.moveTo(0, h - 8);
    ctx.lineTo(w, h - 8);
    ctx.stroke();

    ctx.fillStyle = color;
    for (const t of times) {
      const x = (t / total) * (w - 4) + 2;
      ctx.fillRect(x - 1, 8, 2, h - 18);
    }
  }, [times, duration, color]);

  return <canvas ref={ref} className="hit-canvas" />;
}
