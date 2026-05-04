// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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


[[nodiscard]] inline bool ValidGeometryClass(const u32 geometryClass){
    return
        geometryClass == GeometryClass::Static
        || geometryClass == GeometryClass::StaticDeform
        || geometryClass == GeometryClass::Skinned
        || geometryClass == GeometryClass::SkinnedDeform
    ;
}

[[nodiscard]] inline bool GeometryClassUsesSkinning(const u32 geometryClass){
    return geometryClass == GeometryClass::Skinned || geometryClass == GeometryClass::SkinnedDeform;
}

[[nodiscard]] inline bool GeometryClassAllowsRuntimeDeform(const u32 geometryClass){
    return geometryClass == GeometryClass::StaticDeform || geometryClass == GeometryClass::SkinnedDeform;
}

[[nodiscard]] inline bool GeometryClassUsesDeformableRuntime(const u32 geometryClass){
    return geometryClass != GeometryClass::Static && ValidGeometryClass(geometryClass);
}

[[nodiscard]] inline AStringView GeometryClassText(const u32 geometryClass){
    switch(geometryClass){
    case GeometryClass::Static:
        return "static";
    case GeometryClass::StaticDeform:
        return "static_deform";
    case GeometryClass::Skinned:
        return "skinned";
    case GeometryClass::SkinnedDeform:
        return "skinned_deform";
    case GeometryClass::Invalid:
        return "invalid";
    default:
        return "unknown";
    }
}

[[nodiscard]] inline bool ParseGeometryClassText(const AStringView text, u32& outGeometryClass){
    if(text == "static"){
        outGeometryClass = GeometryClass::Static;
        return true;
    }
    if(text == "static_deform"){
        outGeometryClass = GeometryClass::StaticDeform;
        return true;
    }
    if(text == "skinned"){
        outGeometryClass = GeometryClass::Skinned;
        return true;
    }
    if(text == "skinned_deform"){
        outGeometryClass = GeometryClass::SkinnedDeform;
        return true;
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

