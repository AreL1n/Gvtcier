#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include "bicu/core/gfile.h"

namespace gmap {

struct Vec3f { double x = 0, y = 0, z = 0; };
struct Vec2f { double u = 0, v = 0; };

enum class BrushKind { Brush, BrushEntity, FuncEntity, Trigger };
enum class LightType { Point, Spot, Dir };
enum class PointClass { Effect, Audio, Image, Text, Model, Camera, Unknown };

struct BrushDef {
    BrushKind kind = BrushKind::Brush;
    std::string name;
    std::string uid;
    std::string tag;
    Vec3f boxCenter;
    Vec3f boxSize = Vec3f{1.0f, 1.0f, 1.0f};
    std::string tex;
    int material = 0;
    bool emissive = false;
    bool specular = true;
    double uvScale = 1.0;
    std::string collider;
    bool visible = true;
    bool transparent = false;
    bool rigid = false;
    bool kinematic = false;
    bool pushable = false;
    bool physics = false;
    double mass = 1.0;
    double friction = 0.6;
    std::string script;
    std::string target;
    Vec3f move;
    double speed = 1.0;
    bool once = false;
};

struct LightDef {
    LightType type = LightType::Point;
    std::string name;
    std::string tag;
    Vec3f pos;
    Vec3f dir;
    Vec3f color{1.0, 0.95, 0.88};
    double intensity = 1.0;
    double range = 8.0;
    double angle = 35.0;
    bool shadow = true;
    std::string script;
};

struct PointEntityDef {
    PointClass cls = PointClass::Unknown;
    std::string name;
    std::string tag;
    Vec3f pos;
    Vec3f rotDeg;
    std::string asset;
    std::string text;
    double size = 1.0;
    double param0 = 1.0;
    double param1 = 1.0;
    double dir = 0.0;
    double emissive = 0.0;
    double fov = 60.0;
    bool autoplay = false;
    bool loop = true;
    double volume = 1.0;
    double soundRange = 12.0;
    double pitch = 1.0;
    std::string script;
};

struct PlayerDef {
    Vec3f pos;
    double speed = 6.0;
    double jump = 8.0;
    double fov = 60.0;
    double nearPlane = 0.05;
    double farPlane = 2000.0;
    std::string script;
};

struct ItemDef {
    std::string name;
    std::string tag;
    Vec3f pos;
    std::string model;
    std::string script;
};

struct WorldEnv {
    Vec3f sky{0.62, 0.78, 0.96};
    Vec3f ambient{1.0, 1.0, 1.0};
    double ambientIntensity = 0.5;
    Vec3f sunDir{0.5, -1.0, 0.3};
    Vec3f sunColor{1.0, 0.95, 0.88};
    bool sunEnabled = false;
    double fog = 0.004;
    double gravity = 30.0;
};

struct GMapDoc {
    WorldEnv world;
    std::vector<BrushDef> brushes;
    std::vector<LightDef> lights;
    std::vector<PointEntityDef> pointEntities;
    std::vector<PlayerDef> players;
    std::vector<ItemDef> items;

