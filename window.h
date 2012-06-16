#pragma once
#include "process.h"
#include "signal.h"
#include "image.h"
#include "map.h"
#include "x.h"
struct Widget;

extern "C" Atom XInternAtom(Display*, const char*, bool);
#define Atom(name) XInternAtom(Window::x, #name, 1)

/// Window embeds \a widget in an X11 window, displaying it and forwarding user input
struct Window : Poll {
    no_copy(Window)
    /// Initialize an X11 window for \a widget
    /// \note Windows are initially hidden, use \a show to display windows.
    /// \note size admits special values (0: screen.size, -x: widget.sizeHint + margin=-x-1), widget.sizeHint will be called from \a show
    Window(Widget* widget, const string &name=string(), const Image &icon=Image(), int2 size=int2(-1,-1));
    ~Window();
    /// Create the window
    void create();
    /// Process any incoming events
    static void processEvents();
    /// Recursively \a update the whole widget hierarchy and render
    void update();
    /// Repaint window contents. Also automatically called when any event is accepted by a widget
    void render();

    /// Show window
    void show();
    /// Hide window
    void hide();
    /// Current visibility
    bool visible = false;

    /// Move window to \a position
    void setPosition(int2 position);
    /// Resize window to \a size
    void setSize(int2 size);
    /// Toggle windowed/fullscreen mode
    void setFullscreen(bool fullscreen=true);
    /// Rename window to \a title
    void setTitle(const string& title);
    /// Set window icon to \a icon
    void setIcon(const Image& icon);
    /// Set window type (using Extended Window Manager Hints)
    void setType(Atom type);
    /// Set window override_redirect attribute
    void setOverrideRedirect(bool overrideRedirect);

    /// Register local shortcut on \a key (X11 KeySym)
    signal<>& localShortcut(const string& key);
    /// Register global shortcut on \a key (X11 KeySym)
    static signal<>& globalShortcut(const string& key);

    /// Returns if this window has keyboard input focus
    bool hasFocus();
    /// Set keyboard input focus
    void setFocus(Widget* focus);
    /// Current widget that has the keyboard input focus
    static Widget* focus;

    /// Get current text selection
    static string getSelection();

    /// Get X11 property \a name on \a window
    template<class T> static array<T> getProperty(XID window, const char* property);

    /// Set X11 property \a name to \a value
    template<class T> static void setProperty(XID window, const char* type,const char* name, const array<T>& value);

    /// X11 Event handler
    void event(pollfd);
    bool event(const XEvent& e);

    /// Connection to X11 display
    static Display* x;
    /// Windows managed by this connection
    static map<uint, Window*> windows;

    /// Shortcuts triggered when \a KeySym is pressed and the focus belongs to this window
    map<uint, signal<> > localShortcuts;
    /// Shortcuts triggered when \a KeySym is pressed
    static map<uint, signal<> > globalShortcuts;

    /// Screen depth
    static int depth;
    /// Screen X11 Visual
    static Visual* visual;
    /// Screen size
    static int2 screen;
    /// Screen mouse cursor position;
    static int2 cursor;

    /// X11 window Identifier
    XID id=0;

    int2 position=zero;
    int2 size=zero;
    string title;
    Image icon;
    Atom type=0;
    bool overrideRedirect=false;

    GC gc;
    XImage* image=0;
    XShmSegmentInfo shminfo;

    Widget* widget;
};
