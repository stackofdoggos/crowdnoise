// Thin wrapper around the preload bridge.
// Falls back to an inert stub when opened in a plain browser (UI dev only).
const stub = {
  listProjects: async () => [],
  getProject: async () => null,
  scrape: async () => ({ jobId: 0 }),
  runStep: async () => ({ jobId: 0 }),
  getJob: async () => null,
  saveRecording: async () => null,
  importSample: async () => null,
  placeSample: async () => ({ jobId: 0 }),
  buildRemadeDrums: async () => ({ jobId: 0 }),
  buildFinalMix: async () => ({ jobId: 0 }),
  replaceTrack: async () => null,
  restoreTrack: async () => null,
  readBuffer: async () => new ArrayBuffer(0),
  revealInFinder: async () => {},
  getRoot: async () => "/",
  onJobUpdate: () => () => {},
  onScrapeDone: () => () => {},
  onPlaceDone: () => () => {},
  onMixDone: () => () => {},
};

export const api = window.crowdnoise || stub;

export function mediaUrl(absPath) {
  if (!absPath) return null;
  return `media://local${encodeURI(absPath)}`;
}

export function fmtTime(sec) {
  if (!Number.isFinite(sec)) return "0:00";
  const m = Math.floor(sec / 60);
  const s = Math.floor(sec % 60);
  return `${m}:${String(s).padStart(2, "0")}`;
}
