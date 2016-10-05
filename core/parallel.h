#pragma once
/// \file parallel.h
#include <pthread.h> //pthread
#include "data.h"
#include "file.h"
#include "function.h"
#include "thread.h"
#include "time.h"

struct thread {
 pthread_t pthread;
 int64 id; int64* counter; int64 stop;
 function<void(uint, uint)>* delegate;
 uint64 time = 0;
};

//extern const int maxThreadCount;
extern Semaphore jobs;
extern Semaphore results;

int threadCount();

/// Runs a loop in parallel
uint64 parallel_for(int64 start, int64 stop, function<void(uint, uint)> delegate, const int unused threadCount = ::threadCount());
#if 0
static const int maxThreadCount = 8; // 32
extern thread threads[::maxThreadCount];
generic uint64 parallel_for(int64 start, int64 stop, T delegate, const int unused threadCount) {
 if(threadCount == 1) {
  tsc time; time.start();
  for(int64 i : range(start, stop)) delegate(0, i);
  return time.cycleCount();
 } else {
#if OPENMP
  tsc time; time.start();
  omp_set_num_threads(threadCount);
  #pragma omp parallel for
  for(int i=start; i<stop; i++) delegate(omp_get_thread_num(), i);
  return time.cycleCount();
#else
  assert_(threadCount == ::threadCount());
  for(int index: range(::threadCount())) {
   threads[index].counter = &start;
   threads[index].stop = stop;
   threads[index].delegate = &delegate;
   threads[index].time = 0;
  }
  int jobCount = ::min(threadCount, int(stop-start));
  //Time time; time.start();
  jobs.release(jobCount);
  tsc time; time.start();
  results.acquire(jobCount);
  //return time.nanoseconds();
  return time.cycleCount();
  //uint64 time = 0; for(int index: range(::threadCount())) time += threads[index].time; return time;
#endif
 }
}
#endif

/// Runs a loop in parallel chunks with chunk-wise functor
template<Type F> uint64 parallel_chunk(size_t jobCount, F f, const uint threadCount = ::threadCount()) {
 if(threadCount==1) {
  tsc time; time.start();
  f(0, 0, jobCount);
  return time.cycleCount();
 }
 assert_(jobCount);
 assert_(threadCount);
 const size_t chunkSize = (jobCount+threadCount-1)/threadCount;
 assert_(chunkSize);
 const size_t chunkCount = (jobCount+chunkSize-1)/chunkSize; // Last chunk might be smaller
 assert_(chunkCount <= threadCount);
 assert_((chunkCount-1)*chunkSize < jobCount);
 assert_(jobCount <= chunkCount*chunkSize);
 return parallel_for(0, chunkCount, [&](uint id, int64 chunkIndex) {
  f(id, chunkIndex*chunkSize, min<size_t>(chunkSize, jobCount-chunkIndex*chunkSize));
 }, threadCount);
}

/// Runs a loop in parallel chunks with chunk-wise functor
template<Type F> uint64 parallel_chunk(int64 start, int64 stop, F f, const uint threadCount = ::threadCount()) {
 return parallel_chunk(stop-start, [&](uint id, int64 I0, int64 DI) {
  f(id, start+I0, DI);
 }, threadCount);
}
