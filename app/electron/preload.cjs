const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("crowdnoise", {
  listProjects: () => ipcRenderer.invoke("projects:list"),
  getProject: (id) => ipcRenderer.invoke("projects:get", id),
  scrape: (query) => ipcRenderer.invoke("projects:scrape", query),
  runStep: (id, step) => ipcRenderer.invoke("projects:runStep", id, step),
  getJob: (jobId) => ipcRenderer.invoke("job:get", jobId),

  saveRecording: (id, role, arrayBuffer, ext) =>
    ipcRenderer.invoke("samples:saveRecording", id, role, arrayBuffer, ext),
  importSample: (id, role) => ipcRenderer.invoke("samples:import", id, role),
  placeSample: (id, role) => ipcRenderer.invoke("samples:place", id, role),

  buildRemadeDrums: (id) => ipcRenderer.invoke("mix:remadeDrums", id),
  buildFinalMix: (id) => ipcRenderer.invoke("mix:final", id),
  replaceTrack: (id, instrument, newPath) => ipcRenderer.invoke("tracks:replace", id, instrument, newPath),
  restoreTrack: (id, instrument) => ipcRenderer.invoke("tracks:restore", id, instrument),

  readBuffer: (p) => ipcRenderer.invoke("fs:readBuffer", p),
  revealInFinder: (p) => ipcRenderer.invoke("fs:revealInFinder", p),
  getRoot: () => ipcRenderer.invoke("app:root"),

  onJobUpdate: (cb) => {
    const handler = (_e, payload) => cb(payload);
    ipcRenderer.on("job:update", handler);
    return () => ipcRenderer.removeListener("job:update", handler);
  },
  onScrapeDone: (cb) => {
    const handler = (_e, payload) => cb(payload);
    ipcRenderer.on("scrape:done", handler);
    return () => ipcRenderer.removeListener("scrape:done", handler);
  },
  onPlaceDone: (cb) => {
    const handler = (_e, payload) => cb(payload);
    ipcRenderer.on("place:done", handler);
    return () => ipcRenderer.removeListener("place:done", handler);
  },
  onMixDone: (cb) => {
    const handler = (_e, payload) => cb(payload);
    ipcRenderer.on("mix:done", handler);
    return () => ipcRenderer.removeListener("mix:done", handler);
  },
});
