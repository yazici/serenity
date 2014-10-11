#pragma once
#include <pthread.h> //pthread
#include "function.h"

// -> \file algorithm.h

template<Type T, Type F> auto reduce(ref<T> values, F fold, T accumulator) {
    assert_(values);
    for(const T& e: values) accumulator = fold(accumulator, e);
    return accumulator;
}
template<Type T, Type F, size_t N> auto reduce(const T (&values)[N], F fold, T initialValue) {
    return reduce(ref<T>(values), fold, initialValue);
}

generic auto sum(ref<T> values) { return reduce(values, [](T accumulator, T value) { return accumulator + value; }, T()); }
template<Type T, size_t N> auto sum(const T (&values)[N]) { return sum(ref<T>(values)); }

generic auto min(ref<T> values) { return reduce(values, [](T accumulator, T value) { return min(accumulator, value); }, values[0]); }
template<Type T, size_t N> const T& min(const T (&a)[N]) { return min(ref<T>(a)); }

generic auto max(ref<T> values) { return reduce(values, [](T accumulator, T value) { return max(accumulator, value); }, values[0]); }
template<Type T, size_t N> const T& max(const T (&a)[N]) { return max(ref<T>(a)); }

// \file parallel.h

static constexpr uint threadCount = 4;

struct thread { uint64 id; uint64* counter; uint64 stop; pthread_t pthread; function<void(uint, uint)>* delegate; uint64 pad[3]; };
inline void* start_routine(thread* t) {
    for(;;) {
        uint64 i=__sync_fetch_and_add(t->counter,1);
        if(i>=t->stop) break;
        (*t->delegate)(t->id, i);
    }
    return 0;
}

/// Runs a loop in parallel
template<Type F> void parallel(uint64 start, uint64 stop, F f) {
#if DEBUG || PROFILE
    for(uint i : range(start, stop)) f(0, i);
#else
    function<void(uint, uint)> delegate = f;
    thread threads[threadCount];
    for(uint i: range(threadCount)) {
        threads[i].id = i;
        threads[i].counter = &start;
        threads[i].stop = stop;
        threads[i].delegate = &delegate;
        pthread_create(&threads[i].pthread,0,(void*(*)(void*))start_routine,&threads[i]);
    }
    for(const thread& t: threads) { uint64 status=-1; pthread_join(t.pthread,(void**)&status); assert(status==0); }
#endif
}
template<Type F> void parallel(uint stop, F f) { parallel(0,stop,f); }

/// Runs a loop in parallel chunks with chunk-wise functor
template<Type F> void parallel_chunk(uint64 totalSize, F f) {
    constexpr uint64 chunkCount = threadCount;
    assert(totalSize%chunkCount<chunkCount); //Last chunk might be up to chunkCount smaller
    const uint64 chunkSize = (totalSize+chunkCount-1)/chunkCount;
    parallel(chunkCount, [&](uint id, uint64 chunkIndex) { f(id, chunkIndex*chunkSize, min(chunkSize, totalSize-chunkIndex*chunkSize)); });
}

/// Runs a loop in parallel chunks with element-wise functor
template<Type F> void chunk_parallel(uint64 totalSize, F f) {
    parallel_chunk(totalSize, [&](uint id, uint64 start, uint64 size) { for(uint64 index: range(start, start+size)) f(id, index); });
}

/// Stores the application of a function to every index in a mref
template<Type T, Type Function>
void parallel_apply(mref<T> target, Function function) {
    chunk_parallel(target.size, [&](uint, size_t index) { new (&target[index]) T(function(index)); });
}

/// Stores the application of a function to every elements of a ref in a mref
template<Type T, Type Function, Type S0, Type... Ss>
void parallel_apply(mref<T> target, Function function, ref<S0> source0, ref<Ss>... sources) {
    chunk_parallel(target.size, [&](uint, size_t index) { new (&target[index]) T(function(source0[index], sources[index]...)); });
}

/// Minimum number of values to trigger parallel operations
static constexpr size_t parallelMinimum = 1<<15;

template<Type T, Type F> auto parallel_reduce(ref<T> values, F fold, T initial_value) {
    assert_(values);
    if(values.size < parallelMinimum) return reduce(values, fold, initial_value);
    else {
        float accumulators[threadCount];
        parallel_chunk(values.size, [&](uint id, size_t start, size_t size) { accumulators[id] = reduce(values.slice(start, size), fold, initial_value);});
        return reduce(accumulators, fold, initial_value);
    }
}
template<Type T, Type F> auto parallel_reduce(ref<T> values, F fold) { return parallel_reduce(values, fold, values[0]); }

// \file arithmetic.cc Parallel arithmetic operations

generic T parallel_minimum(ref<T> values) { return parallel_reduce(values, [](T accumulator, T value) { return min(accumulator, value); }); }
generic T parallel_maximum(ref<T> values) { return parallel_reduce(values, [](T accumulator, T value) { return max(accumulator, value); }); }
generic T parallel_sum(ref<T> values) { return parallel_reduce(values, [](T accumulator, T value) { return accumulator + value; }, 0.f); }

inline float mean(ref<float> values) {
    float sum = parallel_sum(values);
    return sum/values.size;
}

inline float energy(ref<float> values) {
    return parallel_reduce(values, [](float accumulator, float value) { return accumulator + value*value; }, 0.f);
}

inline void abs(mref<float> target, ref<float> source) { parallel_apply(target, [&](float v) {  return abs(v); }, source); }

inline void operator*=(mref<float> values, float factor) {
    if(values.size < parallelMinimum) values.apply(values, [&](float v) {  return factor*v; });
    else parallel_apply(values, [&](float v) {  return factor*v; }, values);
}

inline void subtract(mref<float> Y, ref<float> A, float B) {
    if(Y.size < parallelMinimum) Y.apply(A, [&](float a) {  return a-B; });
    else parallel_apply(Y, [&](float a) {  return a-B; }, A);
}

inline void subtract(mref<float> Y, ref<float> A, ref<float> B) {
    if(Y.size < parallelMinimum) Y.apply(A, B, [&](float a, float b) {  return a-b; });
    else parallel_apply(Y, [&](float a, float b) {  return a-b; }, A, B);
}

inline void operator-=(mref<float> target, float DC) { subtract(target, target, DC); }
inline void operator-=(mref<float> target, ref<float> source) { subtract(target, target, source); }

inline void div(mref<float> Y, ref<float> A, ref<float> B) {
    if(Y.size < parallelMinimum) Y.apply(A, B, [&](float a, float b) {  return a/b; });
    else parallel_apply(Y, [&](float a, float b) {  return a/b; }, A, B);
}
