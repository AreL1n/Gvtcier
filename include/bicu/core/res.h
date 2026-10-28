#pragma once
#include "bicu/core/export.h"
#ifdef __cplusplus
extern "C" {
#endif
GVTCIER_API const void* res_get(const char* name, int* outSize);
GVTCIER_API const void* res_get_shader(int id, int* outSize);
#ifdef __cplusplus
}
#endif
