import React, { createContext, useCallback, useContext, useEffect, useState } from "react";
import { api } from "./api.js";
import Home from "./screens/Home.jsx";
import NewProject from "./screens/NewProject.jsx";
import Project from "./screens/Project.jsx";
import DrumLab from "./screens/DrumLab.jsx";
import Provenance from "./screens/Provenance.jsx";
import Community from "./screens/Community.jsx";

export const JobsContext = createContext({ jobs: {}, version: 0 });
export const NavContext = createContext(() => {});

export function useJobs() {
  return useContext(JobsContext);
}
export function useNav() {
  return useContext(NavContext);
}

export default function App() {
  const [route, setRoute] = useState({ name: "home" });
  const [jobs, setJobs] = useState({});
  const [version, setVersion] = useState(0); // bumps when any job finishes

  useEffect(() => {
    const off = api.onJobUpdate((p) => {
      setJobs((prev) => {
        const job = prev[p.id] || { id: p.id, type: p.type, projectId: p.projectId, status: "running", lines: [] };
        const lines = p.line ? [...job.lines, p.line].slice(-500) : job.lines;
        return { ...prev, [p.id]: { ...job, status: p.status, code: p.code, lines } };
      });
      if (p.status === "done" || p.status === "error") setVersion((v) => v + 1);
    });
    return off;
  }, []);

  const nav = useCallback((name, params = {}) => setRoute({ name, ...params }), []);

  const screens = {
    home: <Home />,
    new: <NewProject />,
    project: <Project id={route.id} />,
    drumlab: <DrumLab id={route.id} />,
    provenance: <Provenance id={route.id} />,
    community: <Community />,
  };

  return (
    <NavContext.Provider value={nav}>
      <JobsContext.Provider value={{ jobs, version }}>
        <div className="shell">
          <aside className="sidebar">
            <div className="logo">crowd<span className="dot">·</span>noise</div>
            <button className={`nav-item ${route.name === "home" ? "active" : ""}`} onClick={() => nav("home")}>
              home
            </button>
            <button className={`nav-item ${route.name === "new" ? "active" : ""}`} onClick={() => nav("new")}>
              new project
            </button>
            <button className={`nav-item ${route.name === "community" ? "active" : ""}`} onClick={() => nav("community")}>
              community
            </button>
            <div className="sidebar-footer">
              make an album with your friends. sample the world. prove where every sound came from.
            </div>
          </aside>
          <main className="main">
            <div className="main-inner">{screens[route.name] || screens.home}</div>
          </main>
        </div>
      </JobsContext.Provider>
    </NavContext.Provider>
  );
}
