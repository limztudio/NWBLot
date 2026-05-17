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
        Skinned = 1,
        Invalid = Limit<u32>::s_Max,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GeometryClassInfo{
    u32 geometryClass = GeometryClass::Invalid;
    AStringView text;
    bool usesSkinning = false;
};

inline constexpr GeometryClassInfo s_GeometryClassInfos[] = {
    { GeometryClass::Static, "static", false },
    { GeometryClass::Skinned, "skinned", true },
};

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

[[nodiscard]] inline bool GeometryClassUsesSkinnedGeometryRuntime(const u32 geometryClass){
    return geometryClass == GeometryClass::Skinned;
}

[[nodiscard]] inline bool GeometryClassAcceptsSkinnedGeometryPayload(const u32 geometryClass, const bool hasPayload){
    return GeometryClassUsesSkinnedGeometryRuntime(geometryClass) || !hasPayload;
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

