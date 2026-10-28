#include "Gvtcier.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

// 粒子特效系统：管理粒子的发射、生命周期、运动与渲染数据。
// 每个特效由若干"发射器"组成，粒子沿速度加加速度运动，颜色与尺寸随生命曲线插值。
// 粒子数据在渲染时批量提交，渲染器负责实际绘制（相对相机的公告板）。
// 本文件只处理 CPU 端的模拟，不直接调用图形接口。

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static Vec3 lerpv3(const Vec3& a, const Vec3& b, float t) {
    return Vec3(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t));
}

static FxColor lerpColor(const FxColor& a, const FxColor& b, float t) {
    return { lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t), lerpf(a.a, b.a, t) };
}

// 三次贝塞尔曲线求值，粒子沿轨道运动时用它算中间位置，p0/p3 是首尾控制点
static float bezier(float p0, float p1, float p2, float p3, float t) {
    float u = 1.0f - t;
    return u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t * p3;
}

float CurveEvaluator::Gvtcier_Effect_EvalFloat(float t) const {
    if (keys.empty()) return 0.0f;
    if (keys.size() == 1) return keys[0].v0;
    if (t <= keys.front().time) return keys.front().v0;
    if (t >= keys.back().time) return keys.back().v0;
    for (size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].time) {
            float seg = (t - keys[i - 1].time) / (keys[i].time - keys[i - 1].time);
            if (keys[i].interp == InterpMode::Step) return keys[i - 1].v0;
            if (keys[i].interp == InterpMode::Bezier)
                return bezier(keys[i - 1].v0, keys[i - 1].v1, keys[i].v2, keys[i].v0, seg);
            return lerpf(keys[i - 1].v0, keys[i].v0, seg);
        }
    }
    return keys.back().v0;
}

Vec2 CurveEvaluator::Gvtcier_Effect_EvalVec2(float t) const {
    if (keys.empty()) return Vec2(0, 0);
    if (keys.size() == 1) return Vec2(keys[0].v0, keys[0].v1);
    if (t <= keys.front().time) return Vec2(keys.front().v0, keys.front().v1);
    if (t >= keys.back().time) return Vec2(keys.back().v0, keys.back().v1);
    for (size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].time) {
            float seg = (t - keys[i - 1].time) / (keys[i].time - keys[i - 1].time);
            if (keys[i].interp == InterpMode::Step)
                return Vec2(keys[i - 1].v0, keys[i - 1].v1);
            float x = lerpf(keys[i - 1].v0, keys[i].v0, seg);
            float y = lerpf(keys[i - 1].v1, keys[i].v1, seg);
            return Vec2(x, y);
        }
    }
    return Vec2(keys.back().v0, keys.back().v1);
}

Vec3 CurveEvaluator::Gvtcier_Effect_EvalVec3(float t) const {
    if (keys.empty()) return Vec3(0, 0, 0);
    if (keys.size() == 1) return Vec3(keys[0].v0, keys[0].v1, keys[0].v2);
    if (t <= keys.front().time) return Vec3(keys.front().v0, keys.front().v1, keys.front().v2);
    if (t >= keys.back().time) return Vec3(keys.back().v0, keys.back().v1, keys.back().v2);
    for (size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].time) {
            float seg = (t - keys[i - 1].time) / (keys[i].time - keys[i - 1].time);
            if (keys[i].interp == InterpMode::Step)
                return Vec3(keys[i - 1].v0, keys[i - 1].v1, keys[i - 1].v2);
            float x = lerpf(keys[i - 1].v0, keys[i].v0, seg);
            float y = lerpf(keys[i - 1].v1, keys[i].v1, seg);
            float z = lerpf(keys[i - 1].v2, keys[i].v2, seg);
            return Vec3(x, y, z);
        }
    }
    return Vec3(keys.back().v0, keys.back().v1, keys.back().v2);
}

FxColor CurveEvaluator::Gvtcier_Effect_EvalColor(float t) const {
    if (keys.empty()) return { 1, 1, 1, 1 };
    if (keys.size() == 1) return { keys[0].v0, keys[0].v1, keys[0].v2, keys[0].v3 };
    if (t <= keys.front().time)
        return { keys.front().v0, keys.front().v1, keys.front().v2, keys.front().v3 };
    if (t >= keys.back().time)
        return { keys.back().v0, keys.back().v1, keys.back().v2, keys.back().v3 };
    for (size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].time) {
            float seg = (t - keys[i - 1].time) / (keys[i].time - keys[i - 1].time);
            FxColor a = { keys[i - 1].v0, keys[i - 1].v1, keys[i - 1].v2, keys[i - 1].v3 };
            FxColor b = { keys[i].v0, keys[i].v1, keys[i].v2, keys[i].v3 };
            if (keys[i].interp == InterpMode::Step) return a;
            return lerpColor(a, b, seg);
        }
    }
    return { keys.back().v0, keys.back().v1, keys.back().v2, keys.back().v3 };
}

void CurveEvaluator::Gvtcier_Effect_SetDefault(float v) {
    keys.clear();
    Keyframe k;
    k.time = 0.0f;
    k.v0 = v;
    keys.push_back(k);
}

