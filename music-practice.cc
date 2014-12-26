/// \file music.cc Keyboard (piano) practice application
#include "thread.h"
#include "midi-input.h"
#include "sampler.h"
#include "asound.h"
#include "pdf-renderer.h"
#include "layout.h"
#include "interface.h"
#include "window.h"

struct GraphicsWidget : Graphics, Widget {
    GraphicsWidget(Graphics&& o) : Graphics(move(o)) {}
    vec2 sizeHint(vec2) override;
    shared<Graphics> graphics(vec2) override;
};

vec2 GraphicsWidget::sizeHint(vec2) { return bounds.max; }
shared<Graphics> GraphicsWidget::graphics(vec2 unused size /*TODO: center*/) { return shared<Graphics>((Graphics*)this); }

/// SFZ sampler and PDF renderer
struct Music {
    Folder folder {"Scores"_, home()};
    array<String> files = folder.list(Files|Sorted);
    String title;

    const uint rate = 44100;
    Thread decodeThread;
    Sampler sampler {rate, "/Samples/Maestro.sfz"_, [](uint){}, decodeThread}; // Audio mixing (consumer thread) preempts decoder running in advance (in producer thread (main thread))
    Thread audioThread{-20};
    AudioOutput audio {{&sampler, &Sampler::read32}, audioThread};
    MidiInput input {audioThread};

    array<unique<FontData>> fonts;
    unique<Scroll<HList<GraphicsWidget>>> pages;
    Window window {&pages->area(), 0};

    Music() {
        assert_(files);
        setTitle(arguments() ? arguments()[0] : files[0]);
        window.actions[DownArrow] = {this, &Music::nextTitle};
        window.actions[Return] = {this, &Music::nextTitle};
        sampler.pollEvents = {&input, &MidiInput::event}; // Ensures all events are received right before mixing
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        decodeThread.spawn();
        AudioControl("Master Playback Switch") = 1;
        AudioControl("Headphone Playback Switch") = 1;
        AudioControl("Master Playback Volume") = 100;
        audio.start(sampler.rate, Sampler::periodSize, 32, 2);
        audioThread.spawn();
     }
    ~Music() {
        decodeThread.wait(); // ~Thread
        audioThread.wait(); // ~Thread
    }

     void setTitle(string title) {
         if(endsWith(title,".pdf"_)) title=title.slice(0,title.size-4);
         this->title = copyRef(title);
         pages = unique<Scroll<HList<GraphicsWidget>>>( apply(decodePDF(readFile(title+".pdf"_, folder), fonts), [](Graphics& o) { return GraphicsWidget(move(o)); }) );
         pages->horizontal = true;
         window.widget = window.focus = &pages->area();
         window.render();
         window.setTitle(title);
     }
     void nextTitle() {
         for(size_t index: range(files.size-1)) if(startsWith(files[index], title) && !startsWith(files[index+1], title)) { setTitle(section(files[index+1],'.', 0, -2)); break; }
     }
} app;