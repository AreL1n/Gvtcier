#pragma once
#include "bicu/core/export.h"

GVTCIER_API void Gvtcier_Debug_SetEnabled(bool on);
GVTCIER_API bool Gvtcier_Debug_Enabled();
GVTCIER_API void Gvtcier_Debug_InitConsole();
GVTCIER_API void Gvtcier_Debug_HideConsole();

#if GV_DEBUG
#define DBGLN(...) do { if (Gvtcier_Debug_Enabled()) { printf(__VA_ARGS__); printf("\n"); fflush(stdout); } } while (0)
#define DBGF(...) do { if (Gvtcier_Debug_Enabled()) { printf(__VA_ARGS__); fflush(stdout); } } while (0)
#else
#define DBGLN(...) ((void)0)
#define DBGF(...) ((void)0)
#endif