void CurveEvaluator::Gvtcier_Effect_SetDefault(const Vec3& v) {
    keys.clear();
    Keyframe k;
    k.time = 0.0f;
    k.v0 = v.x;
    k.v1 = v.y;
    k.v2 = v.z;
    keys.push_back(k);
}

void CurveEvaluator::Gvtcier_Effect_SetDefault(const FxColor& v) {
    keys.clear();
    Keyframe k;
    k.time = 0.0f;
    k.v0 = v.r;
    k.v1 = v.g;
    k.v2 = v.b;
    k.v3 = v.a;
    keys.push_back(k);
}

static const char* jsonSkip(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return p;
}

static const char* jsonSkipString(const char* p) {
    if (*p != '"') return p;
    ++p;
    while (*p && *p != '"') { if (*p == '\\') ++p; ++p; }
    if (*p == '"') ++p;
    return p;
}

static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static void appendUtf8(std::string& s, unsigned cp) {
    if (cp < 0x80) {
        s += (char)cp;
    } else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
}

static std::string jsonReadString(const char*& p) {
    p = jsonSkip(p);
    if (*p != '"') return "";
    ++p;
    std::string s;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            ++p;
            switch (*p) {
            case 'n': s += '\n'; break;
            case 't': s += '\t'; break;
            case 'r': s += '\r'; break;
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case 'u': {
                unsigned cp = 0;
                for (int k = 0; k < 4 && p[1]; ++k) {
                    ++p;
                    cp = cp * 16u + (unsigned)hexDigit(*p);
                }
                appendUtf8(s, cp);
                break;
            }
            default: s += *p; break;
            }
            ++p;
        } else {
            s += *p;
            ++p;
        }
    }
    if (*p == '"') ++p;
    return s;
}

static double jsonReadNumber(const char*& p) {
    p = jsonSkip(p);
    char* end = nullptr;
    double v = std::strtod(p, &end);
    p = end;
    return v;
}

static bool jsonExpect(const char*& p, char c) {
    p = jsonSkip(p);
    if (*p == c) { ++p; return true; }
    return false;
}

static void jsonSkipValue(const char*& p);

static const char* jsonFindKey(const char* p, const char* key) {
    size_t klen = strlen(key);
    while (*p) {
        p = jsonSkip(p);
        if (*p == '{' || *p == '[') { ++p; continue; }
        if (*p != '"') return nullptr;
        ++p;
        const char* start = p;
        while (*p && *p != '"') ++p;
        if ((size_t)(p - start) == klen && memcmp(start, key, klen) == 0) {
            ++p;
            p = jsonSkip(p);
            if (*p == ':') { ++p; return p; }
        }
        while (*p && *p != '"') ++p;
        if (*p == '"') ++p;
        p = jsonSkip(p);
        if (*p == ':') {
            ++p;
            jsonSkipValue(p);
        }
        while (*p && *p != ',' && *p != '}' && *p != ']') ++p;
        if (*p == ',') ++p;
    }
    return nullptr;
}

