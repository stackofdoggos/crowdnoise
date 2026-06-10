import React, { useEffect, useState } from "react";
import { api } from "../api.js";
import { useNav, useJobs } from "../App.jsx";
import { GROUP, colorFor } from "../mock.js";
import AlbumArt from "../components/AlbumArt.jsx";

export default function Home() {
  const nav = useNav();
  const { version } = useJobs();
  const [projects, setProjects] = useState([]);

  useEffect(() => {
    api.listProjects().then(setProjects);
  }, [version]);

  return (
    <div>
      <h1>home</h1>
      <p className="sub">your groups and active remakes.</p>

      <div className="section">
        <h2>your group</h2>
        <div className="panel">
          <div className="row spread">
            <div className="row">
              <div style={{ display: "flex" }}>
                {GROUP.members.map((m, i) => (
                  <div
                    key={m.id}
                    className="avatar"
                    style={{ background: m.color || colorFor(m.name), marginLeft: i ? -8 : 0, border: "2px solid var(--panel)" }}
                    title={m.name}
                  >
                    {m.name[0]}
                  </div>
                ))}
              </div>
              <div className="col">
                <h3>{GROUP.name}</h3>
                <span className="faint">{GROUP.members.length} members · local mock</span>
              </div>
            </div>
            <button className="btn small" onClick={() => nav("community")}>view community</button>
          </div>
        </div>
      </div>

      <div className="section">
        <div className="row spread" style={{ marginBottom: 12 }}>
          <h2 style={{ margin: 0 }}>projects</h2>
          <button className="btn primary small" onClick={() => nav("new")}>+ new project</button>
        </div>

        {projects.length === 0 && (
          <div className="empty">
            no projects yet. scrape a song to start a remake.
          </div>
        )}

        <div className="grid2">
          {projects.map((p) => {
            const progress = progressFor(p);
            return (
              <div key={p.id} className="project-card" onClick={() => nav("project", { id: p.id })}>
                <div className="row" style={{ alignItems: "flex-start" }}>
                  <div style={{ width: 84, flexShrink: 0 }}>
                    <AlbumArt id={p.id} progress={progress} label={false} />
                  </div>
                  <div className="col" style={{ flex: 1, minWidth: 0 }}>
                    <h3 style={{ overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>{p.title}</h3>
                    <span className="faint">
                      {p.stemCount ? `${p.stemCount} stems` : "not split yet"}
                      {p.decomposed ? " · drums decomposed" : ""}
                      {p.replaced ? ` · ${p.replaced} replaced` : ""}
                    </span>
                    <div className="row" style={{ marginTop: 8, gap: 6, flexWrap: "wrap" }}>
                      {p.remake && <span className="tag good">remake ready</span>}
                      {!p.stemCount && p.hasSource && <span className="tag warn">needs split</span>}
                      <span className="tag">{Math.round(progress * 100)}% unlocked</span>
                    </div>
                  </div>
                </div>
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}

function progressFor(p) {
  let prog = 0;
  if (p.hasSource) prog += 0.1;
  if (p.stemCount) prog += 0.15;
  if (p.decomposed) prog += 0.15;
  if (p.instrumentCount) prog += 0.4 * Math.min(1, p.replaced / Math.max(1, 1));
  if (p.remake) prog += 0.2;
  return Math.min(1, prog);
}
