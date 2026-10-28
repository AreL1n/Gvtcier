#pragma once

#ifdef WAREHOUSE_BUILD
#define WAREHOUSE_API extern "C" __declspec(dllexport)
#else
#define WAREHOUSE_API extern "C" __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

WAREHOUSE_API int wh_init(void);

WAREHOUSE_API void wh_shutdown(void);

WAREHOUSE_API int wh_compile_glsl(int stage, const char* source, int length,
                                  unsigned int** out_words, int* out_count, const char** out_log);

WAREHOUSE_API void wh_free(void* p);

#ifdef __cplusplus
}
#endif
