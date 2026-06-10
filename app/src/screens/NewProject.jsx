import React, { useEffect, useRef, useState } from "react";
import { api } from "../api.js";
import { useNav, useJobs } from "../App.jsx";
import JobConsole from "../components/JobConsole.jsx";

const STEP_LABELS = {
  scrape: "scrape audio",
  split: "split stems (demucs)",
  decompose: "decompose drums (kick / snare / hats)",
  midi: "piano → midi (basic pitch)",
};

export default function NewProject() {
  const nav = useNav();
  const { jobs } = useJobs();
  const [query, setQuery] = useState("");
  const [steps, setSteps] = useState([]); // [{key, jobId, status}]
  const [projectId, setProjectId] = useState(null);
  const [running, setRunning] = useState(false);
  const [error, setError] = useState(null);
  const chainRef = useRef({ projectId: null, started: new Set() });

  // Merge live job status into steps.
  const stepsView = steps.map((s) => ({
    ...s,
    status: s.jobId && jobs[s.jobId] ? jobs[s.jobId].status : s.status,
  }));

  const allLines = stepsView.flatMap((s) => (s.jobId && jobs[s.jobId] ? jobs[s.jobId].lines : []));

  useEffect(() => {
    const off = api.onScrapeDone(async (p) => {
      if (!p.ok) {
        setError(p.error || "scrape failed");
        setRunning(false);
        return;
      }
      setProjectId(p.projectId);
      chainRef.current.projectId = p.projectId;
      startStep("split", p.projectId);
    });
    return off;
  }, []);

  async function startStep(key, pid) {
    if (chainRef.current.started.has(key)) return;
    chainRef.current.started.add(key);
    const { jobId } = await api.runStep(pid, key);
    setSteps((prev) => prev.map((s) => (s.key === key ? { ...s, jobId, status: "running" } : s)));
  }

  // Chain steps as jobs complete.
  useEffect(() => {
    const pid = chainRef.current.projectId;
    if (!pid) return;
    (async () => {
      const split = stepsView.find((s) => s.key === "split");
      const dec = stepsView.find((s) => s.key === "decompose");
      const midi = stepsView.find((s) => s.key === "midi");

      if (split?.status === "done" && dec && dec.status === "pending") {
        const proj = await api.getProject(pid);
        if (proj.stems.some((st) => st.name === "drums")) {
          startStep("decompose", pid);
        } else {
          setSteps((prev) => prev.map((s) => (s.key === "decompose" ? { ...s, status: "skipped" } : s)));
        }
        if (midi && !proj.stems.some((st) => st.name === "piano")) {
          setSteps((prev) => prev.map((s) => (s.key === "midi" ? { ...s, status: "skipped" } : s)));
        }
      }
      if (dec?.status === "done" && midi && midi.status === "pending") {
        startStep("midi", pid);
      }
      const done = stepsView.every((s) => ["done", "skipped", "error"].includes(s.status));
      if (stepsView.length && done && running) setRunning(false);
    })();
  }, [jobs]); // eslint-disable-line react-hooks/exhaustive-deps

  async function begin() {
    if (!query.trim() || running) return;
    setError(null);
    setRunning(true);
    setProjectId(null);
    chainRef.current = { projectId: null, started: new Set() };
    setSteps([
      { key: "scrape", jobId: null, status: "running" },
      { key: "split", jobId: null, status: "pending" },
      { key: "decompose", jobId: null, status: "pending" },
      { key: "midi", jobId: null, status: "pending" },
    ]);
    const { jobId } = await api.scrape(query.trim());
    setSteps((prev) => prev.map((s) => (s.key === "scrape" ? { ...s, jobId } : s)));
  }

  const finished = stepsView.length > 0 && stepsView.every((s) => ["done", "skipped", "error"].includes(s.status));

  return (
    <div>
      <h1>new project</h1>
      <p className="sub">
        paste a youtube link or type a song name. we'll scrape the audio, split it into
        instrument stems, and decompose the drums into kick / snare / hats — all visible below.
      </p>

      <div className="section row">
        <input
          className="input"
          placeholder='try "kanye west homecoming" or a youtube url'
          value={query}
          onChange={(e) => setQuery(e.target.value)}
          onKeyDown={(e) => e.key === "Enter" && begin()}
          disabled={running}
        />
        <button className="btn primary" onClick={begin} disabled={running || !query.trim()}>
          {running ? "working…" : "scrape it"}
        </button>
      </div>

      {error && <p className="sub" style={{ color: "var(--danger)", marginTop: 12 }}>{error}</p>}

      {stepsView.length > 0 && (
        <div className="section">
          <h2>pipeline</h2>
          <div className="steps" style={{ marginBottom: 14 }}>
            {stepsView.map((s) => (
              <div key={s.key} className="step-chip">
                <span className={`dot-status ${s.status === "done" ? "done" : s.status === "running" ? "running" : s.status === "error" ? "error" : ""}`} />
                {STEP_LABELS[s.key]}
                {s.status === "skipped" && <span className="faint">(skipped)</span>}
              </div>
            ))}
          </div>
          <JobConsole lines={allLines} />
        </div>
      )}

      {finished && projectId && (
        <div className="section row">
          <button className="btn primary" onClick={() => nav("project", { id: projectId })}>
            open project →
          </button>
          <span className="faint">{projectId}</span>
        </div>
      )}
    </div>
  );
}
