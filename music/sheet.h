#pragma once
#include "notation.h"
#include "widget.h"
#include "font.h"

inline String str(const Note& a) { return str(a.key); }

struct Sheet : Widget {
    // Layout parameters
    const int staffCount = 2;
    const int halfLineInterval = 5, lineInterval = 2*halfLineInterval;
    const int lineWidth = 1, barWidth=1, stemWidth = 1, stemLength = 4*lineInterval, beamWidth = 6;
    const int shortStemLength = 5*halfLineInterval;
    // Layout helpers
    int clefStep(ClefSign clefSign, int step) { return step - (clefSign==Treble ? 10 : -2); } // Translates C4 step to top line step using clef
    int staffY(uint staff, int clefStep) { return staff*10*lineInterval - clefStep * halfLineInterval; } // Clef independent
    int Y(Clef clef, uint staff, int step) { return staffY(staff, clefStep(clef.clefSign, step)); } // Clef dependent
    int Y(Sign sign) { assert_(sign.type==Sign::Note); return Y(sign.note.clef, sign.staff, sign.note.step); } // Clef dependent
    int Y(const map<uint, Clef>& clefs, uint staff, int step) { return staffY(staff, clefStep(clefs.at(staff).clefSign, step)); } // Clef dependent

    // Fonts
	Font graceFont {File("emmentaler-26.otf", Folder("/usr/local/share/fonts"_)), 4.f*halfLineInterval, "Emmentaler"};
	Font font {File("emmentaler-26.otf", "/usr/local/share/fonts"_), 9.f*halfLineInterval, "Emmentaler"};
	Font textFont{File("LinLibertine_R.ttf", "/usr/share/fonts/libertine-ttf"_), 6.f*halfLineInterval, "LinLibertine_R"};
	Font smallFont{File("LinLibertine_R.ttf", "/usr/share/fonts/libertine-ttf"_), 14.f, "LinLibertine_R"};
    // Font helpers
	vec2 glyphSize(string name) { return font.metrics(font.index(name)).size; }
    int2 noteSize = int2(round(glyphSize("noteheads.s2"_)));
    float glyph(int2 position, const string name, Font& font);
    float glyph(int2 position, const string name) { return glyph(position, name, font); }

    // Graphic primitives
    array<Rect> fills;
    struct Parallelogram { int2 min,max; int dy; }; array<Parallelogram> parallelograms;
    struct Blit { int2 position; Image image; }; array<Blit> blits;
	typedef buffer<vec2> Cubic; array<Cubic> cubics;
	map<uint, bgr3f> colors; // Overrides color for Blit index

    uint text(int2 position, const string& text, Font& font, array<Blit>& blits);
    uint text(int2 position, const string& text, Font& font) { return this->text(position, text, font, blits); }

    // Layouts notations to graphic primitives
    Sheet(const ref<Sign>& signs, uint divisions, uint height);

    // MIDI Synchronization
    map<uint,array<Note>> notes; // Signs for notes (time, key, blitIndex)
    uint extraErrors = 0, missingErrors = 0, wrongErrors = 0, orderErrors = 0;
    bool synchronizationFailed = false;

    /// Synchronizes with MIDI notes and layouts additional debug output if necessary
    /// \return Returns blit index of corresponding note for each MIDI note
    buffer<uint> synchronize(const ref<uint>& midiMotes);
    int measureIndex(int x0);

    // Control
    array<int> measures; // X position of measure starts
    array<int> measureToChord; // first chord index of measure
    array<int> chordToNote; // first note index of chord
    //int position = 0;

	int2 sizeHint(int2) override { return int2(-1, staffY(1,-16)); }
	Graphics graphics(int2 size) override;
    //bool mouseEvent(int2, int2, Event, Button button);
};