    bool load(const char* path);
    bool loadMemory(const char* data, size_t size);
};

namespace detail {

inline std::vector<std::string> splitFields(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        size_t start = i;
        while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

inline bool parseFloat(const char* s, double& out) {
    if (!s || !s[0]) return false;
    char* end = nullptr;
    out = strtod(s, &end);
    return end != s;
}

inline bool parseInt(const char* s, int& out) {
    if (!s || !s[0]) return false;
    char* end = nullptr;
    out = (int)strtol(s, &end, 10);
    return end != s;
}

inline bool parseBool(const char* s, bool& out) {
    if (!s || !s[0]) return false;
    out = (strcmp(s, "1") == 0 || strcmp(s, "真") == 0);
    return true;
}

}

inline bool GMapDoc::load(const char* path) {
    std::string content;
    if (!Gvtcier_File_GopenText(path, content)) return false;
    return loadMemory(content.data(), content.size());
}

inline bool GMapDoc::loadMemory(const char* data, size_t size) {
    if (!data || size == 0) return false;
    std::string content(data, size);

    enum Block { None, World, Brush, Light, PointEntity, Player, Item };
    Block cur = None;
    BrushDef curBrush;
    LightDef curLight;
    PointEntityDef curPoint;
    PlayerDef curPlayer;
    ItemDef curItem;
    bool inBlock = false;
    bool legacyBoxWarned = false;

    size_t pos = 0;
    while (pos <= content.size()) {
        size_t eol = content.find('\n', pos);
        std::string line = content.substr(pos, (eol == std::string::npos ? content.size() : eol) - pos);
        pos = (eol == std::string::npos) ? content.size() + 1 : eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        if (line[first] == '#') continue;
        std::string trimmed = line.substr(first);

        std::vector<std::string> tk = detail::splitFields(trimmed);
        if (tk.empty()) continue;

        if (!inBlock) {
            if (tk[0] == "世界" && tk.size() >= 2 && tk[1] == "{") { cur = World; inBlock = true; continue; }
            if (tk[0] == "画刷" && tk.size() >= 2 && tk[1] == "{") {
                curBrush = BrushDef(); curBrush.kind = BrushKind::Brush; cur = Brush; inBlock = true; continue;
            }
            if (tk[0] == "画刷实体" && tk.size() >= 2 && tk[1] == "{") {
                curBrush = BrushDef(); curBrush.kind = BrushKind::BrushEntity; cur = Brush; inBlock = true; continue;
            }
            if (tk[0] == "功能实体" && tk.size() >= 2 && tk[1] == "{") {
                curBrush = BrushDef(); curBrush.kind = BrushKind::FuncEntity; cur = Brush; inBlock = true; continue;
            }
            if (tk[0] == "触发器" && tk.size() >= 2 && tk[1] == "{") {
                curBrush = BrushDef(); curBrush.kind = BrushKind::Trigger; cur = Brush; inBlock = true; continue;
            }
            if (tk[0] == "灯光" && tk.size() >= 2 && tk[1] == "{") {
                curLight = LightDef(); cur = Light; inBlock = true; continue;
            }
            if (tk[0] == "点实体" && tk.size() >= 2 && tk[1] == "{") {
                curPoint = PointEntityDef(); cur = PointEntity; inBlock = true; continue;
            }
            if (tk[0] == "玩家" && tk.size() >= 2 && tk[1] == "{") {
                curPlayer = PlayerDef(); cur = Player; inBlock = true; continue;
            }
            if (tk[0] == "物品" && tk.size() >= 2 && tk[1] == "{") {
                curItem = ItemDef(); cur = Item; inBlock = true; continue;
            }
            continue;
        }

        if (tk[0] == "}") {
            switch (cur) {
                case World: break;
                case Brush: brushes.push_back(curBrush); break;
                case Light: lights.push_back(curLight); break;
                case PointEntity: pointEntities.push_back(curPoint); break;
                case Player: players.push_back(curPlayer); break;
                case Item: items.push_back(curItem); break;
                default: break;
            }
            inBlock = false;
            cur = None;
            continue;
        }

        const std::string& key = tk[0];
        const char* a1 = tk.size() > 1 ? tk[1].c_str() : "";
        const char* a2 = tk.size() > 2 ? tk[2].c_str() : "";
        const char* a3 = tk.size() > 3 ? tk[3].c_str() : "";
        const char* a4 = tk.size() > 4 ? tk[4].c_str() : "";
        auto setV3 = [&](Vec3f& v, const char* x, const char* y, const char* z) {
            detail::parseFloat(x, v.x);
            detail::parseFloat(y, v.y);
            detail::parseFloat(z, v.z);
        };

        switch (cur) {
            case World: {
                if (key == "天空") setV3(world.sky, a1, a2, a3);
                else if (key == "环境光") { setV3(world.ambient, a1, a2, a3); detail::parseFloat(a4, world.ambientIntensity); }
                else if (key == "太阳") { setV3(world.sunDir, a1, a2, a3); world.sunEnabled = true; }
                else if (key == "太阳颜色") setV3(world.sunColor, a1, a2, a3);
                else if (key == "雾") detail::parseFloat(a1, world.fog);
                else if (key == "重力") detail::parseFloat(a1, world.gravity);
                break;
            }
            case Brush: {
                if (key == "位置") setV3(curBrush.boxCenter, a1, a2, a3);
                else if (key == "尺寸") setV3(curBrush.boxSize, a1, a2, a3);
                else if (key == "包围盒") {
                    setV3(curBrush.boxCenter, a1, a2, a3);
                    setV3(curBrush.boxSize, a4, tk.size() > 5 ? tk[5].c_str() : "1", tk.size() > 6 ? tk[6].c_str() : "1");
                    curBrush.boxSize.x *= 2.0f;
                    curBrush.boxSize.y *= 2.0f;
                    curBrush.boxSize.z *= 2.0f;
                    if (!legacyBoxWarned) {
                        legacyBoxWarned = true;
                        printf("[gmap] 遇到旧写法「包围盒」，已按旧语义半长×2 兼容；新地图请改用「位置 x y z」与「尺寸 x y z」（尺寸为全长）\n");
                        fflush(stdout);
                    }
                }
                else if (key == "名称") curBrush.name = a1;
                else if (key == "标签") curBrush.tag = a1;
                else if (key == "ID") curBrush.uid = a1;
                else if (key == "编号") curBrush.uid = a1;
                else if (key == "贴图") curBrush.tex = a1;
                else if (key == "材质") detail::parseInt(a1, curBrush.material);
                else if (key == "自发光") detail::parseBool(a1, curBrush.emissive);
                else if (key == "反光") detail::parseBool(a1, curBrush.specular);
                else if (key == "贴图缩放") detail::parseFloat(a1, curBrush.uvScale);
                else if (key == "碰撞") curBrush.collider = a1;
                else if (key == "可见") detail::parseBool(a1, curBrush.visible);
                else if (key == "透明") detail::parseBool(a1, curBrush.transparent);
                else if (key == "刚体") detail::parseBool(a1, curBrush.rigid);
                else if (key == "运动学") detail::parseBool(a1, curBrush.kinematic);
                else if (key == "可推动") detail::parseBool(a1, curBrush.pushable);
                else if (key == "物理") detail::parseBool(a1, curBrush.physics);
                else if (key == "质量") detail::parseFloat(a1, curBrush.mass);
                else if (key == "摩擦") detail::parseFloat(a1, curBrush.friction);
                else if (key == "脚本") curBrush.script = a1;
                else if (key == "目标") curBrush.target = a1;
                else if (key == "移动") setV3(curBrush.move, a1, a2, a3);
                else if (key == "速度") detail::parseFloat(a1, curBrush.speed);
                else if (key == "单次") detail::parseBool(a1, curBrush.once);
                break;
            }
            case Light: {
                if (key == "种类") curLight.type = (strcmp(a1, "聚光") == 0) ? LightType::Spot : (strcmp(a1, "平行光") == 0) ? LightType::Dir : LightType::Point;
                else if (key == "名称") curLight.name = a1;
                else if (key == "标签") curLight.tag = a1;
                else if (key == "位置") setV3(curLight.pos, a1, a2, a3);
                else if (key == "朝向") setV3(curLight.dir, a1, a2, a3);
                else if (key == "颜色") setV3(curLight.color, a1, a2, a3);
                else if (key == "强度") detail::parseFloat(a1, curLight.intensity);
                else if (key == "范围") detail::parseFloat(a1, curLight.range);
                else if (key == "角度") detail::parseFloat(a1, curLight.angle);
                else if (key == "阴影") detail::parseBool(a1, curLight.shadow);
                else if (key == "脚本") curLight.script = a1;
                break;
            }
            case PointEntity: {
                if (key == "类别") {
                    if (strcmp(a1, "特效") == 0) curPoint.cls = PointClass::Effect;
                    else if (strcmp(a1, "音频") == 0) curPoint.cls = PointClass::Audio;
                    else if (strcmp(a1, "图片") == 0) curPoint.cls = PointClass::Image;
                    else if (strcmp(a1, "文本") == 0) curPoint.cls = PointClass::Text;
                    else if (strcmp(a1, "模型") == 0) curPoint.cls = PointClass::Model;
                    else if (strcmp(a1, "相机") == 0) curPoint.cls = PointClass::Camera;
                    else curPoint.cls = PointClass::Unknown;
                }
                else if (key == "名称") curPoint.name = a1;
                else if (key == "标签") curPoint.tag = a1;
                else if (key == "位置") setV3(curPoint.pos, a1, a2, a3);
                else if (key == "旋转") setV3(curPoint.rotDeg, a1, a2, a3);
                else if (key == "资源") curPoint.asset = a1;
                else if (key == "文本") curPoint.text = a1;
                else if (key == "尺寸") detail::parseFloat(a1, curPoint.size);
                else if (key == "参数0") detail::parseFloat(a1, curPoint.param0);
                else if (key == "参数1") detail::parseFloat(a1, curPoint.param1);
                else if (key == "朝向") detail::parseFloat(a1, curPoint.dir);
                else if (key == "自发光") detail::parseFloat(a1, curPoint.emissive);
                else if (key == "自动播放") detail::parseBool(a1, curPoint.autoplay);
                else if (key == "循环") detail::parseBool(a1, curPoint.loop);
                else if (key == "音量") detail::parseFloat(a1, curPoint.volume);
                else if (key == "声音范围") detail::parseFloat(a1, curPoint.soundRange);
                else if (key == "音调") detail::parseFloat(a1, curPoint.pitch);
                else if (key == "脚本") curPoint.script = a1;
                break;
            }
            case Player: {
                if (key == "位置") setV3(curPlayer.pos, a1, a2, a3);
                else if (key == "速度") detail::parseFloat(a1, curPlayer.speed);
                else if (key == "跳跃") detail::parseFloat(a1, curPlayer.jump);
                else if (key == "视场角") detail::parseFloat(a1, curPlayer.fov);
                else if (key == "近平面") detail::parseFloat(a1, curPlayer.nearPlane);
                else if (key == "远平面") detail::parseFloat(a1, curPlayer.farPlane);
                else if (key == "脚本") curPlayer.script = a1;
                break;
            }
            case Item: {
                if (key == "名称") curItem.name = a1;
                else if (key == "标签") curItem.tag = a1;
                else if (key == "位置") setV3(curItem.pos, a1, a2, a3);
                else if (key == "模型") curItem.model = a1;
                else if (key == "脚本") curItem.script = a1;
                break;
            }
            default: break;
        }
    }
    return true;
}

}
