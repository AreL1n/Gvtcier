#pragma once
#include "bicu/core/math.h"
#include <vector>

struct Vertex;

namespace GvtcierNormal {

Vec3 faceNormal(const Vec3& a, const Vec3& b, const Vec3& c);

Vec3 safeNormalize(const Vec3& v, const Vec3& fallback = Vec3(0, 1, 0));

float signedVolume(const std::vector<Vertex>& verts, const std::vector<unsigned>& indices);

bool needsWindingFlip(const std::vector<Vertex>& verts, const std::vector<unsigned>& indices);

void flipWinding(std::vector<unsigned>& indices);

void flipNormals(std::vector<Vertex>& verts);

void accumulateFaceNormals(std::vector<Vertex>& verts, const std::vector<unsigned>& indices);

void normalizeVertNormals(std::vector<Vertex>& verts);

}
