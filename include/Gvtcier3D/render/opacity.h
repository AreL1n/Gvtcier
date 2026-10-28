#pragma once
#include "bicu/core/export.h"
#include "bicu/core/identity.h"
#include <string>

struct Object;

class Opacity {
public:
    static GVTCIER_API void Gvtcier_Opacity_SetObject(GvtId<Object> id, float alpha);
    static GVTCIER_API float Gvtcier_Opacity_Object(GvtId<Object> id);
    static GVTCIER_API void Gvtcier_Opacity_ClearObject(GvtId<Object> id);

    static GVTCIER_API void Gvtcier_Opacity_SetUi(const std::string& key, float alpha);
    static GVTCIER_API float Gvtcier_Opacity_Ui(const std::string& key);
    static GVTCIER_API void Gvtcier_Opacity_ClearUi(const std::string& key);

    static GVTCIER_API void Gvtcier_Opacity_SetObjectMaster(float alpha);
    static GVTCIER_API float Gvtcier_Opacity_ObjectMaster();
    static GVTCIER_API void Gvtcier_Opacity_SetUiMaster(float alpha);
    static GVTCIER_API float Gvtcier_Opacity_UiMaster();

    static GVTCIER_API void Gvtcier_Opacity_Reset();
};
