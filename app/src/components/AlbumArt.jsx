import React, { useMemo } from "react";

// Deterministic generated artwork + reveal grid.
// progress 0..1 controls how many cover tiles are removed.
const GRID = 6;

function hash(str) {
  let h = 2166136261;
  for (const c of str) {
    h ^= c.charCodeAt(0);
    h = Math.imul(h, 16777619);
  }
  return h >>> 0;
}

export default function AlbumArt({ id, progress = 0, label = true }) {
  const seed = useMemo(() => hash(id || "untitled"), [id]);

  const hue1 = seed % 360;
  const hue2 = (seed >> 8) % 360;
  const angle = (seed >> 16) % 360;

  // Stable pseudo-random tile reveal order.
  const order = useMemo(() => {
    const tiles = Array.from({ length: GRID * GRID }, (_, i) => i);
    let s = seed;
    for (let i = tiles.length - 1; i > 0; i--) {
      s = (Math.imul(s, 1103515245) + 12345) >>> 0;
      const j = s % (i + 1);
      [tiles[i], tiles[j]] = [tiles[j], tiles[i]];
    }
    return tiles;
  }, [seed]);

  const revealed = Math.round(progress * GRID * GRID);
  const revealedSet = new Set(order.slice(0, revealed));

  return (
    <div className="art">
      <div
        className="art-bg"
        style={{
          background: `linear-gradient(${angle}deg,
            hsl(${hue1} 45% 38%),
            hsl(${hue2} 55% 22%) 60%,
            hsl(${(hue1 + 40) % 360} 35% 12%))`,
        }}
      />
      {Array.from({ length: GRID * GRID }, (_, i) => {
        const x = i % GRID;
        const y = Math.floor(i / GRID);
        return (
          <div
            key={i}
            className="cover-tile"
            style={{
              left: `${(x / GRID) * 100}%`,
              top: `${(y / GRID) * 100}%`,
              width: `${100 / GRID + 0.5}%`,
              height: `${100 / GRID + 0.5}%`,
              opacity: revealedSet.has(i) ? 0 : 1,
            }}
          />
        );
      })}
      {label && (
        <span className="reveal-label">{Math.round(progress * 100)}% revealed</span>
      )}
    </div>
  );
}
