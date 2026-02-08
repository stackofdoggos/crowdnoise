Goal: take multiple different instrumental tracks that have different start and stop times and combind them together into one instrumental track. In this one instrumental track, the instruments will come in at and stop at the times they are expected to, and no original sound of the instruments will be altered. 

Walkthrough of how this will be done: 

1) Decode each MP3 into raw sound numbers (samples)
2) Pick one common format for all tracks
   --> all tracks must match sample rate and channels (mono or stero)

3) Figure out where each instrument starts
If drums start at 0 seconds and piano starts at 2 seconds, convert that time to a sample index:
startSample = startSeconds * sampleRate
Example at 44,100 Hz:
2 seconds → 2 * 44100 = 88200 samples
That tells you where on the timeline that instrument begins.

4) Create an empty “final song” buffer
You make a big array full of zeros (silence) long enough to hold the whole song. The length of the array should be as long as the latest ending track

5) Place each instrument onto the timeline by adding it in
For each track:
Start at its startSample
Add its samples into the final buffer at that position
In code terms, it’s like:
finalSong[startSample + i] += instrumentTrack[i]
This is the key idea: mixing is addition. When two instruments overlap at the same time, their sound waves add together.
If an instrument stops earlier (like drums after 5 seconds), then there are no more samples to add after that point—so it naturally disappears from the mix.

6) Prevent distortion (clipping)
When you add many instruments, the total can get too loud (numbers bigger than allowed), which causes harsh distortion.
So after mixing, you check how loud the result gets and turn down the entire final song by one constant amount (a “master volume”) so it fits safely.
This does not change the timing or character of individual instruments; it just prevents the combined result from distorting.

7) Encode the final mixed PCM (Pulse Code Modulation, standard way that computers represent audio as raw numbers) back into one MP3
Finally, you take the mixed PCM buffer and run an MP3 encoder to produce combined.mp3.

Languages Used: 
  --> for code to be compatiable on iOS and Android:
    - C++ for the core mixing logic (summing PCM with per-track offsets, scaling to avoid clipping).
    - Swift for the iOS wrapper (calls into the C++ core)
    - Kotlin for the Android wrapper (calls into the C++ core via JNI)
    - C/C++ MP3 encoder library (commonly LAME) to encode the final mixed PCM into MP3 on both platforms.