static void jsonSkipValue(const char*& p) {
    p = jsonSkip(p);
    if (*p == '"') { jsonSkipString(p); return; }
    if (*p == '{' || *p == '[') {
        char end = (*p == '{') ? '}' : ']';
        int depth = 1;
        ++p;
        while (*p && depth > 0) {
            if (*p == '{' || *p == '[') ++depth;
            else if (*p == '}' || *p == ']') --depth;
            else if (*p == '"') { p = jsonSkipString(p); continue; }
            ++p;
        }
        return;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') ++p;
}

static void parseCurve(const char* p, CurveEvaluator& curve) {
    curve.keys.clear();
    const char* typeStr = jsonFindKey(p, "type");
    if (typeStr) {
        std::string t = jsonReadString(typeStr);
        if (t == "vec2") curve.type = CurveType::Vec2;
        else if (t == "vec3") curve.type = CurveType::Vec3;
        else if (t == "color") curve.type = CurveType::Color;
        else curve.type = CurveType::Float;
    }
    const char* keysArr = jsonFindKey(p, "keys");
    if (!keysArr || *jsonSkip(keysArr) != '[') return;
    const char* q = keysArr + 1;
    while (*q) {
        q = jsonSkip(q);
        if (*q == ']') break;
        if (*q != '{') break;
        ++q;
        Keyframe kf;
        const char* tv = jsonFindKey(q, "t");
        if (tv) kf.time = (float)jsonReadNumber(tv);
        const char* v0v = jsonFindKey(q, "v0");
        if (v0v) kf.v0 = (float)jsonReadNumber(v0v);
        const char* v1v = jsonFindKey(q, "v1");
        if (v1v) kf.v1 = (float)jsonReadNumber(v1v);
        const char* v2v = jsonFindKey(q, "v2");
        if (v2v) kf.v2 = (float)jsonReadNumber(v2v);
        const char* v3v = jsonFindKey(q, "v3");
        if (v3v) kf.v3 = (float)jsonReadNumber(v3v);
        const char* iv = jsonFindKey(q, "interp");
        if (iv) {
            std::string im = jsonReadString(iv);
            if (im == "bezier") kf.interp = InterpMode::Bezier;
            else if (im == "step") kf.interp = InterpMode::Step;
            else kf.interp = InterpMode::Linear;
        }
        while (*q && *q != '}') ++q;
        if (*q == '}') ++q;
        curve.keys.push_back(kf);
        q = jsonSkip(q);
        if (*q == ',') ++q;
    }
}

static EmitterShape parseShape(const std::string& s) {
    if (s == "sphere") return EmitterShape::Sphere;
    if (s == "sphere_surface") return EmitterShape::SphereSurface;
    if (s == "box") return EmitterShape::Box;
    if (s == "cone") return EmitterShape::Cone;
    if (s == "hemisphere") return EmitterShape::Hemisphere;
    if (s == "ring") return EmitterShape::Ring;
    return EmitterShape::Point;
}

static const int kFxFormatVersion = 2;

enum FxFieldType { kFxfFloat, kFxfBool, kFxfInt, kFxfVec3, kFxfString, kFxfCurve, kFxfShape };

template <typename T>
struct FxField {
    const char* key;
    FxFieldType type;
    union Ref {
        float T::*f;
        bool T::*b;
        int T::*i;
        Vec3 T::*v;
        std::string T::*s;
        CurveEvaluator T::*c;
        EmitterShape T::*e;
    } ref;
    FxField(const char* k, float T::*m) : key(k), type(kFxfFloat) { ref.f = m; }
    FxField(const char* k, bool T::*m) : key(k), type(kFxfBool) { ref.b = m; }
    FxField(const char* k, int T::*m) : key(k), type(kFxfInt) { ref.i = m; }
    FxField(const char* k, Vec3 T::*m) : key(k), type(kFxfVec3) { ref.v = m; }
    FxField(const char* k, std::string T::*m) : key(k), type(kFxfString) { ref.s = m; }
    FxField(const char* k, CurveEvaluator T::*m) : key(k), type(kFxfCurve) { ref.c = m; }
    FxField(const char* k, EmitterShape T::*m) : key(k), type(kFxfShape) { ref.e = m; }
};

static const FxField<Emitter> kEmitterFields[] = {
    { "name", &Emitter::name },
    { "maxParticles", &Emitter::maxParticles },
    { "emissionRate", &Emitter::emissionRate },
    { "duration", &Emitter::duration },
    { "looping", &Emitter::looping },
    { "warmup", &Emitter::warmup },
    { "nodeVelocity", &Emitter::nodeVelocity },
    { "nodeGravity", &Emitter::nodeGravity },
    { "nodeColor", &Emitter::nodeColor },
    { "nodeSize", &Emitter::nodeSize },
    { "shape", &Emitter::shape },
    { "shapeSize", &Emitter::shapeSize },
    { "shapeDir", &Emitter::shapeDir },
    { "shapeConeAngle", &Emitter::shapeConeAngle },
    { "gravityDir", &Emitter::gravityDir },
    { "gravityStrength", &Emitter::gravityStrength },
};
static const int kEmitterFieldCount = (int)(sizeof(kEmitterFields) / sizeof(kEmitterFields[0]));

static const FxField<Emitter> kEmitterScriptFields[] = {
    { "onInit", &Emitter::onInitScript },
    { "onUpdate", &Emitter::onUpdateScript },
    { "onParticle", &Emitter::onParticleScript },
    { "onFinish", &Emitter::onFinishScript },
};
static const int kEmitterScriptFieldCount =
    (int)(sizeof(kEmitterScriptFields) / sizeof(kEmitterScriptFields[0]));

static const FxField<ParticleTemplate> kParticleFields[] = {
    { "lifeMin", &ParticleTemplate::lifeMin },
    { "lifeMax", &ParticleTemplate::lifeMax },
    { "sizeMin", &ParticleTemplate::sizeMin },
    { "sizeMax", &ParticleTemplate::sizeMax },
    { "speedMin", &ParticleTemplate::speedMin },
    { "speedMax", &ParticleTemplate::speedMax },
    { "gravity", &ParticleTemplate::gravity },
    { "growth", &ParticleTemplate::growth },
    { "alphaBlend", &ParticleTemplate::alphaBlend },
    { "castShadow", &ParticleTemplate::castShadow },
    { "spread", &ParticleTemplate::spread },
    { "flatness", &ParticleTemplate::flatness },
    { "linearAccelMin", &ParticleTemplate::linearAccelMin },
    { "linearAccelMax", &ParticleTemplate::linearAccelMax },
    { "radialAccelMin", &ParticleTemplate::radialAccelMin },
    { "radialAccelMax", &ParticleTemplate::radialAccelMax },
    { "tangentialAccelMin", &ParticleTemplate::tangentialAccelMin },
    { "tangentialAccelMax", &ParticleTemplate::tangentialAccelMax },
    { "dampingMin", &ParticleTemplate::dampingMin },
    { "dampingMax", &ParticleTemplate::dampingMax },
    { "orbitVelMin", &ParticleTemplate::orbitVelMin },
    { "orbitVelMax", &ParticleTemplate::orbitVelMax },
    { "angularVelMin", &ParticleTemplate::angularVelMin },
    { "angularVelMax", &ParticleTemplate::angularVelMax },
    { "dampingAsFriction", &ParticleTemplate::dampingAsFriction },
    { "turbulenceEnabled", &ParticleTemplate::turbulenceEnabled },
    { "turbulenceStrength", &ParticleTemplate::turbulenceStrength },
    { "turbulenceScale", &ParticleTemplate::turbulenceScale },
    { "turbulenceSpeed", &ParticleTemplate::turbulenceSpeed },
    { "turbulenceSpeedRandom", &ParticleTemplate::turbulenceSpeedRandom },
    { "turbulenceInfluenceMin", &ParticleTemplate::turbulenceInfluenceMin },
    { "turbulenceInfluenceMax", &ParticleTemplate::turbulenceInfluenceMax },
    { "turbulenceInitDispMin", &ParticleTemplate::turbulenceInitDispMin },
    { "turbulenceInitDispMax", &ParticleTemplate::turbulenceInitDispMax },
    { "alignYToVelocity", &ParticleTemplate::alignYToVelocity },
    { "rotateY", &ParticleTemplate::rotateY },
    { "renderKind", &ParticleTemplate::renderKind },
    { "color", &ParticleTemplate::colorCurve },
    { "sizeCurve", &ParticleTemplate::sizeCurve },
};
static const int kParticleFieldCount = (int)(sizeof(kParticleFields) / sizeof(kParticleFields[0]));

template <typename T>
static void fxLoadFields(const char* p, T& o, const FxField<T>* tbl, int count) {
    char kx[160];
    for (int i = 0; i < count; ++i) {
        const FxField<T>& fl = tbl[i];
        switch (fl.type) {
        case kFxfFloat: {
            const char* v = jsonFindKey(p, fl.key);
            if (v) o.*(fl.ref.f) = (float)jsonReadNumber(v);
            break;
        }
        case kFxfBool: {
            const char* v = jsonFindKey(p, fl.key);
            if (v) o.*(fl.ref.b) = (*jsonSkip(v) == 't');
            break;
        }
        case kFxfInt: {
            const char* v = jsonFindKey(p, fl.key);
            if (v) o.*(fl.ref.i) = (int)jsonReadNumber(v);
            break;
        }
        case kFxfVec3: {
            snprintf(kx, sizeof(kx), "%sX", fl.key);
            const char* vx = jsonFindKey(p, kx);
            if (vx) (o.*(fl.ref.v)).x = (float)jsonReadNumber(vx);
            snprintf(kx, sizeof(kx), "%sY", fl.key);
            const char* vy = jsonFindKey(p, kx);
            if (vy) (o.*(fl.ref.v)).y = (float)jsonReadNumber(vy);
            snprintf(kx, sizeof(kx), "%sZ", fl.key);
            const char* vz = jsonFindKey(p, kx);
            if (vz) (o.*(fl.ref.v)).z = (float)jsonReadNumber(vz);
            break;
        }
        case kFxfString: {
            const char* v = jsonFindKey(p, fl.key);
            if (v) o.*(fl.ref.s) = jsonReadString(v);
            break;
        }
        case kFxfShape: {
            const char* v = jsonFindKey(p, fl.key);
            if (v) o.*(fl.ref.e) = parseShape(jsonReadString(v));
            break;
        }
        case kFxfCurve: {
            const char* v = jsonFindKey(p, fl.key);
            if (v) parseCurve(v, o.*(fl.ref.c));
            break;
        }
        }
    }
}

static void fxMigrateEmitter(const char* emitterJson, Emitter& em, int fromVersion) {
    if (fromVersion >= 2) return;
    const char* pt = jsonFindKey(emitterJson, "particle");
    if (!pt) return;
    const char* dx = jsonFindKey(pt, "dirX");
    if (dx) em.shapeDir.x = (float)jsonReadNumber(dx);
    const char* dy = jsonFindKey(pt, "dirY");
    if (dy) em.shapeDir.y = (float)jsonReadNumber(dy);
    const char* dz = jsonFindKey(pt, "dirZ");
    if (dz) em.shapeDir.z = (float)jsonReadNumber(dz);
}

bool EffectAsset::Gvtcier_Effect_LoadJSON(const char* path) {
    std::vector<unsigned char> raw;
    if (!Gvtcier_Res_ReadFile(path, raw)) return false;
    if (raw.empty()) return false;
    std::vector<char> buf(raw.begin(), raw.end());
    buf.push_back('\0');
    const char* p = buf.data();
    int srcVersion = 1;
    const char* ver = jsonFindKey(p, "version");
    if (ver) srcVersion = (int)jsonReadNumber(ver);
    const char* dur = jsonFindKey(p, "maxDuration");
    if (dur) maxDuration = (float)jsonReadNumber(dur);
    const char* scr = jsonFindKey(p, "script");
    if (scr) scriptPath = jsonReadString(scr);
    emitters.clear();
    const char* emArr = jsonFindKey(p, "emitters");
    const char* emOpen = emArr ? jsonSkip(emArr) : nullptr;
    if (!emOpen || *emOpen != '[') {
        version = kFxFormatVersion;
        return true;
    }
    const char* q = emOpen + 1;
    while (*q) {
        q = jsonSkip(q);
        if (*q == ']') break;
        if (*q != '{') break;
        const char* emBody = q + 1;
        const char* emEnd = q;
        jsonSkipValue(emEnd);
        Emitter em;
        fxLoadFields(emBody, em, kEmitterFields, kEmitterFieldCount);
        const char* ptSec = jsonFindKey(emBody, "particle");
        const char* ptOpen = ptSec ? jsonSkip(ptSec) : nullptr;
        if (ptOpen && *ptOpen == '{')
            fxLoadFields(ptOpen + 1, em.particle, kParticleFields, kParticleFieldCount);
        fxLoadFields(emBody, em, kEmitterScriptFields, kEmitterScriptFieldCount);
        fxMigrateEmitter(emBody, em, srcVersion);
        emitters.push_back(em);
        q = jsonSkip(emEnd);
        if (*q == ',') ++q;
    }
    version = kFxFormatVersion;
    return true;
}

static void initEmitterRuntime(EmitterRuntime& er, const Emitter& em, const Vec3& pos) {
    er.emitterIndex = 0;
    er.age = 0.0f;
    er.emitAccum = 0.0f;
    er.finished = false;
    er.particles.clear();
    er.particles.reserve((size_t)em.maxParticles);
}

void EffectInstance::Gvtcier_Effect_Reset(GvtId<EffectAsset> assetId, const EffectAsset& a, bool lp) {
    asset = assetId;
    loop = lp;
    playing = true;
    age = 0.0f;
    emitters.clear();
    emitters.resize(a.emitters.size());
    for (size_t i = 0; i < a.emitters.size(); ++i)
        initEmitterRuntime(emitters[i], a.emitters[i], position);
}

static float frand() {
    return (float)(rand() % 10000) / 10000.0f;
}

static float fxNoiseHash(int x, int y, int z) {
    unsigned n = (unsigned)(x * 374761393 + y * 668265263 + z * 1274126177);
    n = (n ^ (n >> 13)) * 1274126177u;
    n = n ^ (n >> 16);
    return (float)(n % 100000u) / 100000.0f;
}

static float fxValueNoise(float x, float y, float z) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
    float xf = x - (float)xi, yf = y - (float)yi, zf = z - (float)zi;
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = yf * yf * (3.0f - 2.0f * yf);
    float w = zf * zf * (3.0f - 2.0f * zf);
    float c000 = fxNoiseHash(xi, yi, zi);
    float c100 = fxNoiseHash(xi + 1, yi, zi);
    float c010 = fxNoiseHash(xi, yi + 1, zi);
    float c110 = fxNoiseHash(xi + 1, yi + 1, zi);
    float c001 = fxNoiseHash(xi, yi, zi + 1);
    float c101 = fxNoiseHash(xi + 1, yi, zi + 1);
    float c011 = fxNoiseHash(xi, yi + 1, zi + 1);
    float c111 = fxNoiseHash(xi + 1, yi + 1, zi + 1);
    float x00 = c000 + (c100 - c000) * u;
    float x10 = c010 + (c110 - c010) * u;
    float x01 = c001 + (c101 - c001) * u;
    float x11 = c011 + (c111 - c011) * u;
    float y0 = x00 + (x10 - x00) * v;
    float y1 = x01 + (x11 - x01) * v;
    return y0 + (y1 - y0) * w;
}

