// Project + pipeline orchestration for the crowd·noise desktop app.
//
// The repo filesystem is the source of truth:
//   resources/<id>.mp3                       scraped source audio
//   output/htdemucs_6s/<id>/*.mp3            demucs stems
//   output/trackDecomp/<id>/drum/*           drum decomposition (kick/snare/hats)
//   output/trackDecomp/<id>/piano/piano.mid  basic-pitch MIDI
//   projects/<id>/                           app data: samples, placed tracks,
//                                            Song_State.json, remake.mp3

const fs = require("node:fs");
const path = require("node:path");
const { runJob, runCapture } = require("./pipeline.cjs");

const ROOT = path.resolve(__dirname, "..", "..");
const RESOURCES = path.join(ROOT, "resources");
const STEMS_ROOT = path.join(ROOT, "output", "htdemucs_6s");
const DECOMP_ROOT = path.join(ROOT, "output", "trackDecomp");
const PROJECTS_ROOT = path.join(ROOT, "projects");
const PYTHON = process.env.CROWDNOISE_PYTHON || "python3";

const DRUM_ROLES = ["kick", "snare", "hats"];
const AUDIO_EXTS = new Set([".mp3", ".wav", ".flac"]);

function exists(p) {
  try { fs.accessSync(p); return true; } catch { return false; }
}

function readJson(p) {
  try { return JSON.parse(fs.readFileSync(p, "utf8")); } catch { return null; }
}

function writeJson(p, obj) {
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, JSON.stringify(obj, null, 2) + "\n", "utf8");
}

function projectDir(id) { return path.join(PROJECTS_ROOT, id); }
function projectJsonPath(id) { return path.join(projectDir(id), "project.json"); }

function readProjectJson(id) {
  return readJson(projectJsonPath(id)) || { id, title: id, samples: {}, createdAt: null };
}

function saveProjectJson(id, obj) {
  writeJson(projectJsonPath(id), obj);
}

// ---------------------------------------------------------------------------
// Discovery

function listResourceMp3s() {
  if (!exists(RESOURCES)) return [];
  return fs.readdirSync(RESOURCES)
    .filter((f) => f.toLowerCase().endsWith(".mp3"))
    .map((f) => path.join(RESOURCES, f));
}

function stemFiles(id) {
  const dir = path.join(STEMS_ROOT, id);
  if (!exists(dir)) return [];
  return fs.readdirSync(dir)
    .filter((f) => AUDIO_EXTS.has(path.extname(f).toLowerCase()))
    .sort()
    .map((f) => ({ name: path.basename(f, path.extname(f)), path: path.join(dir, f) }));
}

function drumInfo(id) {
  const dir = path.join(DECOMP_ROOT, id, "drum");
  const summary = readJson(path.join(dir, "decomposition.json"));
  const classes = {};
  for (const role of DRUM_ROLES) {
    const stem = path.join(dir, `${role}.mp3`);
    const times = path.join(dir, `${role}_times.csv`);
    const oneShot = path.join(dir, `${role}_one_shot.mp3`);
    if (!exists(times)) continue;
    let count = 0;
    let timesList = [];
    try {
      const rows = fs.readFileSync(times, "utf8").trim().split("\n").slice(1);
      timesList = rows.map((r) => parseFloat(r.split(",")[1])).filter((t) => !Number.isNaN(t));
      count = timesList.length;
    } catch { /* leave empty */ }
    if (count === 0) continue;
    classes[role] = {
      stem: exists(stem) ? stem : null,
      timesCsv: times,
      times: timesList,
      oneShot: exists(oneShot) ? oneShot : null,
      count,
    };
  }
  return { dir, summary, classes };
}

function pianoInfo(id) {
  const midiPath = path.join(DECOMP_ROOT, id, "piano", "piano.mid");
  return { midiPath: exists(midiPath) ? midiPath : null };
}

async function probeDuration(file) {
  const res = await runCapture("ffprobe", [
    "-v", "error", "-show_entries", "format=duration",
    "-of", "default=nw=1:nk=1", file,
  ]);
  const d = parseFloat(res.stdout.trim());
  return Number.isFinite(d) ? d : 0;
}

// ---------------------------------------------------------------------------
// Song state (same schema as the C++ replace_track/pipeline_test tools)

function songStatePath(id) { return path.join(projectDir(id), "Song_State.json"); }

