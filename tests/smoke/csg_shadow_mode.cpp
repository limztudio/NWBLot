// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_shadow_mode.h"
#include "smoke_environment.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgShadowMode::read(const b8 caustic){
    Core::Alloc::ScratchArena scratchArena(s_SmokeEnvironmentArena);
    if(const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_SHADOW_ARM")){
        if(!TryParseCsgShadowArm(AStringView(value->data(), value->size()), this->arm))
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
        if(atlas != "boxes" && atlas != s_ShadowSmokeAnalyticAtlas && atlas != s_ShadowSmokeOpenAtlas)
            return false;
        analyticAtlas = atlas == s_ShadowSmokeAnalyticAtlas;
        openAtlas = atlas == s_ShadowSmokeOpenAtlas;
    }
    if(openAtlas){
        const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_SHADOW_OPEN_CASE");
        if(!value || pointLight || finiteLightSource || arm != CsgShadowArm::Cut || caustic)
            return false;
        if(!TryParseCsgShadowOpenCase(AStringView(value->data(), value->size()), openCase))
            return false;
    }
    return !analyticAtlas || (!pointLight && (arm == CsgShadowArm::Cut || arm == CsgShadowArm::Uncut));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
