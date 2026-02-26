Once songs have been decomposed into their separate instruments, we proceed to decompose each instrument into its individual notes/beats. Then, we will be able to isolate a single note for the user to recreate.

Melodic tracks should be decomposed into notes, stored in the MIDI format. Percussion tracks should be decomposed into tracks for the separate drum types.

This is done by src/basic_pitch_to_midi.py. Usage is as follows:
`python3.10 src/basic_pitch_to_midi.py resources/Instrument.mp3 --out output/trackDecomp/Instrument.mid`

If you have a different python version greater than 3.10, you can use that too. Replace resources/Instrument.mp3 with the path to your instrument mp3 file. Replace output/trackDecomp/Instrument.mid with the path to where you want the MIDI file to be saved.

We have a separate system for decomposing percussion tracks (MIDI doesn't work for things that don't have notes). We separate drums based on their frequencies.

This is done by src/isolate_drums.py. To run:
`python3.10 src/isolate_drums.py resources/drumTrack.mp3 --out-dir output/trackDecomp --export-hits`

Again, you can use a newer python version if you'd like. Replace resources/drumTrack.mp3 with the path to your drum track file. Replace output/trackDecomp with the path to the directory where you want drum track MP3s and CSVs stored. Note that this program does not take a file name, but a directory name, since it has multiple outputs. It splits the drum track into kick and hats. If you add `--export-hits`, it then finds *a single* instance of each drum beat and generates a CSV for when it appears.