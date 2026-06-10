import React, { useEffect, useState } from "react";
import { api } from "../api.js";
import { useNav } from "../App.jsx";
import Player from "../components/Player.jsx";
import { rarityForSample } from "../mock.js";

// The most important screen: rewind any sound to where it came from.
export default function Provenance({ id }) {
  const nav = useNav();
  const [proj, setProj] = useState(null);

  useEffect(() => {
    api.getProject(id).then(setProj);
  }, [id]);

  if (!proj) return <div className="empty">loading…</div>;

  const samples = Object.values(proj.samples || {});
  const history = proj.state?.history || [];

  return (
    <div>
      <button className="back-link" onClick={() => nav("project", { id })}>← {proj.title}</button>
      <h1>provenance</h1>
      <p className="sub">
        every sound in the remake can be rewound to its original clip. nothing is hidden —
        that's the point.
      </p>

      {samples.length === 0 && (
        <div className="empty">no samples yet. record something in the drum lab first.</div>
      )}

      {samples.map((s) => (
        <div className="section" key={s.sampleId}>
          <div className="row spread" style={{ marginBottom: 10 }}>
            <h2 style={{ margin: 0 }}>{s.role}</h2>
            <span className={`tag ${rarityForSample(s) !== "common" ? "user" : ""}`}>
              {rarityForSample(s)} · chain of {chainLength(s)}
            </span>
          </div>

          <div className="chain">
            {s.placedPath && (
              <>
                <ChainNode
                  kind="step 4 — placed in the song"
                  title={`repeated at every detected ${s.role} hit, volume-matched to the original`}
                  src={s.placedPath}
                  detail={s.gain != null ? `gain ×${Number(s.gain).toFixed(2)} via match_sample_gain` : null}
                />
                <div className="chain-link" />
              </>
            )}
            {s.gain != null && !s.placedPath && null}
            <ChainNode
              kind="step 3 — canonical mp3"
              title="standardized for editing (original preserved untouched)"
              src={s.canonicalPath}
              detail={`ingested ${new Date(s.createdAt).toLocaleString()}`}
            />
            <div className="chain-link" />
            <ChainNode
              kind={`step 2 — ${s.source === "recorded" ? "mic recording" : "uploaded file"}`}
              title={s.originalName || "original clip"}
              src={s.originalPath}
              detail="the exact bytes that were captured — never modified"
            />
            <div className="chain-link" />
            <ChainNode
              kind="step 1 — the source"
              title={s.source === "recorded" ? "recorded live in crowd·noise by you" : "brought in from your library"}
              src={null}
              detail={`sample id: ${s.sampleId}`}
            />
          </div>
        </div>
      ))}

      {history.length > 0 && (
        <div className="section">
          <h2>replacement history</h2>
          <div className="panel">
            {history.slice().reverse().map((h, i) => (
              <div className="list-row" key={i}>
                <span className="tag user">{h.instrument}</span>
                <div className="col" style={{ flex: 1 }}>
                  <span style={{ fontSize: 13 }}>
                    {basename(h.old_active_path)} → {basename(h.new_active_path)}
                  </span>
                  <span className="faint">{new Date(h.ts).toLocaleString()} · by {h.user_id}</span>
                </div>
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}

function ChainNode({ kind, title, src, detail }) {
  return (
    <div className="chain-node">
      <div className="kind">{kind}</div>
      <div className="row spread">
        <div className="col" style={{ flex: 1, minWidth: 0 }}>
          <h3>{title}</h3>
          {detail && <span className="faint">{detail}</span>}
        </div>
        {src && <div style={{ width: 240 }}><Player src={src} /></div>}
      </div>
    </div>
  );
}

function chainLength(s) {
  let n = 2;
  if (s.gain != null) n += 1;
  if (s.placedPath) n += 1;
  return n;
}

function basename(p) {
  return p ? p.split("/").pop() : "?";
}
