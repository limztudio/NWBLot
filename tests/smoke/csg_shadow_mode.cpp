// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_shadow_mode.h"
#include "smoke_environment.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_AnalyticAtlas = "analytic";
static constexpr AStringView s_OpenAtlas = "open";
static constexpr AStringView s_Directional = "directional";
static constexpr AStringView s_Point = "point";
static constexpr AStringView s_Hard = "hard";
static constexpr AStringView s_Finite = "finite";
static constexpr Array<AStringView, 6> s_OpenCaseNames = { "reference", "retained", "clipped_reference", "clipped", "boundary64", "terminal65", };


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
        if(light != s_Directional && light != s_Point)
            return false;
        pointLight = light == s_Point;
    }
    if(const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_SHADOW_LIGHT_SOURCE")){
        const AStringView source(value->data(), value->size());
        if(source != s_Hard && source != s_Finite)
            return false;
        finiteLightSource = source == s_Finite;
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

