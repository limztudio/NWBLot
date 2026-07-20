#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_MESH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshClass{
    enum Enum : u32{
        Static = 0,
        Skinned = 1,
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

[[nodiscard]] inline const MeshClassInfo* FindMeshClassInfo(const u32 meshClass){
    for(const MeshClassInfo& info : s_MeshClassInfos){
        if(info.meshClass == meshClass)
            return &info;
    }
    return nullptr;
}

[[nodiscard]] inline const MeshClassInfo* FindMeshClassInfo(const AStringView text){
    for(const MeshClassInfo& info : s_MeshClassInfos){
        if(info.text == text)
            return &info;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ValidMeshClass(const u32 meshClass){
    return FindMeshClassInfo(meshClass) != nullptr;
}

[[nodiscard]] inline bool MeshClassUsesSkinning(const u32 meshClass){
    const MeshClassInfo* info = FindMeshClassInfo(meshClass);
    return info && info->usesSkinning;
}

[[nodiscard]] inline bool MeshClassMatchesSkinPayload(const u32 meshClass, const bool hasSkin){
    return MeshClassUsesSkinning(meshClass) == hasSkin;
}

[[nodiscard]] inline AStringView MeshClassText(const u32 meshClass){
    const MeshClassInfo* info = FindMeshClassInfo(meshClass);
    if(info)
        return info->text;
    if(meshClass == MeshClass::Invalid)
        return "invalid";
    return "unknown";
}

[[nodiscard]] inline bool ParseMeshClassText(const AStringView text, u32& outMeshClass){
    const MeshClassInfo* info = FindMeshClassInfo(text);
    outMeshClass = info ? info->meshClass : MeshClass::Invalid;
    return info != nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_MESH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