static Vec3 fxNoise3(const Vec3& p) {
    float nx = fxValueNoise(p.x, p.y, p.z);
    float ny = fxValueNoise(p.x + 37.7f, p.y + 71.3f, p.z + 13.9f);
    float nz = fxValueNoise(p.x + 91.1f, p.y + 23.5f, p.z + 57.3f);
    return Vec3(nx * 2.0f - 1.0f, ny * 2.0f - 1.0f, nz * 2.0f - 1.0f);
}

static Vec3 curlNoise(const Vec3& p, float contrast) {
    float e = 0.001f + contrast * 0.0005f;
    Vec3 dx(e, 0.0f, 0.0f), dy(0.0f, e, 0.0f), dz(0.0f, 0.0f, e);
    Vec3 x0 = fxNoise3(p - dx), x1 = fxNoise3(p + dx);
    Vec3 y0 = fxNoise3(p - dy), y1 = fxNoise3(p + dy);
    Vec3 z0 = fxNoise3(p - dz), z1 = fxNoise3(p + dz);
    float cx = (y1.z - y0.z) - (z1.y - z0.y);
    float cy = (z1.x - z0.x) - (x1.z - x0.z);
    float cz = (x1.y - x0.y) - (y1.x - y0.x);
    Vec3 r(cx, cy, cz);
    float rl = r.length();
    return (rl < 1e-5f) ? Vec3(0.0f, 1.0f, 0.0f) : r / rl;
}

