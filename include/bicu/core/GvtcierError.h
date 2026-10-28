#pragma once
#include "bicu/core/export.h"
#include <string>

namespace GvtcierError {

enum class Type {
    None,
    Syntax,
    Lexical,
    Argument,
    Runtime,
    Engine,
    Gvts,
    Language,
};

GVTCIER_API const char* typeName(Type t);

GVTCIER_API std::string format(const char* file, int line, const char* source, Type t, const std::string& msg);

GVTCIER_API std::string translateGvts(const char* msg);

GVTCIER_API std::string checkLanguage(const char* msg);

GVTCIER_API void warnLanguage(const char* msg);

GVTCIER_API void engine(const char* tag, const char* fmt, ...);

typedef void (*GvtsErrorSink)(const char* msg, const char* file, int line);
GVTCIER_API void setGvtsErrorSink(GvtsErrorSink fn);
GVTCIER_API void gvtsError(const char* rawMsg);
GVTCIER_API bool lastGvtsError(char* outMsg, int msgCap, char* outFile, int fileCap, int* outLine);
GVTCIER_API void clearGvtsError();

}
