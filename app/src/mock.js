// Local mock data for the social layer (no backend yet).
// Everything here is clearly fake and lives only in the UI.

export const ME = { id: "you", name: "you", color: "#9be7a2" };

export const FRIENDS = [
  { id: "mia", name: "mia", color: "#9bc8e7", taste: "soul samples, boom bap" },
  { id: "theo", name: "theo", color: "#c89be7", taste: "house, uk garage" },
  { id: "june", name: "june", color: "#e7d49b", taste: "indie pop, hyperpop" },
  { id: "ray", name: "ray", color: "#e7b89b", taste: "trap, drill" },
];

export const GROUP = {
  id: "couch-collective",
  name: "couch collective",
  members: [ME, ...FRIENDS.slice(0, 3)],
};

export const ASSIGNMENTS = [
  { role: "kick", assignee: "you", hint: "a low punch — table thump, chest hit, basketball bounce" },
  { role: "snare", assignee: "mia", hint: "a sharp crack — book slap, clap, ruler on desk" },
  { role: "hats", assignee: "theo", hint: "a bright tick — keys, pen click, finger snap" },
];

export const LEADERBOARD = [
  { group: "couch collective", song: "i wonder", score: 87, votes: 132 },
  { group: "basement tapes", song: "homecoming", score: 81, votes: 98 },
  { group: "midnight snacc", song: "piano man", score: 74, votes: 76 },
  { group: "no auxiliary", song: "ye.", score: 66, votes: 51 },
];

export const COMMENTS = [
  { user: "mia", text: "the basketball kick is genius. it knocks.", votes: 14 },
  { user: "theo", text: "hats from a bike spoke?? ok that's creative", votes: 9 },
  { user: "june", text: "play the provenance reveal at the listening party", votes: 7 },
];

export const DIFFICULTY = [
  { song: "ye — kanye west", level: "easier", reward: "1x cover unlock" },
  { song: "graduation — kanye west", level: "medium", reward: "2x cover unlock" },
  { song: "currents — tame impala", level: "hard", reward: "3x cover unlock" },
  { song: "whole lotta red — playboi carti", level: "very hard", reward: "5x + gold frame" },
];

// rarity from transformation-chain complexity
export function rarityForSample(sample) {
  let chain = 1; // recorded/uploaded
  if (sample?.canonicalPath) chain += 1; // canonicalized
  if (sample?.gain != null) chain += 1; // gain-matched
  if (sample?.placedPath) chain += 1; // placed at hit times
  if (chain >= 4) return "epic";
  if (chain >= 3) return "rare";
  return "common";
}

export function colorFor(name) {
  const palette = ["#9be7a2", "#9bc8e7", "#c89be7", "#e7d49b", "#e7b89b"];
  let h = 0;
  for (const c of name) h = (h * 31 + c.charCodeAt(0)) >>> 0;
  return palette[h % palette.length];
}