static Vec3 spreadDirection(const Vec3& base, float spreadDeg, float flatness) {
    float sr = spreadDeg * 3.14159265f / 180.0f;
    float a1 = (frand() * 2.0f - 1.0f) * sr;
    float a2 = (frand() * 2.0f - 1.0f) * sr * (1.0f - flatness);
    Vec3 dxz(std::sin(a1), 0.0f, std::cos(a1));
    Vec3 dyz(0.0f, std::sin(a2), std::cos(a2));
    dyz.z = dyz.z / std::sqrt(std::fabs(dyz.z) > 1e-4f ? std::fabs(dyz.z) : 1e-4f);
    Vec3 sd(dxz.x * dyz.z, dyz.y, dxz.z * dyz.z);
    Vec3 n = base;
    float nl = n.length();
    if (nl < 1e-4f) n = Vec3(0, 0, 1);
    else n = n / nl;
    Vec3 binormal = Vec3(0, 1, 0).cross(n);
    if (binormal.length() < 1e-4f) binormal = Vec3(0, 0, 1);
    binormal = binormal / binormal.length();
    Vec3 normal = binormal.cross(n);
    Vec3 r = binormal * sd.x + normal * sd.y + n * sd.z;
    float rl = r.length();
    return (rl < 1e-4f) ? n : r / rl;
}

