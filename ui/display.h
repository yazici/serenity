#pragma once
/// \file display.h
#include "thread.h"
#include "function.h" // onEvent
#include "map.h" // actions
#include "vector.h" // int2

inline string padding(size_t size, uint width=4){ return "\0\0\0\0"_.slice(0, align(width, size)-size); }
generic auto pad(T&& t, uint width=4) -> decltype(t+padding(t.size, width)) { return move(t)+padding(t.size, width); }

/// Connection to an X display server
struct Display : Socket, Poll {
// Connection
    /// Synchronizes access to connection and event queue
    Lock lock;
    /// Event queue
    array<array<byte>> events;
    /// Signals events
    signal<const ref<byte>> onEvent;
    // Write
     uint16 sequence = 0;

// Server
     /// Base resource id
	 uint id = 0;
// Display
	 /// Root window
	 uint root = 0;
	 /// Root visual
	 uint visual = 0;
	 /// Screen size
	 int2 size = 0;

// Keyboard
    /// Keycode range
    uint8 minKeyCode=8, maxKeyCode=0xFF;

// Methods
	Display(bool GL, Thread& thread=mainThread);
// Connection
    // Read
     /// Event handler
     void event() override;
     /// Processes global events and dispatches signal
     void event(const ref<byte>);
    // Write
	 uint16 send(ref<byte> data, int fd=-1);
	 template<Type Request> uint16 send(Request request, const ref<byte> data, int fd=-1) {
         assert_(sizeof(request)%4==0 && sizeof(request) + align(4, data.size) == request.size*4, sizeof(request), data.size, request.size*4);
		 return send(ref<byte>(data?raw(request)+pad(data):raw(request)), fd);
     }
	 template<Type Request> uint16 send(Request request, int fd=-1) { return send(request, {}, fd); }

     /// Reads reply checking for errors and queueing events
	 array<byte> readReply(uint16 sequence, uint elementSize, buffer<int>& fds);

	 template<Type Request, Type T> typename Request::Reply request(Request request, buffer<T>& output, buffer<int>& fds, const ref<byte> data={}, int fd=-1) {
         static_assert(sizeof(typename Request::Reply)==31,"");
         Locker lock(this->lock); // Prevents a concurrent thread from reading the reply and lock event queue
		 uint16 sequence = send(request, data, fd);
		 array<byte> replyData = readReply(sequence, sizeof(T), fds);
         typename Request::Reply reply = *(typename Request::Reply*)replyData.data;
         assert_(replyData.size == sizeof(typename Request::Reply)+reply.size*sizeof(T));
		 output = copyRef(cast<T>(replyData.slice(sizeof(reply), reply.size*sizeof(T))));
         return reply;
     }

	 template<Type Request, Type T> typename Request::Reply request(Request request, buffer<T>& output, const ref<byte> data={}, int fd=-1) {
		 buffer<int> fds;
		 typename Request::Reply reply = this->request(request, output, fds, data, fd);
		 assert_(/*reply.fdCount==0 &&*/ fds.size == 0);
		 return reply;
	 }

	 template<Type Request> typename Request::Reply requestFD(Request request, buffer<int>& fds, const ref<byte> data={}) {
		 buffer<byte> output;
		 typename Request::Reply reply = this->request(request, output, fds, data);
		 assert_(reply.size == 0 && output.size ==0, reply.size, output.size);
		 return reply;
	 }

	 template<Type Request> typename Request::Reply request(Request request, const ref<byte> data={}, int fd=-1) {
         buffer<byte> output;
		 typename Request::Reply reply = this->request(request, output, data, fd);
         assert_(reply.size == 0 && output.size ==0, reply.size, output.size);
         return reply;
     }

// Keyboard
     /// Returns KeySym for key \a code and modifier \a state
     uint keySym(uint8 code, uint8 state);
     /// Returns KeyCode for \a sym
     uint8 keyCode(uint sym);

     /// Actions triggered when a key is pressed
     map<uint, function<void()>> actions;
     /// Registers global action on \a key
     function<void()>& globalAction(uint key);

// Window
     /// Returns Atom for \a name
     uint Atom(const string name);
};
