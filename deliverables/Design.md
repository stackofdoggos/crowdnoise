# crowd·noise

_make an album with your friends. sample the world. prove where every sound came from._

---

## Concept (What It Is)

**crowd·noise** is a community-based, gamified music production app where groups of friends recreate well-known songs using **tiny recordings (≈0.1–10s)** captured from themselves + their environment. you transform those clips with an in‑app sound changer, layer them into a remake, and **unlock more of the album artwork as it improves**.

the twist: **every transformed sound keeps its provenance**—you can always reveal the original audio/video source and see the story behind the sample.

---

## Goal / Purpose (Why It Exists)

- **make music feel accessible**: no gear, no studio—just a phone and curiosity.
- **make collaboration the instrument**: friends split the work, trade samples, and build a shared library.
- **make sampling legible**: remakes are fun, but the "where did that sound come from?" moment is the point.

---

## What Makes It Different

- **album-reveal progression**: better remakes = more of the cover revealed (progress you can _see_).
- **provenance-first sampling**: any effect chain can be rewound to the original clip; credit is built in.
- **collectible sample cards**: transformed sounds become shareable cards with rarity tiers based on transformation complexity.
- **assignment-based collaboration**: the app can hand out "make this sound" tasks so everyone contributes.
- **indie, minimal ux**: lowercases, no clutter—like [untitled], but for collaborative remakes.

---

## Target Audience

- **people who love music** and want to make it with friends—especially without instruments or equipment
- broad age range; especially good for **hip-hop / sample-driven** listening habits (high remixability)
- users who want music to feel **less corporatized** and more playful + community-owned

---

## Core Loop (What You Do)

1. form / join a group
2. pick an album or song to remake
3. the project shows the sounds you need (or assigns parts to teammates)
4. record/upload micro-clips + trim fast
5. transform clips with simple effects (sound changer)
6. layer tracks into the remake + swap elements (e.g., "replace drums with our drums")
7. submit / share; get votes + climb leaderboards
8. unlock more cover art; keep building your shared sample library
9. anytime: open provenance and trace any sound back to its original clip

---

## UI (Key Screens)

| Screen         | Vibe          | What You See                                 |
| -------------- | ------------- | -------------------------------------------- |
| **home**       | calm + social | groups + active projects                     |
| **project**    | focused       | layered tracks with simple controls          |
| **recording**  | full-screen   | one button, no clutter                       |
| **editing**    | minimal       | a few sliders + real-time preview            |
| **album**      | rewarding     | artwork slowly revealed as progress is made  |
| **provenance** | transparent   | a clean chain showing where sounds came from |

---

## How I'll Communicate It (Demo + Story)

- **30-second hook**: "we remade a song using only table taps + keys + a basketball."
- **before/after moment**: play the original bar → play the remake bar → show the layer stack.
- **provenance reveal**: tap a sound → watch it rewind to the original clip (credit + context).
- **shareable artifacts**: export the remake + a provenance card (what we used + who made what).
- **community angle**: feature weekly remakes + "hard albums" challenges with extra rewards.

---

## Technical Specifications (How It Could Be Built)

### Technology Stack

- mobile (ios + android); native or cross-platform; on-device recording/editing; backend for auth/projects/storage/feeds

### Architecture

- separate audio engine + project state from ui; async collaboration via project events; provenance as first-class data

### Data Model (Sketch)

- project / clip / sound (derived + effect chain + original pointer) / track layer

### Security & Performance

- clear mic/camera permissions; per-project privacy; caching + background uploads; keep originals while streaming lightweight versions

### Core Features

#### Original Design Features

- **recording / uploading**: capture short video/audio clips (≈0.1–10s) with fast trimming
- **sound changer**: simple effects to turn raw clips into usable instruments (preview in realtime)
- **replace sounds in a song**: swap original elements (e.g., drums) with your group's sounds and hear the remake
- **layered mixing**: basic track stack (volume, mute/solo, timing nudges) without overwhelming controls
- **shared sample library**: save edited sounds with friends; reuse across projects
- **assignment system**: assign parts of a track to specific people ("you make this snare") to drive collaboration
- **leaderboards + voting**: showcase album remakes; community votes on creativity + quality
- **difficulty ranking + rewards**: easier→harder albums/songs; bigger rewards for harder remakes
- **album unlock progression**: cover art reveals as your remake improves
- **provenance view (most important)**: tap any sound to reveal the original source clip and transformation chain
- **collectible sample cards**: each transformed sound becomes a shareable card showing original video, transformation chain, creator, and output; rarity tiers (common/rare/epic/legendary) based on transformation complexity

#### Additions Based on Feedback

- **reminder system**: keep project mates engaged with notifications and prompts _(added in response to: "How can users keep their project mates on top of it?")_
- **music-taste-based friend discovery**: connect with music-lovers who share similar tastes, even if you're not friends yet _(added in response to: "Is it possible for music-lovers who aren't friends to connect with each other?")_
- **commenting system**: admire and discuss creativity in remakes; celebrate the unique approaches different groups take _(added in response to: "Why should they listen to the remake if the sounds that are being mimicked are going to sound very similar?")_

### Aesthetics (Look)

- [untitled]-inspired minimalism—lowercases everywhere, clean spacing, calm hierarchy; recording + making should feel unblocked (few controls, obvious defaults, no clutter)