static void shapeSpawnLocal(const Emitter& em, Vec3& pos, Vec3& vel) {
    float r1, r2, r3;
    r1 = ((float)(rand() % 1000) / 500.0f) - 1.0f;
    r2 = ((float)(rand() % 1000) / 500.0f) - 1.0f;
    r3 = ((float)(rand() % 1000) / 500.0f) - 1.0f;
    switch (em.shape) {
    case EmitterShape::Point:
        pos = Vec3(0, 0, 0);
        break;
    case EmitterShape::Sphere: {
        float s = frand() * 2.0f - 1.0f;
        float t = frand() * 6.2831853f;
        float rad = std::sqrt(1.0f - s * s);
        Vec3 surface(std::cos(t) * rad, std::sin(t) * rad, s);
        pos = surface * em.shapeSize.x * std::pow(frand(), 1.0f / 3.0f);
        break;
    }
    case EmitterShape::SphereSurface: {
        float s = frand() * 2.0f - 1.0f;
        float t = frand() * 6.2831853f;
        float rad = std::sqrt(1.0f - s * s);
        pos = Vec3(std::cos(t) * rad, std::sin(t) * rad, s) * em.shapeSize.x;
        break;
    }
    case EmitterShape::Box:
        pos = Vec3(r1 * em.shapeSize.x, r2 * em.shapeSize.y, r3 * em.shapeSize.z);
        break;
    case EmitterShape::Cone: {
        float ang = em.shapeConeAngle * 3.14159f / 180.0f;
        float cr = ((float)(rand() % 1000) / 1000.0f) * ang;
        float ca = ((float)(rand() % 6283) / 1000.0f);
        pos = Vec3(0, 0, 0);
        vel = Vec3(std::sin(cr) * std::cos(ca), std::cos(cr), std::sin(cr) * std::sin(ca));
        return;
    }
    case EmitterShape::Hemisphere: {
        float len = std::sqrt(r1 * r1 + r2 * r2 + r3 * r3);
        if (len < 1e-4f) len = 1.0f;
        pos = Vec3(r1 / len, std::fabs(r2 / len), r3 / len) * em.shapeSize.x;
        break;
    }
    case EmitterShape::Ring: {
        float a = ((float)(rand() % 6283) / 1000.0f);
        pos = Vec3(std::cos(a) * em.shapeSize.x, 0, std::sin(a) * em.shapeSize.x);
        break;
    }
    }
    float spd = em.particle.speedMin + frand() * (em.particle.speedMax - em.particle.speedMin);
    Vec3 dir = em.shapeDir;
    if (dir.length() < 1e-4f) dir = Vec3(0, 1, 0);
    vel = spreadDirection(dir, em.particle.spread, em.particle.flatness) * spd;
}

static void spawnParticlesLocal(EmitterRuntime& er, const Emitter& em, float dt) {
    er.emitAccum += em.emissionRate * dt;
    int toEmit = (int)er.emitAccum;
    er.emitAccum -= (float)toEmit;
    for (int i = 0; i < toEmit; ++i) {
        if ((int)er.particles.size() >= em.maxParticles) break;
        const ParticleTemplate& pt = em.particle;
        EffectParticle p;
        Vec3 off, vel;
        shapeSpawnLocal(em, off, vel);
        if (!em.nodeVelocity) vel = Vec3(0, 0, 0);
        p.pos = off;
        p.origin = off;
        p.vel = vel;
        float t = frand();
        p.life = p.maxLife = pt.lifeMin + t * (pt.lifeMax - pt.lifeMin);
        p.size = em.nodeSize ? (pt.sizeMin + t * (pt.sizeMax - pt.sizeMin)) : 0.15f;
        p.baseSize = p.size;
        p.growth = em.nodeSize ? pt.growth : 0.0f;
        p.gravity = em.nodeGravity ? pt.gravity : 0.0f;
        p.alphaBlend = pt.alphaBlend;
        p.linearAccel = pt.linearAccelMin + frand() * (pt.linearAccelMax - pt.linearAccelMin);
        p.radialAccel = pt.radialAccelMin + frand() * (pt.radialAccelMax - pt.radialAccelMin);
        p.tangentialAccel = pt.tangentialAccelMin + frand() * (pt.tangentialAccelMax - pt.tangentialAccelMin);
        p.damping = pt.dampingMin + frand() * (pt.dampingMax - pt.dampingMin);
        p.orbitVel = pt.orbitVelMin + frand() * (pt.orbitVelMax - pt.orbitVelMin);
        p.angularVel = pt.angularVelMin + frand() * (pt.angularVelMax - pt.angularVelMin);
        p.dampingAsFriction = pt.dampingAsFriction;
        p.angle = frand() * 6.2831853f;
        p.turbEnabled = pt.turbulenceEnabled;
        p.turbStrength = pt.turbulenceStrength;
        p.turbScale = pt.turbulenceScale;
        p.turbSpeed = pt.turbulenceSpeed;
        p.turbSpeedRandom = pt.turbulenceSpeedRandom;
        p.turbInfluence = pt.turbulenceInfluenceMin + frand() * (pt.turbulenceInfluenceMax - pt.turbulenceInfluenceMin);
        p.seed = frand() * 1000.0f;
        p.alignYToVelocity = pt.alignYToVelocity;
        p.rotateY = pt.rotateY;
        p.renderKind = pt.renderKind;
        if (em.nodeColor && pt.colorCurve.keys.size() > 0)
            p.color = pt.colorCurve.Gvtcier_Effect_EvalColor(0.0f);
        else
            p.color = { 1, 1, 1, 1 };
        er.particles.push_back(p);
    }
}

