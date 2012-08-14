#include "core.h"

/// X Protocol
struct XError { uint8 code; uint16 seq; uint32 id; uint16 minor; int8 major; int8 pad[21]; } packed;
union XEvent {
    struct { uint8 key; uint16 seq; uint32 time,root,event,child; int16 rootX,rootY,x,y; int16 state; int8 sameScreen; } packed;
    struct { uint8 pad; uint16 seq; uint32 window; uint16 x,y,w,h,count; } packed expose; //Expose
    struct { uint8 pad; uint16 seq; uint32 event,window,above; int16 x,y,w,h,border; } packed configure; //Configure
    byte pad[31]; } packed;
i(static_assert(sizeof(XEvent)==31,"");)
struct ConnectionSetup { byte bom=0x6C, pad=0; int16 major=11,minor=0; int16 nameSize=18, dataSize=16, pad2=0; };
struct ConnectionSetupReply { int8 status,reason; int16 major,minor,additionnal; int32 release, ridBase, ridMask, motionBufferSize; int16 vendorLength, maxRequestSize; int8 numScreens, numFormats, imageByteOrder, bitmapBitOrder, bitmapScanlineUnit, bitmapScanlinePad, minKeyCode, maxKeyCode; int32 pad2; };
struct Format { uint8 depth, bitsPerPixel, scanlinePad; int32 pad; };
struct Screen { int32 root, colormap, white, black, inputMask; int16 width, height, widthMM, heightMM, minMaps, maxMaps; int32 visual;
                int8 backingStores, saveUnders, depth, numDepths; };
struct Depth { int8 depth; int16 numVisualTypes; uint32 pad; };
struct VisualType { uint32 id; uint8 class_, bpp; int16 colormapEntries; uint32 red,green,blue,pad; }; i(static_assert(sizeof(VisualType)==24,"");)
struct CreateWindow { int8 req=1, depth=32; uint16 size=12; uint32 id=0,parent=0; uint16 x=0,y=0,width,height,border=0,class_=1;
                      uint32 visual=0, mask=0x280A/*0x2800*/, backgroundPixel, borderPixel, eventMask, colormap; };
struct ChangeWindowAttribute { int8 req=2; uint16 size; uint32 window, mask; };
struct GetWindowAttributes { int8 req=3; uint16 size; uint32 window; };
struct GetWindowAttributesReply { int8 backingStore; uint16 seq; uint32 replySize, visual; int16 class_; int8 bit,win; uint32 planes, pixel; int8 saveUnder, mapIsInstalled, mapState, overrideRedirect; int colormap, allEventMask, yourEventMask; int16 nopropagate, pad; };
struct DestroyWindow { int8 req=4; uint16 size=2; uint32 id; };
struct MapWindow { int8 req=8; uint16 size=2; uint32 id;};
struct UnmapWindow { int8 req=10; uint16 size=2; uint32 id;};
struct ConfigureWindow { int8 req=12; uint16 size=5; uint32 id; uint16 mask,pad; uint32 x,y; };
struct InternAtom { int8 req=16,exists=1; uint16 size=2; int16 length, pad; };
struct InternAtomReply { byte pad1; uint16 seq; uint32 length,atom; byte pad2[20]; } packed;
struct ChangeProperty { int8 req=18,replace=0; uint16 size=6; uint32 id,property,type; uint8 format; uint32 length; };
struct CreateGC { int8 req=55,pad; uint16 size=4; uint32 context=0,window,mask=0; };
struct CopyArea { int8 req=62,pad; uint16 size=7; uint32 src,dst,gc; int16 srcX,srcY,dstX,dstY,w,h; };
struct PutImage { int8 req=72,format=2; uint16 size=6; uint32 window,context; int16 w,h,x=0,y=0; uint8 leftPad=0,depth=32; int16 pad; };
struct CreateColormap { int8 req=78,alloc=0; uint16 size=4; uint32 colormap,window,visual; };
struct QueryExtension { int8 req=98,pad; uint16 size=2, length, pad2; };
struct QueryExtensionReply { int8 pad; uint16 seq; uint32 length; uint8 present,major,firstEvent,firstError; byte pad2[20]; } packed;
struct GetKeyboardMapping { int8 req=101; uint16 size=2; uint8 keycode, count=1; int16 pad; };
struct GetKeyboardMappingReply { uint8 numKeySymsPerKeyCode; uint16 seq; uint32 length; byte pad[24]; } packed;
constexpr ref<byte> xerror[] = {""_,"Request"_,"Value"_,"Window"_,"Pixmap"_,"Atom"_,"Cursor"_,"Font"_,"Match"_,"Drawable"_,"Access"_,"Alloc"_,
                                  "Colormap"_,"GContext"_,"IDChoice"_,"Name"_,"Length"_,"Implementation"_};
