#include "music.h"
#include "xml.h"

array<Sign> parse(string document, uint& divisions) {
    array<Sign> signs;
    Element root = parseXML(document);
    map<uint, Clef> clefs; map<uint, bool> slurs; KeySignature keySignature={0}; TimeSignature timeSignature={0,0}; uint time = 0, nextTime = 0; uint measureIndex=1;
    root.xpath("score-partwise/part/measure"_, [&](const Element& m) {
        for(const Element& e: m.children) {
            if(!(e.name=="note"_ && e("chord"_))) time = nextTime; // Reverts previous advance

            if(e.name=="note"_) {
                uint staff = fromInteger(e("staff"_).text())-1;
                Duration type = Duration(ref<string>{"whole"_,"half"_,"quarter"_,"eighth"_,"16th"_}.indexOf(e("type"_).text()));
                assert_(int(type)>=0, e);
                uint duration = e("duration"_) ? fromInteger(e("duration"_).text()) : 0;
                if(e("rest"_)) {
                    {Sign sign{time, duration, staff, Sign::Rest, {}}; sign.rest={type}; signs.insertSorted(sign);}
                } else {
                    assert_(e("pitch"_)("step"_).text(), e);
                    uint octaveStep = "CDEFGAB"_.indexOf(e("pitch"_)("step"_).text()[0]);
                    int octave = fromInteger(e("pitch"_)("octave"_).text());
                    int step = (octave-4) * 7 + octaveStep;
                    Accidental accidental = Accidental(ref<string>{""_,"flat"_,"sharp"_,"natural"_}.indexOf(e("accidental"_).text()));
                    if(e("notations"_)("slur"_)) {
                        if(slurs[staff]) assert_(e("notations"_)("slur"_).attribute("type"_)=="stop"_);
                        else assert_(e("notations"_)("slur"_).attribute("type"_)=="start"_, e("notations"_)("slur"_).attribute("type"_), e);
                        slurs[staff] = !slurs[staff];
                    }
                    //signs << Sign{time, staff, Sign::Note, .note={pitch, accidental, type, e("grace"_)?true:false}};
                    {Sign sign{time, duration, staff, Sign::Note, {}};
                        sign.note={clefs.at(staff), step, accidental, type,
                                   false /*dot*/,
                                   e("notations"_)("slur"_)?true:false,
                                   e("grace"_)?true:false,
                                   e("notations"_)("articulations"_)("staccato"_)?true:false,
                                   e("notations"_)("articulations"_)("tenuto"_)?true:false,
                                   e("notations"_)("articulations"_)("accent"_)?true:false,
                                  };
                        signs.insertSorted(sign);};
                }
                nextTime = time+duration;
            }
            else if(e.name=="backup"_) {
                time -= fromInteger(e("duration"_).text());
                nextTime = time;
            }
            else if(e.name=="forward"_) {
                time += fromInteger(e("duration"_).text());
                nextTime = time;
            }
            else if(e.name=="direction"_) {
                const Element& d = e("direction-type"_);
                if(d("dynamics"_)) {
                    Loudness loudness = Loudness(ref<string>{"ppp"_,"pp"_,"p"_,"mp"_,"mf"_,"f"_,"ff"_,"fff"_}.indexOf(d("dynamics"_).children.first()->name));
                    {Sign sign{time, 0, 0, Sign::Dynamic, {}}; sign.dynamic={loudness}; signs << sign;}
                }
                else if(d("metronome"_)) {
                    Duration beatUnit = Duration(ref<string>{"whole"_,"half"_,"quarter"_,"eighth"_,"16th"_}.indexOf(d("metronome"_)("beat-unit"_).text()));
                    uint perMinute = fromInteger(d("metronome"_)("per-minute"_).text());
                    {Sign sign{time, 0, 0, Sign::Metronome, {}}; sign.metronome={beatUnit, perMinute}; signs << sign;}
                }
                else if(d("pedal"_)) {
                    Action action = Action(ref<string>{"start"_,"change"_,"stop"_}.indexOf(d("pedal"_)["type"_]));
                    if(action==Start && d("pedal"_)["line"_]!="yes"_) action=Ped;
                    {Sign sign{time, 0, 0, Sign::Pedal, {}}; sign.pedal={action}; signs << sign;}
                }
                else if(d("wedge"_)) {}
                else if(d("octave-shift"_)) {}
                else if(d("other-direction"_)) {}
                else error(e);
            }
            else if(e.name=="attributes"_) {
                if(e("divisions"_)) divisions = fromInteger(e("divisions"_).text());
                e.xpath("clef"_, [&](const Element& clef) {
                    uint staff = fromInteger(clef["number"_])-1;
                    ClefSign clefSign = ClefSign("FG"_.indexOf(clef("sign"_).text()[0]));
                    {Sign sign{time, 0, staff, Sign::Clef, {}}; sign.clef={clefSign, 0}; signs << sign;};
                    clefs[staff] = {clefSign, 0};
                });
                if(e("key"_)) {
                    keySignature.fifths = fromInteger(e("key"_)("fifths"_).text());
                    {Sign sign{time, 0, 0, Sign::KeySignature, {}}; sign.keySignature=keySignature; signs.insertSorted(sign); }
                }
                if(e("time"_)) {
                    timeSignature = {uint(fromInteger(e("time"_)("beats"_).text())), uint(fromInteger(e("time"_)("beat-type"_).text()))};
                    {Sign sign{time, 0, 0, Sign::TimeSignature, {}}; sign.timeSignature=timeSignature; signs << sign;}
                }
            }
            else if(e.name=="barline"_) {}
            else if(e.name=="print"_) {}
            else error(e);
        }
        time=nextTime;
        measureIndex++;
        {Sign sign{time, 0, 0, Sign::Measure, {}}; sign.measure.index=measureIndex; signs.insertSorted(sign);}
        {Sign sign{time, 0, 1, Sign::Measure, {}}; sign.measure.index=measureIndex; signs.insertSorted(sign);}
    });
    return signs;
}
