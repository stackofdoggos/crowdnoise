// Minimal standard MIDI file parser — extracts note events for the piano roll.

export function parseMidi(arrayBuffer) {
  const data = new DataView(arrayBuffer);
  let pos = 0;

  function readStr(n) {
    let s = "";
    for (let i = 0; i < n; i++) s += String.fromCharCode(data.getUint8(pos + i));
    pos += n;
    return s;
  }
  function readU32() { const v = data.getUint32(pos); pos += 4; return v; }
  function readU16() { const v = data.getUint16(pos); pos += 2; return v; }
  function readU8() { const v = data.getUint8(pos); pos += 1; return v; }
  function readVarLen() {
    let v = 0;
    for (;;) {
      const b = readU8();
      v = (v << 7) | (b & 0x7f);
      if (!(b & 0x80)) return v;
    }
  }

  if (readStr(4) !== "MThd") throw new Error("not a midi file");
  const headerLen = readU32();
  readU16(); // format
  const nTracks = readU16();
  const division = readU16();
  pos += headerLen - 6;

  const ticksPerBeat = division & 0x8000 ? 480 : division;
  let usPerBeat = 500000; // default 120bpm
  const tempos = [{ tick: 0, usPerBeat }];
  const notes = [];

  for (let t = 0; t < nTracks; t++) {
    if (readStr(4) !== "MTrk") break;
    const len = readU32();
    const end = pos + len;
    let tick = 0;
    let runningStatus = 0;
    const open = new Map(); // pitch -> {tick, velocity}

    while (pos < end) {
      tick += readVarLen();
      let status = readU8();
      if (status < 0x80) { pos -= 1; status = runningStatus; }
      else runningStatus = status;

      const type = status & 0xf0;
      if (type === 0x90 || type === 0x80) {
        const pitch = readU8();
        const vel = readU8();
        if (type === 0x90 && vel > 0) {
          open.set(pitch, { tick, vel });
        } else {
          const start = open.get(pitch);
          if (start) {
            notes.push({ pitch, startTick: start.tick, endTick: tick, velocity: start.vel });
            open.delete(pitch);
          }
        }
      } else if (type === 0xa0 || type === 0xb0 || type === 0xe0) {
        pos += 2;
      } else if (type === 0xc0 || type === 0xd0) {
        pos += 1;
      } else if (status === 0xff) {
        const metaType = readU8();
        const metaLen = readVarLen();
        if (metaType === 0x51 && metaLen === 3) {
          usPerBeat = (readU8() << 16) | (readU8() << 8) | readU8();
          tempos.push({ tick, usPerBeat });
        } else {
          pos += metaLen;
        }
      } else if (status === 0xf0 || status === 0xf7) {
        pos += readVarLen();
      }
    }
    pos = end;
  }

  // Convert ticks -> seconds (handle tempo changes).
  tempos.sort((a, b) => a.tick - b.tick);
  function tickToSec(tick) {
    let sec = 0;
    let lastTick = 0;
    let cur = 500000;
    for (const tm of tempos) {
      if (tm.tick >= tick) break;
      sec += ((tm.tick - lastTick) / ticksPerBeat) * (cur / 1e6);
      lastTick = tm.tick;
      cur = tm.usPerBeat;
    }
    sec += ((tick - lastTick) / ticksPerBeat) * (cur / 1e6);
    return sec;
  }

  const out = notes.map((n) => ({
    pitch: n.pitch,
    velocity: n.velocity,
    start: tickToSec(n.startTick),
    end: tickToSec(n.endTick),
  }));
  out.sort((a, b) => a.start - b.start);
  return out;
}
