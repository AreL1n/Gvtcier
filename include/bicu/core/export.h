#pragma once
#ifdef __ANDROID__
#define GVTCIER_API __attribute__((visibility("default")))
#elif defined(GVTCIER_BUILD_DLL)
#define GVTCIER_API __declspec(dllexport)
#else
#define GVTCIER_API __declspec(dllimport)
#endif
