// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/module.h>

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scene defaults are applied before explicit smoke environment overrides.
struct SmokeRenderQualitySettings{
    Impl::CausticQualitySettings caustic;
    Impl::SurfelGiQualitySettings surfelGi;
    Impl::ShadowQualitySettings shadow;
    Impl::SoftwareShadowSettings softwareShadow;
};


[[nodiscard]] b8 ApplyCausticQualitySmokeSettings(Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena, const Impl::CausticQualitySettings& baseSettings = {});
[[nodiscard]] b8 ApplySurfelGiQualitySmokeSettings(Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena, const Impl::SurfelGiQualitySettings& baseSettings = {});
[[nodiscard]] b8 ApplyShadowQualitySmokeSettings(Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena, const Impl::ShadowQualitySettings& baseSettings = {});
[[nodiscard]] b8 ApplySoftwareShadowSmokeSettings(Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena, const Impl::SoftwareShadowSettings& baseSettings = {});
[[nodiscard]] b8 ApplyReflectionQualitySmokeSettings(const Impl::ReflectionSettings& baseSettings, Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

