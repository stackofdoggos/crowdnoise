const { app, BrowserWindow, ipcMain, protocol, net, dialog, session, systemPreferences } = require("electron");
const path = require("node:path");
const fs = require("node:fs");
const os = require("node:os");
const { pathToFileURL } = require("node:url");

const pipeline = require("./pipeline.cjs");
const projects = require("./projects.cjs");

let mainWindow = null;
const getWindow = () => mainWindow;

protocol.registerSchemesAsPrivileged([
  { scheme: "media", privileges: { standard: false, stream: true, supportFetchAPI: true, bypassCSP: true } },
]);

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1320,
    height: 880,
    minWidth: 980,
    minHeight: 640,
    backgroundColor: "#0b0b0d",
    titleBarStyle: process.platform === "darwin" ? "hiddenInset" : "default",
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });

  const devUrl = process.env.VITE_DEV_SERVER_URL;
  if (devUrl) {
    mainWindow.loadURL(devUrl);
    // Surface renderer errors in the dev terminal.
    mainWindow.webContents.on("console-message", (_e, level, message) => {
      if (level >= 2) console.log(`[renderer] ${message}`);
    });
  } else {
    mainWindow.loadFile(path.join(__dirname, "..", "dist", "index.html"));
  }
}

app.whenReady().then(async () => {
  // Serve local repo files (audio stems, album art) to the renderer.
  protocol.handle("media", (request) => {
    const url = new URL(request.url);
    let p = decodeURIComponent(url.pathname);
    if (process.platform === "win32" && p.startsWith("/")) p = p.slice(1);
    return net.fetch(pathToFileURL(p).toString());
  });

  // Allow microphone for in-app recording.
  session.defaultSession.setPermissionRequestHandler((_wc, permission, cb) => {
    cb(permission === "media");
  });
  if (process.platform === "darwin") {
    try { await systemPreferences.askForMediaAccess("microphone"); } catch { /* user denied */ }
  }

  registerIpc();
  createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});

function registerIpc() {
  ipcMain.handle("projects:list", () => projects.listProjects());
  ipcMain.handle("projects:get", (_e, id) => projects.getProject(id));

  ipcMain.handle("projects:scrape", (_e, query) => {
    const { jobId, resultPromise } = projects.scrapeAndCreate(getWindow, query);
    resultPromise.then((result) => {
      const win = getWindow();
      if (win && !win.isDestroyed()) win.webContents.send("scrape:done", { jobId, ...result });
    });
    return { jobId };
  });

  ipcMain.handle("projects:runStep", (_e, id, step) => {
    let res;
    if (step === "split") res = projects.runSplit(getWindow, id);
    else if (step === "decompose") res = projects.runDecompose(getWindow, id);
    else if (step === "midi") res = projects.runMidi(getWindow, id);
    else throw new Error(`unknown step: ${step}`);
    return { jobId: res.jobId };
  });

  ipcMain.handle("job:get", (_e, jobId) => pipeline.getJob(jobId));

  ipcMain.handle("samples:saveRecording", async (_e, id, role, arrayBuffer, ext) => {
    const tmp = path.join(os.tmpdir(), `crowdnoise-rec-${Date.now()}.${ext || "webm"}`);
    fs.writeFileSync(tmp, Buffer.from(arrayBuffer));
    try {
      return await projects.ingestSample(id, role, tmp, "recorded", `mic recording.${ext || "webm"}`);
    } finally {
      try { fs.unlinkSync(tmp); } catch { /* ignore */ }
    }
  });

  ipcMain.handle("samples:import", async (_e, id, role) => {
    const result = await dialog.showOpenDialog(getWindow(), {
      title: `choose a ${role} sample`,
      properties: ["openFile"],
      filters: [{ name: "audio/video", extensions: ["mp3", "wav", "m4a", "aac", "flac", "ogg", "webm", "mp4", "mov"] }],
    });
    if (result.canceled || !result.filePaths.length) return null;
    return projects.ingestSample(id, role, result.filePaths[0], "uploaded");
  });

  ipcMain.handle("samples:place", async (_e, id, role) => {
    const { jobId, resultPromise } = await projects.placeSample(getWindow, id, role);
    resultPromise.then((result) => {
      const win = getWindow();
      if (win && !win.isDestroyed()) win.webContents.send("place:done", { jobId, projectId: id, role, ...result });
    });
    return { jobId };
  });

  ipcMain.handle("mix:remadeDrums", async (_e, id) => {
    const { jobId, resultPromise } = await projects.buildRemadeDrums(getWindow, id);
    resultPromise.then((result) => {
      const win = getWindow();
      if (win && !win.isDestroyed()) win.webContents.send("mix:done", { jobId, projectId: id, kind: "remade_drums", ...result });
    });
    return { jobId };
  });

  ipcMain.handle("mix:final", async (_e, id) => {
    const { jobId, resultPromise } = await projects.buildFinalMix(getWindow, id);
    resultPromise.then((result) => {
      const win = getWindow();
      if (win && !win.isDestroyed()) win.webContents.send("mix:done", { jobId, projectId: id, kind: "final", ...result });
    });
    return { jobId };
  });

  ipcMain.handle("tracks:replace", (_e, id, instrument, newPath) => projects.replaceTrack(id, instrument, newPath));
  ipcMain.handle("tracks:restore", (_e, id, instrument) => projects.restoreTrack(id, instrument));

  ipcMain.handle("fs:readBuffer", (_e, p) => {
    const buf = fs.readFileSync(p);
    return buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength);
  });

  ipcMain.handle("fs:revealInFinder", (_e, p) => {
    const { shell } = require("electron");
    shell.showItemInFolder(p);
  });

  ipcMain.handle("app:root", () => projects.ROOT);
}