async function ensureSongState(id) {
  const statePath = songStatePath(id);
  let state = readJson(statePath);
  if (state) return state;

  const stems = stemFiles(id);
  if (!stems.length) return null;
  const source = path.join(RESOURCES, `${id}.mp3`);
  const duration = exists(source) ? await probeDuration(source) : await probeDuration(stems[0].path);

  state = {
    schema_version: 1,
    song_id: id,
    title: id,
    sr: 44100,
    song_length_ms: Math.round(duration * 1000),
    revision: 0,
    history: [],
    instruments: {},
  };
  for (const stem of stems) {
    state.instruments[stem.name] = {
      original_path: stem.path,
      active_path: stem.path,
      active_source: "original",
    };
  }
  writeJson(statePath, state);
  return state;
}

async function replaceTrack(id, instrument, newPath, userId = "you") {
  const state = await ensureSongState(id);
  if (!state || !state.instruments[instrument]) {
    throw new Error(`unknown instrument: ${instrument}`);
  }
  const inst = state.instruments[instrument];
  state.history.push({
    instrument,
    old_active_path: inst.active_path,
    new_active_path: newPath,
    ts: new Date().toISOString(),
    user_id: userId,
  });
  inst.active_path = newPath;
  inst.active_source = "user";
  state.revision += 1;
  writeJson(songStatePath(id), state);
  return state;
}

async function restoreTrack(id, instrument) {
  const state = await ensureSongState(id);
  if (!state || !state.instruments[instrument]) {
    throw new Error(`unknown instrument: ${instrument}`);
  }
  const inst = state.instruments[instrument];
  state.history.push({
    instrument,
    old_active_path: inst.active_path,
    new_active_path: inst.original_path,
    ts: new Date().toISOString(),
    user_id: "you",
  });
  inst.active_path = inst.original_path;
  inst.active_source = "original";
  state.revision += 1;
  writeJson(songStatePath(id), state);
  return state;
}

// ---------------------------------------------------------------------------
// Project listing / detail

function listProjects() {
  const ids = new Set();
  if (exists(STEMS_ROOT)) {
    for (const d of fs.readdirSync(STEMS_ROOT)) {
      if (fs.statSync(path.join(STEMS_ROOT, d)).isDirectory()) ids.add(d);
    }
  }
  for (const f of listResourceMp3s()) ids.add(path.basename(f, ".mp3"));
  if (exists(PROJECTS_ROOT)) {
    for (const d of fs.readdirSync(PROJECTS_ROOT)) {
      if (fs.statSync(path.join(PROJECTS_ROOT, d)).isDirectory()) ids.add(d);
    }
  }

  return [...ids].sort().map((id) => {
    const meta = readProjectJson(id);
    const stems = stemFiles(id);
    const drum = drumInfo(id);
    const state = readJson(songStatePath(id));
    const replaced = state
      ? Object.values(state.instruments).filter((i) => i.active_source === "user").length
      : 0;
    return {
      id,
      title: meta.title || id,
      createdAt: meta.createdAt,
      hasSource: exists(path.join(RESOURCES, `${id}.mp3`)),
      stemCount: stems.length,
      decomposed: Object.keys(drum.classes).length > 0,
      replaced,
      instrumentCount: stems.length,
      remake: exists(path.join(projectDir(id), "remake.mp3")),
    };
  });
}

async function getProject(id) {
  const meta = readProjectJson(id);
  const stems = stemFiles(id);
  const drum = drumInfo(id);
  const piano = pianoInfo(id);
  const sourcePath = path.join(RESOURCES, `${id}.mp3`);
  const state = await ensureSongState(id); // null until stems exist
  const remakePath = path.join(projectDir(id), "remake.mp3");
  const remadeDrumsPath = path.join(projectDir(id), "remade_drums.mp3");
  const duration = exists(sourcePath) ? await probeDuration(sourcePath) : 0;

  return {
    id,
    title: meta.title || id,
    createdAt: meta.createdAt,
    sourcePath: exists(sourcePath) ? sourcePath : null,
    duration,
    stems,
    drum: { summary: drum.summary, classes: drum.classes },
    piano,
    samples: meta.samples || {},
    remadeDrumsPath: exists(remadeDrumsPath) ? remadeDrumsPath : null,
    remakePath: exists(remakePath) ? remakePath : null,
    state,
    steps: {
      scraped: exists(sourcePath),
      split: stems.length > 0,
      decomposed: Object.keys(drum.classes).length > 0,
      midi: !!piano.midiPath,
    },
  };
}

