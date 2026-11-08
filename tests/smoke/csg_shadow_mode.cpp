// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_shadow_mode.h"
#include "smoke_environment.h"

#include <tests/smoke/scene_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_AnalyticAtlas = NWB::Tests::Smoke::s_ShadowSmokeAnalyticAtlas;
static constexpr AStringView s_OpenAtlas = NWB::Tests::Smoke::s_ShadowSmokeOpenAtlas;
using NWB::Tests::Smoke::s_ShadowSmokeDirectional;
using NWB::Tests::Smoke::s_ShadowSmokePoint;
using NWB::Tests::Smoke::s_ShadowSmokeHard;
using NWB::Tests::Smoke::s_ShadowSmokeFinite;
static constexpr Array<AStringView, 6> s_OpenCaseNames = {
    NWB::Tests::Smoke::s_ShadowSmokeOpenCaseReference,
    NWB::Tests::Smoke::s_ShadowSmokeOpenCaseRetained,
    NWB::Tests::Smoke::s_ShadowSmokeOpenCaseClippedReference,
    NWB::Tests::Smoke::s_ShadowSmokeOpenCaseClipped,
    NWB::Tests::Smoke::s_ShadowSmokeOpenCaseBoundary64,
    NWB::Tests::Smoke::s_ShadowSmokeOpenCaseTerminal65,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgShadowMode::read(const b8 caustic){
    Core::Alloc::ScratchArena scratchArena(s_SmokeEnvironmentArena);
    if(const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_SHADOW_ARM")){
        const AStringView arm(value->data(), value->size());
        if(arm == "reference")
            this->arm = CsgShadowArm::Reference;
        else if(arm == "cut")
            this->arm = CsgShadowArm::Cut;
        else if(arm == "uncut")
            this->arm = CsgShadowArm::Uncut;
        else if(arm == "moved")
            this->arm = CsgShadowArm::Moved;
        else if(arm == "camera_shift")
            this->arm = CsgShadowArm::CameraShift;
        else
            return false;
    }
    if(const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_SHADOW_LIGHT")){
        const AStringView light(value->data(), value->size());
        if(light != s_ShadowSmokeDirectional && light != s_ShadowSmokePoint)
            return false;
        pointLight = light == s_ShadowSmokePoint;
    }
    if(const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_SHADOW_LIGHT_SOURCE")){
        const AStringView source(value->data(), value->size());
        if(source != s_ShadowSmokeHard && source != s_ShadowSmokeFinite)
            return false;
        finiteLightSource = source == s_ShadowSmokeFinite;
    }
    if(const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_SHADOW_ATLAS")){
        const AStringView atlas(value->data(), value->size());
        if(atlas != "boxes" && atlas != s_AnalyticAtlas && atlas != s_OpenAtlas)
            return false;
        analyticAtlas = atlas == s_AnalyticAtlas;
        openAtlas = atlas == s_OpenAtlas;
    }
    if(openAtlas){
        const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_SHADOW_OPEN_CASE");
        if(!value || pointLight || finiteLightSource || arm != CsgShadowArm::Cut)
            return false;
        const AStringView selected(value->data(), value->size());
        b8 found = false;
        for(u32 index = 0u; index < s_OpenCaseNames.size(); ++index){
            if(selected == s_OpenCaseNames[index]){
                openCase = static_cast<CsgShadowOpenCase::Enum>(index);
                found = true;
                break;
            }
        }
        if(!found)
            return false;
        if(caustic)
            return false;
    }
    return !analyticAtlas || (!pointLight && (arm == CsgShadowArm::Cut || arm == CsgShadowArm::Uncut));
}


AStringView CsgShadowMode::openCaseName()const noexcept{
    return s_OpenCaseNames[static_cast<u32>(openCase)];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

