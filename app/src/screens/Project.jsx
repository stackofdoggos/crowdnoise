import React, { useEffect, useMemo, useState } from "react";
import { api } from "../api.js";
import { useNav, useJobs } from "../App.jsx";
import Player from "../components/Player.jsx";
import AlbumArt from "../components/AlbumArt.jsx";
import PianoRoll from "../components/PianoRoll.jsx";
import JobConsole from "../components/JobConsole.jsx";

export default function Project({ id }) {
  const nav = useNav();
  const { jobs, version } = useJobs();
  const [proj, setProj] = useState(null);
  const [volumes, setVolumes] = useState({});
  const [activeJobId, setActiveJobId] = useState(null);
  const [mixKey, setMixKey] = useState(0);

  useEffect(() => {
    api.getProject(id).then(setProj);
  }, [id, version]);

  useEffect(() => {
    const off = api.onMixDone((p) => {
      if (p.projectId === id) setMixKey((k) => k + 1);
    });
    return off;
  }, [id]);

  const activeJob = activeJobId ? jobs[activeJobId] : null;

  const progress = useMemo(() => {
    if (!proj) return 0;
    let prog = 0;
    if (proj.steps.scraped) prog += 0.1;
    if (proj.steps.split) prog += 0.15;
    if (proj.steps.decomposed) prog += 0.15;
    const replaced = proj.state
      ? Object.values(proj.state.instruments).filter((i) => i.active_source === "user").length
      : 0;
    if (replaced > 0) prog += 0.3;
    if (proj.remakePath) prog += 0.3;
    return Math.min(1, prog);
  }, [proj]);

  if (!proj) return <div className="empty">loading…</div>;

  async function runStep(step) {
    const { jobId } = await api.runStep(id, step);
    setActiveJobId(jobId);
  }

  async function buildMix() {
    const { jobId } = await api.buildFinalMix(id);
    setActiveJobId(jobId);
  }

  async function restore(instrument) {
    await api.restoreTrack(id, instrument);
    setProj(await api.getProject(id));
  }

  const instruments = proj.state ? Object.entries(proj.state.instruments) : [];
  const replacedCount = instruments.filter(([, i]) => i.active_source === "user").length;

  return (
    <div>
      <button className="back-link" onClick={() => nav("home")}>← home</button>

      <div className="row" style={{ alignItems: "flex-start", gap: 24 }}>
        <div style={{ width: 200, flexShrink: 0 }}>
          <AlbumArt id={id} progress={progress} />
        </div>
        <div className="col" style={{ flex: 1, gap: 8 }}>
          <h1>{proj.title}</h1>
          <p className="sub">
            replace instruments with sounds you record. the more you remake, the more the cover unlocks.
          </p>
          <div className="steps" style={{ marginTop: 6 }}>
            <StepChip label="scraped" ok={proj.steps.scraped} />
            <StepChip label="stems split" ok={proj.steps.split} />
            <StepChip label="drums decomposed" ok={proj.steps.decomposed} />
            <StepChip label={proj.piano.midiPath ? "piano → midi" : "no piano midi"} ok={proj.steps.midi} />
          </div>
          {proj.sourcePath && (
            <div className="row" style={{ marginTop: 10 }}>
              <span className="faint" style={{ width: 90 }}>original song</span>
              <Player src={proj.sourcePath} label="original" />
            </div>
          )}
          <div className="row" style={{ marginTop: 4, gap: 8 }}>
            {!proj.steps.split && proj.steps.scraped && (
              <button className="btn small" onClick={() => runStep("split")}>split stems</button>
            )}
            {proj.steps.split && !proj.steps.decomposed && (
              <button className="btn small" onClick={() => runStep("decompose")}>decompose drums</button>
            )}
            {proj.steps.split && !proj.steps.midi && proj.stems.some((s) => s.name === "piano") && (
              <button className="btn small" onClick={() => runStep("midi")}>piano → midi</button>
            )}
            <button className="btn small" onClick={() => nav("provenance", { id })}>provenance</button>
          </div>
        </div>
      </div>

      {activeJob && (
        <div className="section">
          <div className="row" style={{ marginBottom: 8 }}>
            <span className={`dot-status ${activeJob.status === "done" ? "done" : activeJob.status === "error" ? "error" : "running"}`} />
            <h3>{activeJob.type}</h3>
          </div>
          <JobConsole lines={activeJob.lines} />
        </div>
      )}

      {instruments.length > 0 && (
        <div className="section">
          <div className="row spread" style={{ marginBottom: 12 }}>
            <h2 style={{ margin: 0 }}>tracks</h2>
            <span className="faint">{replacedCount} of {instruments.length} replaced</span>
          </div>
          {instruments.map(([name, inst]) => (
            <div key={name} className={`track-row ${inst.active_source === "user" ? "replaced" : ""}`}>
              <span className="name">{name}</span>
              <Player
                src={inst.active_path}
                volume={volumes[name] ?? 1}
                refreshKey={mixKey}
              />
              <input
                type="range"
                min="0" max="1" step="0.01"
                value={volumes[name] ?? 1}
                onChange={(e) => setVolumes((v) => ({ ...v, [name]: parseFloat(e.target.value) }))}
                title="preview volume"
              />
              {inst.active_source === "user" ? (
                <>
                  <span className="tag user">yours</span>
                  <button className="btn small ghost" onClick={() => restore(name)}>restore</button>
                </>
              ) : name === "drums" && proj.steps.decomposed ? (
                <button className="btn small" onClick={() => nav("drumlab", { id })}>replace →</button>
              ) : (
                <span className="tag">original</span>
              )}
            </div>
          ))}
        </div>
      )}

      {proj.piano.midiPath && (
        <div className="section">
          <h2>piano decomposition</h2>
          <div className="panel">
            <PianoRoll midiPath={proj.piano.midiPath} />
          </div>
        </div>
      )}

      {instruments.length > 0 && (
        <div className="section">
          <h2>the remake</h2>
          <div className="panel">
            <div className="row spread">
              <div className="col">
                <h3>final mix</h3>
                <span className="faint">
                  combines every active track ({replacedCount ? `${replacedCount} yours, ${instruments.length - replacedCount} original` : "all original so far"})
                </span>
              </div>
              <button className="btn primary" onClick={buildMix}>
                {proj.remakePath ? "rebuild remake" : "build remake"}
              </button>
            </div>
            {proj.remakePath && (
              <>
                <div className="divider" />
                <div className="grid2">
                  <div className="col" style={{ gap: 8 }}>
                    <span className="faint">original</span>
                    <Player src={proj.sourcePath} refreshKey={mixKey} />
                  </div>
                  <div className="col" style={{ gap: 8 }}>
                    <span className="faint">your remake</span>
                    <Player src={proj.remakePath} refreshKey={mixKey} />
                  </div>
                </div>
                <div className="row" style={{ marginTop: 12 }}>
                  <button className="btn small ghost" onClick={() => api.revealInFinder(proj.remakePath)}>
                    reveal remake.mp3 in finder
                  </button>
                </div>
              </>
            )}
          </div>
        </div>
      )}
    </div>
  );
}

function StepChip({ label, ok }) {
  return (
    <div className="step-chip">
      <span className={`dot-status ${ok ? "done" : ""}`} />
      {label}
    </div>
  );
}