// ---------------------------------------------------------------------------
// Pipeline steps (visible jobs)

function scrapeAndCreate(getWindow, query) {
  const before = new Set(listResourceMp3s());
  const { jobId, promise } = runJob(getWindow, {
    type: "scrape",
    projectId: null,
    cmd: PYTHON,
    args: [path.join(ROOT, "src", "scrape_audio.py"), query],
    cwd: ROOT,
  });

  const resultPromise = promise.then(({ code }) => {
    if (code !== 0) return { ok: false, error: "scrape failed" };
    const after = listResourceMp3s();
    const fresh = after.filter((f) => !before.has(f));
    let target;
    if (fresh.length) {
      target = fresh.sort((a, b) => fs.statSync(b.path || b).mtimeMs - fs.statSync(a.path || a).mtimeMs)[0];
    } else {
      // Scrape may have reused an existing file; pick most recently modified.
      target = after.sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs)[0];
    }
    if (!target) return { ok: false, error: "no audio downloaded" };
    const id = path.basename(target, ".mp3");
    const meta = readProjectJson(id);
    meta.id = id;
    meta.title = meta.title === id || !meta.title ? id : meta.title;
    meta.query = query;
    meta.createdAt = meta.createdAt || new Date().toISOString();
    meta.samples = meta.samples || {};
    saveProjectJson(id, meta);
    return { ok: true, projectId: id };
  });

  return { jobId, resultPromise };
}

function runSplit(getWindow, id) {
  return runJob(getWindow, {
    type: "split",
    projectId: id,
    cmd: PYTHON,
    args: [path.join(ROOT, "src", "split_stems.py")],
    cwd: ROOT,
  });
}

function runDecompose(getWindow, id) {
  const drums = path.join(STEMS_ROOT, id, "drums.mp3");
  return runJob(getWindow, {
    type: "decompose",
    projectId: id,
    cmd: PYTHON,
    args: [path.join(ROOT, "src", "decompose_drums.py"), drums],
    cwd: ROOT,
  });
}

function runMidi(getWindow, id) {
  const piano = path.join(STEMS_ROOT, id, "piano.mp3");
  const out = path.join(DECOMP_ROOT, id, "piano", "piano.mid");
  return runJob(getWindow, {
    type: "midi",
    projectId: id,
    cmd: PYTHON,
    args: [path.join(ROOT, "src", "basic_pitch_to_midi.py"), piano, "--out", out],
    cwd: ROOT,
  });
}

// ---------------------------------------------------------------------------
// Samples: ingest (preserve original + canonical mp3), gain match, placement

async function ingestSample(id, role, inputPath, source, originalName) {
  const sampleId = `${role}-${Date.now()}`;
  const storageRoot = path.join(projectDir(id), "storage");
  fs.mkdirSync(storageRoot, { recursive: true });

  const bin = path.join(ROOT, "src", "ingest_sample");
  const res = await runCapture(bin, [
    "--input", inputPath,
    "--storage-root", storageRoot,
    "--sample-id", sampleId,
  ], ROOT);

  let parsed = null;
  try {
    const jsonStart = res.stdout.indexOf("{");
    parsed = JSON.parse(res.stdout.slice(jsonStart));
  } catch { /* fall through */ }

  const canonical = path.join(storageRoot, "samples", sampleId, "canonical", "canonical.mp3");
  if (res.code !== 0 || !exists(canonical)) {
    throw new Error(`ingest failed (code ${res.code}): ${res.stderr || res.stdout}`.slice(0, 400));
  }

  // Find preserved original
  const originalDir = path.join(storageRoot, "samples", sampleId, "original");
  let originalPath = null;
  if (exists(originalDir)) {
    const files = fs.readdirSync(originalDir);
    if (files.length) originalPath = path.join(originalDir, files[0]);
  }

  const meta = readProjectJson(id);
  meta.samples = meta.samples || {};
  meta.samples[role] = {
    sampleId,
    role,
    source, // 'recorded' | 'uploaded'
    originalName: originalName || path.basename(inputPath),
    originalPath,
    canonicalPath: canonical,
    createdAt: new Date().toISOString(),
    ingest: parsed,
    gain: null,
    placedPath: null,
  };
  saveProjectJson(id, meta);
  return meta.samples[role];
}

