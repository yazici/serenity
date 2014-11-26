/// \file sheet.h
#pragma once
#include "notation.h"
#include "widget.h"
#include "font.h"

inline String str(const Note& a) { return str(a.key); }

/// Layouts musical notations to graphic primitives
struct Sheet : Widget {
    // Layout parameters
	static constexpr int staffCount = 2;
	static constexpr float halfLineInterval = 5, lineInterval = 2*halfLineInterval;
	const float lineWidth = 1, barWidth=1, stemWidth = 1, stemLength = 7*halfLineInterval, beamWidth = 6;
	const float shortStemLength = 5*halfLineInterval;
    // Layout helpers
	float staffY(uint staff, int clefStep) { return staff*10*lineInterval - clefStep * halfLineInterval; } // Clef independent
	float Y(uint staff, ClefSign clefSign, int step) { return staffY(staff, step-(clefSign==Treble ? 10 : -2)); } // Clef dependent
	/*int clefStep(Sign sign) {
		assert_(sign.type==Sign::Note);
		return sign.note.step - (sign.note.clef.clefSign==Treble ? 10 : -2) - sign.note.clef.octave*7;
	}*/
	//float Y(Sign sign, int step) { assert_(sign.type==Sign::Note||sign.type==Sign::Clef); return Y(sign.staff, clefStep(sign); } // Clef dependent
	// Translates C4 step to top line step using clef
	int clefStep(Sign sign) {
		assert_(sign.type==Sign::Note);
		return sign.note.step - (sign.note.clef.clefSign==Treble ? 10 : -2) - sign.note.clef.octave*7;
	}
	float Y(Sign sign) { assert_(sign.type==Sign::Note); return staffY(sign.staff, clefStep(sign)); } // Clef dependent
	//int Y(const map<uint, Clef>& clefs, uint staff, int step) { return staffY(staff, clefStep(clefs.at(staff).clefSign, step)); } // Clef dependent

    // Fonts
	Font graceFont {File("emmentaler-26.otf", Folder("/usr/local/share/fonts"_)), 4.f*halfLineInterval, "Emmentaler"};
	Font font {File("emmentaler-26.otf", "/usr/local/share/fonts"_), 9.f*halfLineInterval, "Emmentaler"};
	//Font textFont{File("LinLibertine_R.ttf", "/usr/share/fonts/libertine-ttf"_), 6.f*halfLineInterval, "LinLibertine_R"};
	//Font smallFont{File("LinLibertine_R.ttf", "/usr/share/fonts/libertine-ttf"_), 14.f, "LinLibertine_R"};
	float textSize = 6*halfLineInterval;
    // Font helpers
	vec2 glyphSize(string name) { return font.metrics(font.index(name)).size; }
	vec2 noteSize = glyphSize("noteheads.s2"_);

	// Graphics
	map<int64, float> measureBars; // Maps sheet time to position of measure starts
	map<Rect, shared<Graphics>> measures;
	shared<Graphics> debug;

	int highestStep = 0;
	int2 sizeHint(int2) override { return int2(measureBars.values.last(), -(staffY(1, -32)-staffY(0, highestStep))); }
	shared<Graphics> graphics(int2 size, Rect clip) override;

	// -- Control
	array<size_t> measureToChord; // First chord index of measure
	array<size_t> chordToNote; // First note index of chord

	/// Returns measure index containing position \a x
	size_t measureIndex(float x);
	int stop(int unused axis, int currentPosition, int direction) override;

	// -- MIDI Synchronization
	int64 ticksPerMinutes = 0;
	buffer<Sign> midiToSign; /// Sign of corresponding note for each MIDI note
	uint extraErrors = 0, missingErrors = 0, wrongErrors = 0, orderErrors = 0;
	size_t firstSynchronizationFailureChordIndex = -1;

	/// Layouts musical notations to graphic primitives
	Sheet(ref<Sign> signs, uint ticksPerQuarter, ref<uint> midiNotes={});
};
