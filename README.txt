PERSON A README (Backend: Song state updates for instrument replacement)
=====================================================================
(Person A represents the work that I, Sydney will be doing to replace certain tracks in the song with originally recorded tracks by users)
(Person B is Joya, who will be taking the final, disparate tracks and combining them into one audio file)

What this piece of the app does
-------------------------------
This code is the “project state manager” for one song.

The audio-decomposition team gives us a base file called Song_Details that describes:
- the song’s timeline (length, sample rate)
- each instrument stem (where its audio file lives)
- the exact time ranges (timestamps) where that instrument plays

Users can upload ONE new audio file at a time to replace ONE instrument (ex: replace “drums”).
When a replacement happens, this code updates a SECOND file (the current state file),
so the app always knows which audio file is active for each instrument right now.

Person B reads the current state file and renders (“mixes”) all instruments into one final song.


Key files
---------
1) Song_Details (base file, immutable)
   - Provided by decomposition team
   - Never edited after it is created

2) Song_State (current state file, mutable)
   - Starts as a copy of Song_Details (plus a few extra fields like active_path + revision)
   - Updated every time a user replaces an instrument
   - This is the single source of truth for “what tracks are in the song right now”


Song_Details (base file) — minimum required fields
--------------------------------------------------
We store all timestamps in milliseconds from song start (0ms).
All songs are mono (one channel standard), so we do NOT store channels.

Example (JSON):

{
  "schema_version": 1,
  "song_id": "song123",
  "title": "example song",
  "sr": 44100,
  "song_length_ms": 32000,
  "instruments": {
    "drums": {
      "original_path": "stems/original/drums.mp3",
      "segments": [
        { "start_ms": 0, "end_ms": 1200 },
        { "start_ms": 2000, "end_ms": 2600 }
      ]
    },
    "bass": {
      "original_path": "stems/original/bass.mp3",
      "segments": [
        { "start_ms": 0, "end_ms": 32000 }
      ]
    }
  }
}


Song_State (current state file) — what Person A maintains
---------------------------------------------------------
Song_State should include everything needed for rendering, plus “what is active”.
We recommend adding:
- revision: integer, starts at 0, increments on each update
- instruments.<name>.active_path: the path of the audio file currently used for that instrument
- instruments.<name>.active_source: "original" or "user"
- history: list of replacement events (audit trail)

Example (JSON):

{
  "schema_version": 1,
  "song_id": "song123",
  "sr": 44100,
  "song_length_ms": 32000,
  "revision": 3,
  "instruments": {
    "drums": {
      "segments": [
        { "start_ms": 0, "end_ms": 1200 },
        { "start_ms": 2000, "end_ms": 2600 }
      ],
      "original_path": "stems/original/drums.mp3",
      "active_path": "uploads/user123/remade_drums_take1.mp3",
      "active_source": "user"
    },
    "bass": {
      "segments": [{ "start_ms": 0, "end_ms": 32000 }],
      "original_path": "stems/original/bass.mp3",
      "active_path": "stems/original/bass.mp3",
      "active_source": "original"
    }
  },
  "history": [
    {
      "ts": "2026-02-10T02:15:00Z",
      "instrument": "drums",
      "old_active_path": "stems/original/drums.mp3",
      "new_active_path": "uploads/user123/remade_drums_take1.mp3",
      "user_id": "user123"
    }
  ]
}


Person A responsibilities (the coding plan)
-------------------------------------------
1) Initialize state
   - Read Song_Details
   - Create Song_State (copy Song_Details + set active_path=original_path for every instrument)

2) Replace one instrument
   Inputs:
   - song_id
   - instrument_name (ex: "drums")
   - uploaded file path (ex: "uploads/user123/remade_drums_take1.mp3")
   - expected_revision (to avoid overwriting someone else’s update)

   Steps:
   - Load Song_State
   - Validate:
     - instrument exists
     - uploaded file path exists
     - (optional) uploaded audio duration matches original stem duration
   - Update:
     - instruments[instrument].active_path = uploaded path
     - instruments[instrument].active_source = "user"
     - revision += 1
     - append to history
   - Save Song_State (write atomically: write temp file then rename)

3) Keep the contract stable
   - Person B will render only using:
     - sr, song_length_ms
     - instruments[*].segments
     - instruments[*].active_path

Decisions to make (tell the team now)
-------------------------------------
1) Timestamp rules
   - start_ms/end_ms are always ms from song start (0ms)
   - end_ms should be > start_ms
   - all segments must be within song_length_ms
   - decide whether overlaps are allowed within one instrument (recommend: no)

2) Audio standards
   - choose ONE standard sample rate (sr). Example: 44100
   - mono only (already decided)
   - decide accepted upload formats (mp3 only, or mp3 + wav)

3) Path / storage format
   - Are file references local paths, URLs, or storage IDs?
   - Make it consistent across decomposition outputs and user uploads.

4) Concurrency / conflicts
   - Use revision + expected_revision so two replacements don’t overwrite each other.
   - If revision mismatch: return an error and force client to reload state.

What to communicate to Person B
-------------------------------
- “Render from Song_State, not Song_Details.”
- “Always use instruments[*].active_path as the stem for that instrument.”
- “Use instruments[*].segments as the on/off mask for that instrument.”
- “Assume all audio is mono and resample to sr from the JSON if needed.”