async function placeSample(getWindow, id, role) {
  const meta = readProjectJson(id);
  const sample = meta.samples && meta.samples[role];
  if (!sample) throw new Error(`no sample for ${role}`);
  const drum = drumInfo(id);
  const cls = drum.classes[role];
  if (!cls) throw new Error(`no decomposition for ${role}`);

  const source = path.join(RESOURCES, `${id}.mp3`);
  const duration = await probeDuration(exists(source) ? source : path.join(STEMS_ROOT, id, "drums.mp3"));

  // 1) Volume-match the user sample to the original one-shot.
  let gain = 1.0;
  const reference = cls.oneShot || cls.stem;
  if (reference) {
    const res = await runCapture(PYTHON, [
      path.join(ROOT, "src", "match_sample_gain.py"),
      "--reference", reference,
      "--sample", sample.canonicalPath,
      "--times", cls.timesCsv,
    ], ROOT);
    const g = parseFloat(res.stdout.trim());
    if (Number.isFinite(g) && g > 0) gain = g;
  }

  // 2) Place the sample at every detected hit time.
  const outPath = path.join(projectDir(id), `placed_${role}.mp3`);
  const { jobId, promise } = runJob(getWindow, {
    type: `place:${role}`,
    projectId: id,
    cmd: path.join(ROOT, "bin", "repeat_sample_at_times_cli"),
    args: [
      "--times", cls.timesCsv,
      "--sample", sample.canonicalPath,
      "--out", outPath,
      "--length-seconds", String(duration),
      "--gain", gain.toFixed(4),
    ],
    cwd: ROOT,
  });

  const resultPromise = promise.then(({ code }) => {
    if (code !== 0 || !exists(outPath)) return { ok: false };
    const m = readProjectJson(id);
    m.samples[role].gain = gain;
    m.samples[role].placedPath = outPath;
    m.samples[role].placedAt = new Date().toISOString();
    saveProjectJson(id, m);
    return { ok: true, placedPath: outPath, gain };
  });

  return { jobId, resultPromise };
}

async function buildRemadeDrums(getWindow, id) {
  const meta = readProjectJson(id);
  const placed = DRUM_ROLES
    .map((r) => meta.samples && meta.samples[r] && meta.samples[r].placedPath)
    .filter((p) => p && exists(p));
  if (!placed.length) throw new Error("no placed tracks yet");

  const outPath = path.join(projectDir(id), "remade_drums.mp3");
  const args = [];
  for (const p of placed) args.push("-i", p);
  const filter = `amix=inputs=${placed.length}:duration=longest:normalize=0,volume=1.2,alimiter=limit=0.95`;
  args.push("-filter_complex", filter, "-c:a", "libmp3lame", "-q:a", "2", "-y", outPath);

  const { jobId, promise } = runJob(getWindow, {
    type: "remade_drums",
    projectId: id,
    cmd: "ffmpeg",
    args: ["-hide_banner", ...args],
    cwd: ROOT,
  });

  const resultPromise = promise.then(async ({ code }) => {
    if (code !== 0 || !exists(outPath)) return { ok: false };
    await replaceTrack(id, "drums", outPath);
    return { ok: true, path: outPath };
  });
  return { jobId, resultPromise };
}

async function buildFinalMix(getWindow, id) {
  const state = await ensureSongState(id);
  if (!state) throw new Error("no stems yet");
  const active = Object.values(state.instruments)
    .map((i) => i.active_path)
    .filter((p) => exists(p));
  if (!active.length) throw new Error("no active tracks");

  const outPath = path.join(projectDir(id), "remake.mp3");
  const args = [];
  for (const p of active) args.push("-i", p);
  const filter = `amix=inputs=${active.length}:duration=longest:normalize=0,alimiter=limit=0.95`;
  args.push("-filter_complex", filter, "-c:a", "libmp3lame", "-q:a", "2", "-y", outPath);

  const { jobId, promise } = runJob(getWindow, {
    type: "final_mix",
    projectId: id,
    cmd: "ffmpeg",
    args: ["-hide_banner", ...args],
    cwd: ROOT,
  });

  const resultPromise = promise.then(({ code }) => {
    if (code !== 0 || !exists(outPath)) return { ok: false };
    return { ok: true, path: outPath };
  });
  return { jobId, resultPromise };
}

module.exports = {
  ROOT,
  PROJECTS_ROOT,
  projectDir,
  listProjects,
  getProject,
  scrapeAndCreate,
  runSplit,
  runDecompose,
  runMidi,
  ingestSample,
  placeSample,
  buildRemadeDrums,
  buildFinalMix,
  replaceTrack,
  restoreTrack,
  probeDuration,
};
