#pragma once
/// \file process.h \link Thread Threaded event loops\endlink, \link Lock synchronization\endlink, execute, process environment and arguments
#include "array.h"
#include "file.h"
#include "function.h"
#include <pthread.h>

/// Logical cores count
constexpr uint coreCount=8;

/// Original thread spawned when this process was forked, terminating this thread leader terminates the whole thread group
extern struct Thread mainThread;

/// Lock is an initially released binary semaphore which can only be released by the acquiring thread
struct Lock : handle<pthread_mutex_t> {
    Lock() { pthread_mutex_init(&pointer,0); }
    ~Lock() { pthread_mutex_destroy(&pointer); }
    /// Locks the mutex.
    inline void lock() { pthread_mutex_lock(&pointer); }
    /// Atomically lock the mutex only if unlocked.
    inline bool tryLock() { return !pthread_mutex_trylock(&pointer); }
    /// Unlocks the mutex.
    inline void unlock() { pthread_mutex_unlock(&pointer); }
};

/// Convenience class to automatically unlock a mutex
struct Locker {
    Lock& lock;
    Locker(Lock& lock):lock(lock){lock.lock();}
    ~Locker(){lock.unlock();}
};

struct Condition : handle<pthread_cond_t> {
    Condition() { pthread_cond_init(&pointer,0); }
    ~Condition(){ pthread_cond_destroy(&pointer); }
};

/// A semaphore implemented using POSIX mutex, POSIX condition variable, and a counter
struct Semaphore {
    Lock mutex;
    Condition condition;
    int64 counter;
    /// Creates a semaphore with \a count initial ressources
    Semaphore(int64 count=0) : counter(count) {}
    /// Acquires \a count ressources
    inline void acquire(int64 count) {
        while(counter<count) pthread_cond_wait(&condition,&mutex);
        __sync_sub_and_fetch(&counter,count); assert(counter>=0);
        mutex.unlock();
    }
    /// Atomically tries to acquires \a count ressources only if available
    inline bool tryAcquire(int64 count) {
        if(counter<count) return false;
        __sync_sub_and_fetch(&counter,count);
        return true;
    }
    /// Releases \a count ressources
    inline void release(int64 count) {
        __sync_add_and_fetch(&counter,count);
        pthread_cond_signal(&condition);
    }
    /// Returns available ressources \a count
    operator int() const { return counter; }
};

/// Poll is a convenient interface to participate in the event loops
struct Poll : pollfd {
    Poll(const Poll&)=delete; Poll& operator=(const Poll&)=delete;
    Thread& thread; /// Thread monitoring this pollfd
    /// Poll can be used without a file descriptor to queue jobs using \a wait, \a event will be called after all system events have been handled
    Poll(Thread& thread=mainThread):pollfd{0,0,0},thread(thread){}
    /// Creates an handle to participate in an event loop, use \a registerPoll when ready
    Poll(int fd, int events=POLLIN, Thread& thread=mainThread):pollfd{fd,(short)events,0},thread(thread){}
    ~Poll(){ unregisterPoll(); }
    /// Registers \a fd to the event loop
    void registerPoll();
    /// Unregisters \a fd from the event loop
    void unregisterPoll();
    /// Schedules an \a event call from \a thread's next poll iteration
    void queue();
    /// Callback on new poll events (or when thread is idle when triggered by \a wait)
    virtual void event() =0;
};

/// Pollable semaphore
struct EventFD : Stream {
    EventFD();
    void post(){Stream::write(raw<uint64>(1));}
    void read(){Stream::read<uint64>();}
};

/// Concurrently runs an event loop
struct Thread : array<Poll*>, EventFD, Poll {
    bool terminate=0; // Flag to cleanly terminate a thread
    array<Poll*> queue; // Poll objects queued on this thread
    array<Poll*> unregistered; // Poll objects removed while in event loop
    int priority=0; // Thread system priority
    int tid=0; // Thread system identifier
    pthread_t thread;
    Lock lock;

    Thread(int priority=0);
    ~Thread(){Poll::fd=0;/*Avoid Thread::unregistered reference in ~Poll*/}
    void setPriority(int priority);
    /// Spawns a thread running an event loop with the given \a priority
    void spawn();
    /// Processes all events on \a polls and tasks on \a queue until terminate is set
    void run();
    /// Processes one queued task
    void event();
};

struct thread { uint64 id; uint64* counter; uint64 stop; pthread_t pthread; function<void(uint, uint)>* delegate; uint64 pad[3]; };
static void* start_routine(thread* t) {
    for(;;) {
        uint64 i=__sync_fetch_and_add(t->counter,1);
        if(i>=t->stop) break;
        (*t->delegate)(t->id, i);
    }
    return 0;
}

/// Runs a loop in parallel
template<class F> void parallel(uint64 start, uint64 stop, F f) {
#if DEBUG || PROFILE
    for(uint i : range(start, stop)) f(0, i);
#else
    function<void(uint, uint)> delegate = f;
    thread threads[coreCount];
    for(uint i: range(coreCount)) {
        threads[i].id = i;
        threads[i].counter = &start;
        threads[i].stop = stop;
        threads[i].delegate = &delegate;
        pthread_create(&threads[i].pthread,0,(void*(*)(void*))start_routine,&threads[i]);
    }
    for(const thread& t: threads) { uint64 status=-1; pthread_join(t.pthread,(void**)&status); assert(status==0); }
#endif
}
template<class F> void parallel(uint stop, F f) { parallel(0,stop,f); }

/// Runs a loop in parallel chunks
template<class F> void chunk_parallel(uint totalSize, F f) {
    constexpr uint chunkCount = coreCount;
    assert(totalSize%chunkCount<chunkCount); //Last chunk will be smaller
    const uint chunkSize = totalSize/chunkCount;
    parallel(chunkCount, [&](uint id, uint chunkIndex) { f(id, chunkIndex*chunkSize, min(totalSize-chunkIndex*chunkSize, chunkSize)); });
}

/// Flags all threads to terminate as soon as they return to event loop, destroys all global objects and exits process.
void exit(int status=0);
/// Immediatly terminates the current thread
void __attribute((noreturn)) exit_thread(int status);

/// Execute binary at \a path with command line arguments \a args
/// \note if \a wait is false, Returns the PID to be used for wait
int execute(const string& path, const ref<string>& args={}, bool wait=true, const Folder& workingDirectory=currentWorkingDirectory());
/// Waits for any child process to change state
int wait();
/// Waits for process \a pid to change state
/// \note Returns immediatly if process is waitable (already terminated)
int64 wait(int pid);

/// Returns value for environment variable \a name
string getenv(const string& name, string value=""_);

/// Returns command line arguments
array<string> arguments();

/// Returns standard folders
string homePath(); //$HOME
const Folder& home(); //$HOME
const Folder& config(); //$HOME/.config
const Folder& cache(); //$HOME/.cache
