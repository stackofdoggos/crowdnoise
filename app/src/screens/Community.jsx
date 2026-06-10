import React, { useEffect, useState } from "react";
import { api } from "../api.js";
import { FRIENDS, GROUP, LEADERBOARD, COMMENTS, DIFFICULTY, colorFor, rarityForSample } from "../mock.js";
import Player from "../components/Player.jsx";

// Social layer — mocked locally (no backend yet), but sample cards are real:
// they're built from actual samples you've recorded in your projects.
export default function Community() {
  const [cards, setCards] = useState([]);
  const [votes, setVotes] = useState(() => COMMENTS.map((c) => c.votes));

  useEffect(() => {
    (async () => {
      const projects = await api.listProjects();
      const all = [];
      for (const p of projects) {
        const proj = await api.getProject(p.id);
        for (const s of Object.values(proj.samples || {})) {
          all.push({ ...s, project: proj.title });
        }
      }
      setCards(all);
    })();
  }, []);

  return (
    <div>
      <h1>community</h1>
      <p className="sub">leaderboards, votes and sample cards. social features are mocked locally for now.</p>

      <div className="section grid2">
        <div className="panel">
          <h2>weekly leaderboard</h2>
          {LEADERBOARD.map((entry, i) => (
            <div className="list-row" key={entry.group}>
              <span className="faint" style={{ width: 18 }}>{i + 1}</span>
              <div className="col" style={{ flex: 1 }}>
                <h3>{entry.group}</h3>
                <span className="faint">remade "{entry.song}" · {entry.votes} votes</span>
              </div>
              <div className="col" style={{ width: 120, gap: 6 }}>
                <div className="vote-bar"><div className="fill" style={{ width: `${entry.score}%` }} /></div>
                <span className="faint" style={{ textAlign: "right" }}>{entry.score}</span>
              </div>
            </div>
          ))}
        </div>

        <div className="panel">
          <h2>difficulty board</h2>
          {DIFFICULTY.map((d) => (
            <div className="list-row" key={d.song}>
              <div className="col" style={{ flex: 1 }}>
                <h3>{d.song}</h3>
                <span className="faint">{d.level}</span>
              </div>
              <span className="tag good">{d.reward}</span>
            </div>
          ))}
        </div>
      </div>

      <div className="section">
        <h2>your sample cards</h2>
        {cards.length === 0 ? (
          <div className="empty">record samples in a project to mint cards.</div>
        ) : (
          <div className="grid3">
            {cards.map((c) => {
              const rarity = rarityForSample(c);
              return (
                <div key={c.sampleId} className={`sample-card ${rarity}`}>
                  <span className="rarity">{rarity}</span>
                  <div className="col" style={{ gap: 10 }}>
                    <div className="col" style={{ gap: 2 }}>
                      <h3>{c.role}</h3>
                      <span className="faint">{c.project}</span>
                    </div>
                    <Player src={c.canonicalPath} compact />
                    <span className="faint">
                      {c.source} → canonical{c.gain != null ? " → gain-matched" : ""}{c.placedPath ? " → placed" : ""}
                    </span>
                  </div>
                </div>
              );
            })}
          </div>
        )}
      </div>

      <div className="section grid2">
        <div className="panel">
          <h2>comments on "{LEADERBOARD[0].song}"</h2>
          {COMMENTS.map((c, i) => (
            <div className="list-row" key={i}>
              <div className="avatar" style={{ background: colorFor(c.user) }}>{c.user[0]}</div>
              <div className="col" style={{ flex: 1 }}>
                <span style={{ fontSize: 13 }}>{c.text}</span>
                <span className="faint">{c.user}</span>
              </div>
              <button
                className="btn small ghost"
                onClick={() => setVotes((v) => v.map((n, j) => (i === j ? n + 1 : n)))}
              >
                ▲ {votes[i]}
              </button>
            </div>
          ))}
        </div>

        <div className="panel">
          <h2>find your people</h2>
          <p className="faint" style={{ marginBottom: 10 }}>music-taste-based discovery</p>
          {FRIENDS.map((f) => (
            <div className="list-row" key={f.id}>
              <div className="avatar" style={{ background: f.color }}>{f.name[0]}</div>
              <div className="col" style={{ flex: 1 }}>
                <h3>{f.name}</h3>
                <span className="faint">{f.taste}</span>
              </div>
              <span className="tag">{GROUP.members.some((m) => m.id === f.id) ? "in your group" : "suggested"}</span>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