void EffectInstance::Gvtcier_Effect_Update(float dt, FxSystem& fx) {
    if (!playing) return;
    EffectAsset* a = assetStore ? assetStore->Gvtcier_Object_Get(asset) : nullptr;
    if (!a) return;
    float sdt = dt * speed;
    age += sdt;
    for (size_t ei = 0; ei < emitters.size() && ei < a->emitters.size(); ++ei) {
        EmitterRuntime& er = emitters[ei];
        const Emitter& em = a->emitters[ei];
        if (er.finished) continue;
        if (em.duration > 0.0f && er.age >= em.duration && !em.looping) {
            er.finished = true;
            continue;
        }
        spawnParticlesLocal(er, em, sdt);
        er.age += sdt;
        for (size_t pi = 0; pi < er.particles.size();) {
            EffectParticle& p = er.particles[pi];
            p.life -= sdt;
            if (p.life <= 0.0f) {
                er.particles[pi] = er.particles.back();
                er.particles.pop_back();
                continue;
            }
            const ParticleTemplate& pt = em.particle;
            Vec3 rel = p.pos - p.origin;
            Vec3 force(0.0f, -p.gravity, 0.0f);
            if (std::fabs(p.linearAccel) > 1e-6f && p.vel.length() > 1e-5f)
                force += (p.vel / p.vel.length()) * p.linearAccel;
            if (std::fabs(p.radialAccel) > 1e-6f && rel.length() > 1e-5f)
                force += (rel / rel.length()) * p.radialAccel;
            if (std::fabs(p.tangentialAccel) > 1e-6f && rel.length() > 1e-5f) {
                Vec3 gdir(0.0f, -1.0f, 0.0f);
                Vec3 cd = (rel / rel.length()).cross(gdir);
                if (cd.length() > 1e-5f) force += (cd / cd.length()) * p.tangentialAccel;
            }
            if (p.orbitVel != 0.0f && rel.length() > 1e-5f) {
                Vec3 axis = p.vel.length() > 1e-5f ? p.vel / p.vel.length() : Vec3(0.0f, 1.0f, 0.0f);
                Vec3 tang = axis.cross(rel);
                force += tang * p.orbitVel;
            }
            if (p.turbEnabled && p.turbInfluence > 1e-5f) {
                float ns = p.turbScale <= 0.0f ? 0.001f : 1.0f / p.turbScale;
                Vec3 nc = Vec3(p.pos.x * ns, p.pos.y * ns, p.pos.z * ns) +
                          Vec3(p.seed, p.seed * 0.7f, p.seed * 1.3f) +
                          Vec3(0.0f, er.age * p.turbSpeed * (1.0f + p.turbSpeedRandom), 0.0f);
                Vec3 nd = curlNoise(nc, 1.0f + p.turbStrength);
                float vm = p.vel.length();
                if (vm > 1e-5f) {
                    Vec3 tv = nd * vm * (1.0f + (1.0f - p.turbInfluence) * 0.2f);
                    p.vel = p.vel * (1.0f - p.turbInfluence) + tv * p.turbInfluence;
                }
            }
            p.vel += force * sdt;
            if (p.damping > 0.0f) {
                float v = p.vel.length();
                if (v > 1e-6f) {
                    if (p.dampingAsFriction) v -= v * v * p.damping * 0.05f * sdt;
                    else v -= p.damping * sdt;
                    if (v < 0.0f) v = 0.0f;
                    p.vel = (p.vel / p.vel.length()) * v;
                }
            }
            p.pos += p.vel * sdt;
            p.size += p.growth * sdt;
            p.angle += p.angularVel * sdt;
            float lt = 1.0f - p.life / p.maxLife;
            if (em.nodeColor && pt.colorCurve.keys.size() > 1)
                p.color = pt.colorCurve.Gvtcier_Effect_EvalColor(lt);
            if (em.nodeSize && pt.sizeCurve.keys.size() > 1)
                p.size = p.baseSize * pt.sizeCurve.Gvtcier_Effect_EvalFloat(lt);
            ++pi;
        }
        for (size_t pi = 0; pi < er.particles.size(); ++pi) {
            EffectParticle& p = er.particles[pi];
            FxParticle fp;
            fp.pos = position + p.pos;
            fp.vel = p.vel;
            fp.life = p.life;
            fp.maxLife = p.maxLife;
            fp.size = p.size;
            fp.growth = p.growth;
            fp.color = p.color;
            fp.gravity = p.gravity;
            fp.alphaBlend = p.alphaBlend;
            fp.kind = p.renderKind;
            fx.Gvtcier_Fx_Emit(fp);
        }
    }
    bool allDone = true;
    for (auto& er : emitters) {
        if (!er.finished) { allDone = false; break; }
    }
    if (allDone && !loop) { playing = false; }
}

