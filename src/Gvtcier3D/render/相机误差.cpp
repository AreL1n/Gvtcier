#include "Gvtcier.h"

// 检查场景里是否有一个可用的相机，用来决定引擎还能不能出画面。
// 判定顺序是：
//   1. 场景里有玩家对象（ObjKind::Player）就用玩家视角，玩家自带相机，永远算可用
//   2. 没有玩家但有一个固定机位（ObjKind::Camera），且 scene.mainCamera 为空，
//      表示不指定主相机，随便哪个固定机位都能用
//   3. 指定了 mainCamera 名字，则必须真的存在同名相机对象，否则算失联
//   4. 连固定机位都没有，同样算失联
// 返回的 CameraValidate：
//   ok     为 true 表示相机可用
//   reason 为失联时给用户看的提示文案，固定是“-相机失联-”
//   hasPlayer / hasFixed 记录扫描过程中命中了哪类相机，便于上层区分原因
CameraValidate CameraValidate::Gvtcier_Camera_Check(const Scene& scene) {
    CameraValidate v;
    scene.ForEachObject([&](const Object& o, GvtId<Object>) {
        if (o.kind == ObjKind::Player) v.hasPlayer = true;
        if (o.kind == ObjKind::Camera) v.hasFixed = true;
    });
    // 玩家自带相机，优先级最高，有玩家就一定有画面
    if (v.hasPlayer) {
        v.ok = true;
        return v;
    }
    if (v.hasFixed) {
        // 没指定主相机名字，任意固定机位都可以当主相机
        if (scene.mainCamera.empty()) {
            v.ok = true;
            return v;
        }
        // 指定了主相机名字，必须按名字真实存在，否则视为这个名字对应的机位被删了
        bool found = false;
        scene.ForEachObject([&](const Object& o, GvtId<Object>) {
            if (o.kind == ObjKind::Camera && o.name == scene.mainCamera) found = true;
        });
        if (found) { v.ok = true; return v; }
        v.reason = u8"-相机失联-";
        return v;
    }
    v.reason = u8"-相机失联-";
    return v;
}
