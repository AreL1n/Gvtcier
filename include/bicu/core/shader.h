#pragma once
#include "bicu/core/export.h"
#include <unordered_map>
#include <string>

namespace rhi {
using Program = unsigned int;
using Location = int;
GVTCIER_API void Gvtcier_Rhi_UseProgram(Program p);
}

class GVTCIER_API Shader {
public:
    Shader() = default;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    ~Shader() { Gvtcier_Shader_Destroy(); }
    void Gvtcier_Shader_Destroy();
    bool Gvtcier_Shader_LoadFromMemory(const char* vertSrc, int vertLen, const char* fragSrc, int fragLen);
    bool Gvtcier_Shader_LoadCompute(const char* src, int len);
    void Gvtcier_Shader_Use() const { rhi::Gvtcier_Rhi_UseProgram(id_); }
    unsigned int Gvtcier_Shader_Id() const { return id_; }

    void Gvtcier_Shader_SetInt(const char* name, int v) const;
    void Gvtcier_Shader_SetFloat(const char* name, float v) const;
    void Gvtcier_Shader_SetVec2(const char* name, float x, float y) const;
    void Gvtcier_Shader_SetVec3(const char* name, float x, float y, float z) const;
    void Gvtcier_Shader_SetVec4(const char* name, float x, float y, float z, float w) const;
    void Gvtcier_Shader_SetMat4(const char* name, const float* m) const;
    void Gvtcier_Shader_SetMat4(const char* name, const double* m) const;
    void Gvtcier_Shader_SetMat4Array(const char* name, const float* m, int count) const;

private:
    rhi::Location locOf(const char* name) const;
    unsigned int id_ = 0;
    mutable std::unordered_map<std::string, rhi::Location> locCache_;
};

namespace shader_guard {

GVTCIER_API void Gvtcier_Shader_ResetFailures();
GVTCIER_API void Gvtcier_Shader_NoteFailure(const char* tag);
GVTCIER_API int Gvtcier_Shader_FailureCount();
GVTCIER_API const char* Gvtcier_Shader_FirstFailure();

}