bool EffectInstance::Gvtcier_Effect_IsAlive() const { return playing; }

void EffectInstance::Gvtcier_Effect_Stop() { playing = false; }

void EffectSystem::Gvtcier_Effect_SetAssetStore(Gvtcier_Object<EffectAsset>* store) {
    assetStore_ = store;
}

void EffectSystem::Gvtcier_Effect_Update(float dt, FxSystem& fx) {
    std::vector<GvtId<EffectInstance>> dead;
    instances_.ForEachAlive([&](EffectInstance& inst, GvtId<EffectInstance> id) {
        if (!inst.playing) { dead.push_back(id); return; }
        inst.Gvtcier_Effect_Update(dt, fx);
    });
    for (GvtId<EffectInstance> id : dead) instances_.Gvtcier_Object_Destroy(id);
}

void EffectSystem::Gvtcier_Effect_Clear() { instances_.Gvtcier_Object_Clear(); }

GvtId<EffectInstance> EffectSystem::Gvtcier_Effect_Play(GvtId<EffectAsset> assetId, const Vec3& pos,
                                   bool loop, GvtId<Object> bindObj) {
    EffectAsset* asset = assetStore_ ? assetStore_->Gvtcier_Object_Get(assetId) : nullptr;
    if (!asset) return GvtId<EffectInstance>{};
    if ((int)instances_.Gvtcier_Object_Count() >= kMaxInstances) return GvtId<EffectInstance>{};
    EffectInstance inst;
    inst.asset = assetId;
    inst.assetStore = assetStore_;
    inst.position = pos;
    inst.loop = loop;
    inst.playing = true;
    inst.boundObject = bindObj;
    inst.id = ++nextId_;
    inst.emitters.resize(asset->emitters.size());
    for (size_t i = 0; i < asset->emitters.size(); ++i)
        initEmitterRuntime(inst.emitters[i], asset->emitters[i], pos);
    return instances_.Gvtcier_Object_Register(std::move(inst));
}

void EffectSystem::Gvtcier_Effect_Stop(const std::string& name) {
    instances_.ForEachAlive([&](EffectInstance& inst, GvtId<EffectInstance>) {
        if (inst.name == name) inst.playing = false;
    });
}

void EffectSystem::Gvtcier_Effect_StopAll() {
    instances_.ForEachAlive([](EffectInstance& inst, GvtId<EffectInstance>) {
        inst.playing = false;
    });
}

GvtId<EffectInstance> EffectSystem::Gvtcier_Effect_FindById(uint64_t id) {
    if (id == 0) return GvtId<EffectInstance>{};
    GvtId<EffectInstance> found;
    bool hit = false;
    instances_.ForEachAlive([&](EffectInstance& inst, GvtId<EffectInstance> gid) {
        if (!hit && inst.id == id) { found = gid; hit = true; }
    });
    return found;
}

bool EffectSystem::Gvtcier_Effect_StopById(uint64_t id) {
    GvtId<EffectInstance> gid = Gvtcier_Effect_FindById(id);
    EffectInstance* inst = instances_.Gvtcier_Object_Get(gid);
    if (!inst) return false;
    inst->playing = false;
    return true;
}

bool EffectSystem::Gvtcier_Effect_SetPositionById(uint64_t id, const Vec3& pos) {
    GvtId<EffectInstance> gid = Gvtcier_Effect_FindById(id);
    EffectInstance* inst = instances_.Gvtcier_Object_Get(gid);
    if (!inst) return false;
    inst->position = pos;
    return true;
}

bool EffectSystem::Gvtcier_Effect_IsPlaying(const std::string& name) const {
    bool found = false;
    instances_.ForEachAlive([&](const EffectInstance& inst, GvtId<EffectInstance>) {
        if (inst.name == name && inst.playing) found = true;
    });
    return found;
}

GvtId<EffectInstance> EffectSystem::Gvtcier_Effect_FindByObject(GvtId<Object> obj) {
    if (!obj.valid()) return GvtId<EffectInstance>{};
    GvtId<EffectInstance> found;
    bool hit = false;
    instances_.ForEachAlive([&](EffectInstance& inst, GvtId<EffectInstance> gid) {
        if (!hit && inst.boundObject == obj && inst.playing) { found = gid; hit = true; }
    });
    return found;
}
