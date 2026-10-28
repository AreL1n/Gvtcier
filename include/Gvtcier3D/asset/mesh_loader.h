#pragma once
#include "bicu/core/export.h"
#include "Gvtcier3D/render/mesh.h"
#include <string>
#include <vector>

GVTCIER_API bool loadObjMesh(const void* data, size_t size, Mesh& out);
GVTCIER_API bool loadGltfMesh(const void* data, size_t size, Mesh& out);

GVTCIER_API void finalizeVerts(std::vector<Vertex>& verts);

GVTCIER_API bool loadSkinnedMesh(const void* data, size_t size, SkinnedMesh& out);

GVTCIER_API void sampleSkinnedAnim(const SkinnedMesh& sm, int animIdx, float time, std::vector<SkinnedBonePose>& pose);

GVTCIER_API void computeSkinnedBoneMats(const SkinnedMesh& sm, const std::vector<SkinnedBonePose>& pose,
                            const Mat4& rootWorld, std::vector<float>& outMats);
