Goal: take multiple different instrumental tracks that have different start and stop times and combind them together into one instrumental track. In this one instrumental track, the instruments will come in at and stop at the times they are expected to, and no original sound of the instruments will be altered. 

One liner of everything detailed below: Break down MP3 tracks into PCM --> mix them together in PCM --> encode the PCM back to MP3 

Walkthrough of how this will be done: 

1) Decode each MP3 into raw sound numbers (aka PCM samples)

2) Pick one common format for all tracks
   --> all tracks must match sample rate and channels (mono or stero)

3) Figure out where each instrument starts
If drums start at 0 seconds and piano starts at 2 seconds, convert that time to a sample index:
startSample = startSeconds * sampleRate
Example at 44,100 Hz:
2 seconds → 2 * 44100 = 88200 samples
That tells you where on the timeline that instrument begins.

4) Create an empty “final song” buffer
You make a big array full of zeros (silence) long enough to hold the whole song. The length of the array should be as long as the latest ending track (ex: if one track lasts for 5 seconds and the other lasts for 8 seconds, this whole final array should hold up to 8 seconds)

5) Place each instrument onto the timeline by adding it in
For each track:
Start at its startSample
Add its samples into the final buffer at that position
In code terms, it’s like:
finalSong[startSample + i] += instrumentTrack[i]
This is the key idea: mixing is addition. When two instruments overlap at the same time, their sound waves add together.
If an instrument stops earlier (like drums after 5 seconds), then there are no more samples to add of that instrument after that point—so it naturally disappears (goes silent) for the rest of the mix.

6) Prevent distortion (clipping)
When you add many instruments, the total can get too loud (numbers bigger than allowed), which causes harsh distortion.
So after mixing, you check how loud the result gets and turn down the entire final song by one constant amount (a “master volume”) so it fits safely.
This does not change the timing or character of individual instruments; it just prevents the combined result from distorting.

7) Encode the final mixed PCM (Pulse Code Modulation, standard way that computers represent audio as raw numbers) back into one MP3
Finally, you take the mixed PCM buffer and run an MP3 encoder to produce combined mp3.

       Breaking down step 7 above: 
       1) Decide the MP3 settings
          Common setting stuff:
          Sample rate: 44.1 kHz (or 48 kHz)
          Channels: mono or stereo
          Bitrate mode:
          CBR (constant bitrate): predictable file size
          VBR (variable bitrate): better quality per size (common default)
      2) Feed the PCM to an MP3 encoder in chunks
        You almost never pass the entire song at once. You do it in blocks, like 1152 samples per channel or larger buffers.
        Your code loops:
        take the next chunk of PCM samples
        give it to the encoder
        encoder returns some MP3 bytes
        append those bytes to the output file
      3) Flush and close
        At the end, you tell the encoder “I’m done.” It outputs the final MP3 bytes (MP3 needs an ending flush), then you close the file.
        What library you’d use on iOS + Android
        Because you require MP3 output on-device, you typically embed an MP3 encoder library, most commonly:
        LAME (a well-known MP3 encoder library)

Languages Used: 
  --> for code to be compatiable on iOS and Android:
    - C++ for the core mixing logic (summing PCM with per-track offsets, scaling to avoid clipping).
    - Swift for the iOS wrapper (calls into the C++ core)
    - Kotlin for the Android wrapper (calls into the C++ core via JNI)
    - C/C++ MP3 encoder library (commonly LAME) to encode the final mixed PCM into MP3 on both platforms.
  
  Libraries used: 
  LAME for taking the PCM and encoding back into MP3 
