// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_MESH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshClass{
    static constexpr auto s_MeshClassStaticBase = 0;
    enum Enum : u32{
        Static = s_MeshClassStaticBase,
        Skinned,
        Invalid = Limit<u32>::s_Max,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshClassInfo{
    AStringView text;
    u32 meshClass = MeshClass::Invalid;
    bool usesSkinning = false;
};

inline constexpr MeshClassInfo s_MeshClassInfos[] = {
    { "static", MeshClass::Static, false },
    { "skinned", MeshClass::Skinned, true },
};

[[nodiscard]] inline const MeshClassInfo* FindMeshClassInfo(const u32 meshClass)noexcept{
    for(const MeshClassInfo& info : s_MeshClassInfos){
        if(info.meshClass == meshClass)
            return &info;
    }
    return nullptr;
}

[[nodiscard]] inline const MeshClassInfo* FindMeshClassInfo(const AStringView text)noexcept{
    for(const MeshClassInfo& info : s_MeshClassInfos){
        if(info.text == text)
            return &info;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool MeshClassUsesSkinning(const u32 meshClass)noexcept{
    const MeshClassInfo* info = FindMeshClassInfo(meshClass);
    return info && info->usesSkinning;
}

inline constexpr AStringView s_InvalidMeshClassName = "invalid";
inline constexpr AStringView s_UnknownMeshClassName = "unknown";

[[nodiscard]] inline AStringView MeshClassText(const u32 meshClass)noexcept{
    const MeshClassInfo* info = FindMeshClassInfo(meshClass);
    if(info)
        return info->text;
    if(meshClass == MeshClass::Invalid)
        return s_InvalidMeshClassName;
    return s_UnknownMeshClassName;
}

[[nodiscard]] inline Expected<u32> ParseMeshClassText(const AStringView text)noexcept{
    const MeshClassInfo* info = FindMeshClassInfo(text);
    if(!info)
        return MakeUnexpected(Failure{});
    return info->meshClass;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_MESH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

