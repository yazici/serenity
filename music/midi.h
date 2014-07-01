#pragma once
/// \file midi.h Standard MIDI file player
#include "function.h"
#include "data.h"
#include "map.h"

struct MidiNote { uint time, key, velocity; };
//inline bool operator ==(const MidiNote& a, uint key) { return a.key == key; }
inline bool operator <=(const MidiNote& a, const MidiNote& b) { return a.time < b.time || (a.time == b.time && a.key <= b.key); }
//inline String str(const MidiNote& a) { return str(a.key); }

enum { NoteOff=8, NoteOn, Aftertouch, Controller, ProgramChange, ChannelAftertouch, PitchBend, Meta };
struct Track {
    uint time=0; uint8 type_channel=0; BinaryData data;
    Track(uint time, BinaryData&& data):time(time),data(move(data)){}
    uint startTime=0, startIndex=0;
    void reset() { type_channel=0; time=startTime; data.index=startIndex; }
};

/// Standard MIDI file player
struct MidiFile {
    array<Track> tracks;
    int trackCount=0;
    uint ticksPerSeconds=0;
    uint timeSignature[2] = {4,4}, tempo=60000/120; int key=0; enum {Major,Minor} scale=Major;
    array<MidiNote> notes;
    uint duration=0; // Duration in ticks
    MidiFile(const ref<byte>& data);
    void read(Track& track);
};