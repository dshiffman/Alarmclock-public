"""Turn a song (MP3, FLAC, WAV...) into the clock's own alarm sound.

    python tools/make_alarm_song.py "song.mp3" --name "Good Morning" --start 0 --length 40

Writes data/alarm.pcm (16,000 samples a second, mono, 16-bit, as the clock's speaker plays) and
data/alarm.txt (the name shown in the sound list). Send them to the clock with
    pio run -e ota -t uploadfs --upload-port <clock IP>     (or -e fnk0115q over USB)

The clip fades out at the end and is followed by a second of quiet before it repeats. data/ is
not in git: the song is yours, not the project's.

Needs: pip install miniaudio numpy
"""
import argparse
import pathlib

import miniaudio
import numpy as np

RATE = 16000
MAX_SECONDS = 90  # the clock's file space is 3.3 MB; 90 s is 2.9 MB

ap = argparse.ArgumentParser()
ap.add_argument("song")
ap.add_argument("--name", required=True, help="shown in the alarm's sound list")
ap.add_argument("--start", type=float, default=0, help="seconds into the song")
ap.add_argument("--length", type=float, default=40, help="seconds to keep")
ap.add_argument("--fade", type=float, default=3, help="fade-out at the end, seconds")
ap.add_argument("--gap", type=float, default=1, help="quiet before it repeats, seconds")
a = ap.parse_args()

dec = miniaudio.decode_file(a.song, output_format=miniaudio.SampleFormat.FLOAT32, nchannels=1, sample_rate=RATE)
x = np.frombuffer(dec.samples, dtype=np.float32)
print(f"song: {len(x) / RATE:.1f} s")
s = int(a.start * RATE)
x = x[s:s + int(min(a.length, MAX_SECONDS) * RATE)].copy()

# Gentle fade in (no click) and the fade out, then the gap.
fin = int(0.02 * RATE)
x[:fin] *= np.linspace(0, 1, fin)
fout = min(int(a.fade * RATE), len(x))
x[-fout:] *= np.linspace(1, 0, fout) ** 2
x = np.concatenate([x, np.zeros(int(a.gap * RATE), np.float32)])

# Loud enough to wake to: peaks at 90 %. The alarm's own volume and fade-up work from there.
x *= 0.9 / max(1e-6, float(np.max(np.abs(x))))
pcm = (x * 32767).astype("<i2")

out = pathlib.Path(__file__).resolve().parent.parent / "data"
out.mkdir(exist_ok=True)
(out / "alarm.pcm").write_bytes(pcm.tobytes())
(out / "alarm.txt").write_text(a.name[:24], encoding="utf-8")
print(f"wrote data/alarm.pcm: {len(pcm) / RATE:.1f} s, {len(pcm) * 2 / 1e6:.2f} MB; name \"{a.name[:24]}\"")
