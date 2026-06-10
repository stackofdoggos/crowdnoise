// Job runner: spawns pipeline processes (python scripts, native CLIs, ffmpeg)
// and streams their output to the renderer so every step is visible in the UI.

const { spawn } = require("node:child_process");

const jobs = new Map();
let nextJobId = 1;

function broadcast(getWindow, payload) {
  const win = getWindow();
  if (win && !win.isDestroyed()) {
    win.webContents.send("job:update", payload);
  }
}

/**
 * Run a command as a tracked job.
 * opts: { type, projectId, cmd, args, cwd, env }
 * Returns { jobId, promise } where promise resolves { code, output }.
 */
function runJob(getWindow, opts) {
  const jobId = nextJobId++;
  const job = {
    id: jobId,
    type: opts.type,
    projectId: opts.projectId || null,
    status: "running",
    lines: [],
    startedAt: Date.now(),
  };
  jobs.set(jobId, job);

  broadcast(getWindow, {
    id: jobId,
    type: job.type,
    projectId: job.projectId,
    status: "running",
    line: `$ ${opts.cmd} ${(opts.args || []).join(" ")}`,
  });

  const promise = new Promise((resolve) => {
    let child;
    try {
      child = spawn(opts.cmd, opts.args || [], {
        cwd: opts.cwd,
        env: { ...process.env, PYTHONUNBUFFERED: "1", ...(opts.env || {}) },
      });
    } catch (err) {
      job.status = "error";
      broadcast(getWindow, {
        id: jobId, type: job.type, projectId: job.projectId,
        status: "error", line: String(err),
      });
      resolve({ code: -1, output: String(err) });
      return;
    }

    const onChunk = (chunk) => {
      const text = chunk.toString();
      for (const rawLine of text.split(/\r?\n/)) {
        const line = rawLine.trimEnd();
        if (!line) continue;
        job.lines.push(line);
        broadcast(getWindow, {
          id: jobId, type: job.type, projectId: job.projectId,
          status: "running", line,
        });
      }
    };
    child.stdout.on("data", onChunk);
    child.stderr.on("data", onChunk);

    child.on("close", (code) => {
      job.status = code === 0 ? "done" : "error";
      job.code = code;
      broadcast(getWindow, {
        id: jobId, type: job.type, projectId: job.projectId,
        status: job.status, code,
      });
      resolve({ code, output: job.lines.join("\n") });
    });
  });

  return { jobId, promise };
}

/** Run a command silently (no job UI), capturing stdout. */
function runCapture(cmd, args, cwd) {
  return new Promise((resolve) => {
    const child = spawn(cmd, args, { cwd });
    let out = "";
    let err = "";
    child.stdout.on("data", (c) => (out += c.toString()));
    child.stderr.on("data", (c) => (err += c.toString()));
    child.on("close", (code) => resolve({ code, stdout: out, stderr: err }));
    child.on("error", (e) => resolve({ code: -1, stdout: out, stderr: String(e) }));
  });
}

function getJob(jobId) {
  const job = jobs.get(jobId);
  if (!job) return null;
  return {
    id: job.id,
    type: job.type,
    projectId: job.projectId,
    status: job.status,
    code: job.code,
    lines: job.lines.slice(-400),
  };
}

module.exports = { runJob, runCapture, getJob };
