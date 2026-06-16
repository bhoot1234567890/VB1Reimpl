#!/usr/bin/env python3
"""
generate_test_midi.py — produce vb1_test.mid: identical MIDI to drive BOTH the
original VB-1 (Rosetta/VST2) and the reimpl for the Phase-4 A/B diff.

Sequence: for each of the 16 programs (Program Change), play a short bass run
(E1..E3 chromatic-ish) so every preset + pitch range is exercised. Both plugins
receive this exact file -> their WAV renders are directly comparable by ab_diff.py.

No dependencies. Run: python tools/generate_test_midi.py  -> vb1_test.mid
"""
import struct

PPQ = 480

def vlq(n):                       # variable-length quantity
    out = [n & 0x7F]
    while n >> 7:
        n >>= 7; out.append((n & 0x7F) | 0x80)
    return bytes(reversed(out))

def ev(delta, *msg):              # msg = status + data bytes
    return vlq(delta) + bytes(msg)

def main():
    events, t = [], 0
    # bass range around E1(28)..E3(52): two notes per program, 1s each
    notes = [28, 40, 52]
    for prog in range(16):
        events.append(ev(t, 0xC0, prog, 0)); t = 0          # program change (delta from prev)
        for nn in notes:
            events.append(ev(t, 0x90, nn, 100)); t = PPQ    # note on, quarter note
            events.append(ev(t, 0x80, nn, 0));   t = PPQ//2 # note off (sustain tail)
    events.append(ev(t, 0xFF, 0x2F, 0x00)); t = 0           # end of track

    track_data = b"".join(events)
    track = (b"MTrk" + struct.pack(">I", len(track_data)) + track_data)

    # header: format 0, 1 track, 480 ppq
    header = (b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ))
    open("vb1_test.mid", "wb").write(header + track)
    print("wrote vb1_test.mid:", len(header + track), "bytes")

if __name__ == "__main__":
    main()
