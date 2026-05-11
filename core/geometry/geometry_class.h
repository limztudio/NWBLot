// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GeometryClass{
    enum Enum : u32{
        Static = 0,
        StaticDeform = 1,
        Skinned = 2,
        SkinnedDeform = 3,
        Invalid = Limit<u32>::s_Max,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GeometryClassInfo{
    u32 geometryClass = GeometryClass::Invalid;
    AStringView text;
    bool usesSkinning = false;
    bool allowsRuntimeDeform = false;
};

inline constexpr GeometryClassInfo s_GeometryClassInfos[] = {
    { GeometryClass::Static, "static", false, false },
    { GeometryClass::StaticDeform, "static_deform", false, true },
    { GeometryClass::Skinned, "skinned", true, false },
    { GeometryClass::SkinnedDeform, "skinned_deform", true, true },
};

[[nodiscard]] inline AStringView SupportedGeometryClassText(){
    return "static, static_deform, skinned, or skinned_deform";
}

[[nodiscard]] inline AStringView SupportedDeformableGeometryClassText(){
    return "static_deform, skinned, or skinned_deform";
}

[[nodiscard]] inline const GeometryClassInfo* FindGeometryClassInfo(const u32 geometryClass){
    for(const GeometryClassInfo& info : s_GeometryClassInfos){
        if(info.geometryClass == geometryClass)
            return &info;
    }
    return nullptr;
}

[[nodiscard]] inline const GeometryClassInfo* FindGeometryClassInfo(const AStringView text){
    for(const GeometryClassInfo& info : s_GeometryClassInfos){
        if(info.text == text)
            return &info;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ValidGeometryClass(const u32 geometryClass){
    return FindGeometryClassInfo(geometryClass) != nullptr;
}

[[nodiscard]] inline bool GeometryClassUsesSkinning(const u32 geometryClass){
    const GeometryClassInfo* info = FindGeometryClassInfo(geometryClass);
    return info && info->usesSkinning;
}

[[nodiscard]] inline bool GeometryClassMatchesSkinPayload(const u32 geometryClass, const bool hasSkin){
    return GeometryClassUsesSkinning(geometryClass) == hasSkin;
}

[[nodiscard]] inline bool GeometryClassAllowsRuntimeDeform(const u32 geometryClass){
    const GeometryClassInfo* info = FindGeometryClassInfo(geometryClass);
    return info && info->allowsRuntimeDeform;
}

[[nodiscard]] inline bool GeometryClassAcceptsRuntimeDeformPayload(const u32 geometryClass, const bool hasPayload){
    return GeometryClassAllowsRuntimeDeform(geometryClass) || !hasPayload;
}

[[nodiscard]] inline bool GeometryClassUsesDeformableRuntime(const u32 geometryClass){
    return geometryClass != GeometryClass::Static && ValidGeometryClass(geometryClass);
}

[[nodiscard]] inline AStringView GeometryClassText(const u32 geometryClass){
    const GeometryClassInfo* info = FindGeometryClassInfo(geometryClass);
    if(info)
        return info->text;
    if(geometryClass == GeometryClass::Invalid)
        return "invalid";
    return "unknown";
}

[[nodiscard]] inline bool ParseGeometryClassText(const AStringView text, u32& outGeometryClass){
    const GeometryClassInfo* info = FindGeometryClassInfo(text);
    outGeometryClass = info ? info->geometryClass : GeometryClass::Invalid;
    return info != nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

