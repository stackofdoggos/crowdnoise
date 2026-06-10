import React, { useEffect, useState } from "react";
import { api } from "../api.js";
import { useNav, useJobs } from "../App.jsx";
import Player from "../components/Player.jsx";
import HitTimeline from "../components/HitTimeline.jsx";
import JobConsole from "../components/JobConsole.jsx";
import RecordOverlay from "../components/RecordOverlay.jsx";
import { ASSIGNMENTS } from "../mock.js";

const ROLE_COLORS = { kick: "#9be7a2", snare: "#e7b89b", hats: "#9bc8e7" };

export default function DrumLab({ id }) {
  const nav = useNav();
  const { jobs, version } = useJobs();
  const [proj, setProj] = useState(null);
  const [recordingRole, setRecordingRole] = useState(null);
  const [busyRole, setBusyRole] = useState(null);
  const [activeJobId, setActiveJobId] = useState(null);
  const [refreshKey, setRefreshKey] = useState(0);

  const reload = () => api.getProject(id).then(setProj);

  useEffect(() => { reload(); }, [id, version]);

  useEffect(() => {
    const offPlace = api.onPlaceDone((p) => {
      if (p.projectId === id) { setBusyRole(null); setRefreshKey((k) => k + 1); reload(); }
    });
    const offMix = api.onMixDone((p) => {
      if (p.projectId === id) { setRefreshKey((k) => k + 1); reload(); }
    });
    return () => { offPlace(); offMix(); };
  }, [id]);

  if (!proj) return <div className="empty">loading…</div>;

  const roles = Object.keys(proj.drum.classes);
  const activeJob = activeJobId ? jobs[activeJobId] : null;
  const placedCount = roles.filter((r) => proj.samples[r]?.placedPath).length;

  async function saveRecording(role, buf) {
    setRecordingRole(null);
    setBusyRole(role);
    try {
      await api.saveRecording(id, role, buf, "webm");
    } finally {
      setBusyRole(null);
      reload();
    }
  }

  async function importSample(role) {
    setBusyRole(role);
    try {
      await api.importSample(id, role);
    } finally {
      setBusyRole(null);
      reload();
    }
  }

  async function place(role) {
    setBusyRole(role);
    const { jobId } = await api.placeSample(id, role);
    setActiveJobId(jobId);
  }

  async function buildDrums() {
    const { jobId } = await api.buildRemadeDrums(id);
    setActiveJobId(jobId);
  }

  return (
    <div>
      <button className="back-link" onClick={() => nav("project", { id })}>← {proj.title}</button>
      <h1>drum lab</h1>
      <p className="sub">
        the drum stem was decomposed into {roles.join(" / ")} with detected hit times.
        record or upload one sound per part — we'll volume-match it to the original
        and place it at every hit.
      </p>

      {roles.length === 0 && (
        <div className="empty">no drum decomposition yet. run "decompose drums" from the project page.</div>
      )}

      {roles.map((role) => {
        const cls = proj.drum.classes[role];
        const sample = proj.samples[role];
        const assignment = ASSIGNMENTS.find((a) => a.role === role);
        return (
          <div className="section" key={role}>
            <div className="row spread" style={{ marginBottom: 10 }}>
              <div className="row">
                <h2 style={{ margin: 0, color: ROLE_COLORS[role] }}>{role}</h2>
                <span className="faint">{cls.count} hits detected</span>
              </div>
              {assignment && (
                <span className="tag">{assignment.assignee === "you" ? "assigned to you" : `assigned to ${assignment.assignee}`}</span>
              )}
            </div>

            <div className="panel">
              <HitTimeline times={cls.times} duration={proj.duration} color={ROLE_COLORS[role]} />

              <div className="grid2" style={{ marginTop: 14 }}>
                <div className="col" style={{ gap: 8 }}>
                  <span className="faint">original — isolated stem</span>
                  <Player src={cls.stem} refreshKey={refreshKey} />
                  {cls.oneShot && (
                    <>
                      <span className="faint" style={{ marginTop: 6 }}>original — single hit (your reference)</span>
                      <Player src={cls.oneShot} refreshKey={refreshKey} />
                    </>
                  )}
                </div>

                <div className="col" style={{ gap: 8 }}>
                  <span className="faint">your sound{assignment ? ` — try: ${assignment.hint}` : ""}</span>
                  {sample ? (
                    <>
                      <Player src={sample.canonicalPath} refreshKey={refreshKey} />
                      <div className="row" style={{ gap: 6, flexWrap: "wrap" }}>
                        <span className="tag user">{sample.source}</span>
                        {sample.gain != null && <span className="tag">gain ×{Number(sample.gain).toFixed(2)}</span>}
                        {sample.placedPath && <span className="tag good">placed</span>}
                      </div>
                    </>
                  ) : (
                    <span className="faint">nothing yet</span>
                  )}
                  <div className="row" style={{ gap: 8, marginTop: 4 }}>
                    <button className="btn small" onClick={() => setRecordingRole(role)} disabled={busyRole === role}>
                      ● record
                    </button>
                    <button className="btn small" onClick={() => importSample(role)} disabled={busyRole === role}>
                      upload file
                    </button>
                    {sample && (
                      <button className="btn small primary" onClick={() => place(role)} disabled={busyRole === role}>
                        {sample.placedPath ? "re-place at hits" : "place at hits"}
                      </button>
                    )}
                  </div>
                </div>
              </div>

              {sample?.placedPath && (
                <>
                  <div className="divider" />
                  <div className="col" style={{ gap: 8 }}>
                    <span className="faint">your {role}, placed at all {cls.count} hit times</span>
                    <Player src={sample.placedPath} refreshKey={refreshKey} />
                  </div>
                </>
              )}
            </div>
          </div>
        );
      })}

      {activeJob && (
        <div className="section">
          <div className="row" style={{ marginBottom: 8 }}>
            <span className={`dot-status ${activeJob.status === "done" ? "done" : activeJob.status === "error" ? "error" : "running"}`} />
            <h3>{activeJob.type}</h3>
          </div>
          <JobConsole lines={activeJob.lines} />
        </div>
      )}

      {roles.length > 0 && (
        <div className="section">
          <div className="panel">
            <div className="row spread">
              <div className="col">
                <h3>remade drums</h3>
                <span className="faint">
                  {placedCount ? `mixes your ${placedCount} placed part${placedCount > 1 ? "s" : ""} and swaps them into the song` : "place at least one part first"}
                </span>
              </div>
              <button className="btn primary" onClick={buildDrums} disabled={!placedCount}>
                {proj.remadeDrumsPath ? "rebuild remade drums" : "build remade drums"}
              </button>
            </div>
            {proj.remadeDrumsPath && (
              <>
                <div className="divider" />
                <div className="grid2">
                  <div className="col" style={{ gap: 8 }}>
                    <span className="faint">original drums</span>
                    <Player src={proj.stems.find((s) => s.name === "drums")?.path} refreshKey={refreshKey} />
                  </div>
                  <div className="col" style={{ gap: 8 }}>
                    <span className="faint">your drums</span>
                    <Player src={proj.remadeDrumsPath} refreshKey={refreshKey} />
                  </div>
                </div>
                <div className="row" style={{ marginTop: 12 }}>
                  <button className="btn small" onClick={() => nav("project", { id })}>
                    back to project → build the remake
                  </button>
                </div>
              </>
            )}
          </div>
        </div>
      )}

      {recordingRole && (
        <RecordOverlay
          role={recordingRole}
          hint={ASSIGNMENTS.find((a) => a.role === recordingRole)?.hint || "any short sound works"}
          onDone={(buf) => saveRecording(recordingRole, buf)}
          onCancel={() => setRecordingRole(null)}
        />
      )}
    </div>
  );
}