constexpr ref<byte> xrequest[] = {"0"_,"CreateWindow"_,"ChangeWindowAttributes"_,"GetWindowAttributes"_,"DestroyWindow"_,"DestroySubwindows"_,"ChangeSaveSet"_,"ReparentWindow"_,"MapWindow"_,"MapSubwindows"_,"UnmapWindow"_,"UnmapSubwindows"_,"ConfigureWindow"_,"CirculateWindow"_,"GetGeometry"_,"QueryTree"_,"InternAtom"_,"GetAtomName"_,"ChangeProperty"_,"DeleteProperty"_,"GetProperty"_,"ListProperties"_,"SetSelectionOwner"_,"GetSelectionOwner"_,"ConvertSelection"_,"SendEvents"_,"GrabPointer"_,"UngrabPointer"_,"GrabButton"_,"UngrabButton"_,"ChangeActivePointerGrab"_,"GrabKeyboard"_,"UngrabKeyboard"_,"GrabKey"_,"UngrabKey"_,"AllowEvents"_,"GrabServer"_,"UngrabServer"_,"QueryPointer"_,"GetMotionEvents"_,"TranslateCoordinates"_,"WarpPointer"_,"SetInputFocus"_,"GetInputFocus"_,"QueryKeymap"_,"OpenFont"_,"CloseFont"_,"QueryFont"_,"QueryTextElements"_,"ListFonts"_,"ListFontsWithInfo"_,"SetFontPath"_,"GetFontPath"_,"CreatePixmap"_,"FreePixmap"_,"CreateGC"_,"ChangeGC"_,"CopyGC"_,"SetDashes"_,"SetClipRectangles"_,"FreeGC"_,"ClearArea"_,"CopyArea"_,"CopyPlane"_,"PolyPoint"_,"PolyLine"_,"PolySegment"_,"PolyRectange"_,"PolyArc"_,"FillPoly"_,"PolyFillRectangle"_,"PolyFillArc"_,"PutImage"_};
constexpr ref<byte> xevent[] = {"Error"_,"Reply"_,"KeyPress"_,"KeyRelease"_,"ButtonPress"_,"ButtonRelease"_,"MotionNotify"_,"EnterNotify"_,
                                "LeaveNotify"_,"FocusIn"_,"FocusOut"_,"KeymapNotify"_,"Expose"_,"GraphicsExpose"_,"NoExpose"_,"VisibilityNotify"_,
                                "CreateNotify"_,"DestroyNotify"_,"UnmapNotify"_,"MapNotify"_,"MapRequest"_,"ReparentNotify"_,"ConfigureNotify"_,
                                "ConfigureRequest"_,"GravityNotify"_,"ResizeRequest"_,"CirculateNotify"_,"CirculateRequest"_,"PropertyNotify"_,
                                "SelectionClear"_,"SelectionRequest"_,"SelectionNotify"_,"ColormapNotify "_,"ClientMessage"_};
enum { KeyPressMask=1<<0, KeyReleaseMask=1<<1, ButtonPressMask=1<<2, ButtonReleaseMask=1<<3,
       EnterWindowMask=1<<4, LeaveWindowMask=1<<5, PointerMotionMask=1<<6, ExposureMask=1<<15,
       StructureNotifyMask=1<<17, SubstructureNotifyMask=1<<19, SubstructureRedirectMask=1<<20, PropertyChangeMask=1<<22 };
enum { Button1Mask=1<<8, AnyModifier=1<<15 };
