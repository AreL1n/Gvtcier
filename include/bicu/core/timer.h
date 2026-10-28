#pragma once
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

inline double Gvtcier_NowSeconds() {
#ifdef _WIN32
    static LARGE_INTEGER freq = { 0 };
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
#endif
}
