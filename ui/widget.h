#pragma once
/// \file widget.h Widget interface to compose user interfaces
#include "graphics.h"

/// User interface colors
static constexpr vec3 lightBlue (7./8, 3./4, 1./2);
static constexpr vec3 gray (3./4, 3./4, 3./4);

/// Key symbols
enum Key {
    Space=' ',
    Escape=0xff1b, Backspace=0xff08, Tab, Return=0xff0d,
    Home=0xff50, LeftArrow, UpArrow, RightArrow, DownArrow, PageUp, PageDown, End, PrintScreen=0xff61,
    Execute, Insert,
    KP_Enter=0xff8d, KP_Asterisk=0xffaa, KP_Plus, KP_Separator, KP_Minus, KP_Decimal, KP_Slash, KP_0,KP_1,KP_2,KP_3,KP_4,KP_5,KP_6,KP_7,KP_8,KP_9,
    F1=0xffbe,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,
    ShiftKey=0xffe1, ControlKey=0xffe3,
    Delete=0xffff,
    Play=0x1008ff14, Media=0x1008ff32
};
inline String str(Key key) { return str(uint(key)); }
enum Modifiers { NoModifiers=0, Shift=1<<0, Control=1<<2, Alt=1<<3, NumLock=1<<4};

/// Abstract component to compose user interfaces
struct Widget {
    Widget(){}
    default_move(Widget);
    virtual ~Widget() {}
// Contents
    virtual String title() const { return {}; }
    /// Preferred size (positive means preferred, negative means expanding (i.e benefit from extra space))
    /// \note space is first allocated to preferred widgets, then to expanding widgets.
    virtual int2 sizeHint(int2) = 0;
    /// Returns graphic elements representing this widget at the given \a size.
    virtual Graphics graphics(int2 size) = 0;

// Events
    /// Mouse event type
    enum Event { Press, Release, Motion, Enter, Leave };
    /// Mouse buttons
    enum Button { NoButton, LeftButton, MiddleButton, RightButton, WheelUp, WheelDown };
    /// Override \a mouseEvent to handle or forward user input
    /// \note \a mouseEvent is first called on the root Window#widget
    /// \return Whether the mouse event was accepted
    virtual bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) {
        (void)cursor, (void)size, (void)event, (void)button; (void)focus; return false;
    }
    /// Override \a keyPress to handle or forward user input
    /// \note \a keyPress is directly called on the current focus
    /// \return Whether the key press was accepted
    virtual bool keyPress(Key key, Modifiers modifiers) { (void)key, (void) modifiers; return false; }
    virtual bool keyRelease(Key key, Modifiers modifiers) { (void)key, (void) modifiers; return false; }
};
