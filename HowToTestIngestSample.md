sudo apt-get update  
sudo apt-get install -y ffmpeg g++  
This updates Ubuntu package listings and installs the two things the ingest tool needs: a C++ compiler to build it and FFmpeg to convert your recording into a canonical MP3.

cd ~/HTCS_Projects/crowdnoise/src  
This moves you into the folder that contains `ingest_sample.cpp`, so the build command will work.

g++ -std=c++17 -O2 -Wall -Wextra -pedantic -o ingest_sample ingest_sample.cpp  
This compiles the `ingest_sample` program into an executable file named `ingest_sample` in the current directory.

sudo apt-get install -y alsa-utils  
This installs `arecord`, a simple terminal audio recorder you can use to capture a short microphone sample.

ls -la /mnt/wslg 2>/dev/null  
This checks whether WSLg integration is present; if you see `PulseServer` here, you can record through PulseAudio from inside WSL.

sudo apt-get update  
This refreshes package listings again (safe to re-run) before installing the PulseAudio helper tools needed for microphone capture in WSL.

sudo apt-get install -y pulseaudio-utils libasound2-plugins alsa-utils  
This installs PulseAudio CLI tools (`pactl`) plus the ALSA PulseAudio plugin so `arecord` can record via `-D pulse` (and it also ensures `arecord` is installed).

pactl info  
This confirms your WSL session can talk to the PulseAudio server (you should see a server string like `unix:/mnt/wslg/PulseServer`).

pactl list short sources  
This lists available audio input sources; in WSL you’ll typically see something like `RDPSource` for the Windows microphone path.

arecord -D pulse -d 3 -f S16_LE -r 48000 -c 1 /tmp/recording.wav  
This records 3 seconds from the PulseAudio input into `/tmp/recording.wav`; you speak or make a sound for 3 seconds and then the command ends automatically.

arecord -d 3 -f S16_LE -r 48000 -c 1 /tmp/recording.wav  
This records 3 seconds from your default microphone into `/tmp/recording.wav`; you speak or make a sound for 3 seconds and then the command ends automatically.

ffmpeg -f lavfi -i "sine=frequency=440:duration=3" -ac 1 -ar 48000 /tmp/recording.wav  
This creates a 3-second test recording without using a microphone (use this if the previous recording command fails or you just want a quick ingest test).

./ingest_sample --input /tmp/recording.wav --storage-root /tmp/cn_storage --sample-id test_sample_001 > /tmp/ingest_out.json  
This runs the ingest pipeline on your recording, stores the original + canonical MP3 under `/tmp/cn_storage`, and saves the JSON result to `/tmp/ingest_out.json`.

cat /tmp/ingest_out.json  
This shows you the JSON output, including the `sample_id` and the exact paths where the original file and canonical MP3 were written.

ls -R /tmp/cn_storage/samples/test_sample_001  
This lets you manually confirm the stored files exist on disk (original file, canonical MP3, and the FFmpeg log).

ffprobe -hide_banner /tmp/cn_storage/samples/test_sample_001/canonical/canonical.mp3  
This inspects the generated canonical MP3 so you can confirm it’s there and see details like sample rate and channel count.
