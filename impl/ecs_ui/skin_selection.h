// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_ui/components.adhd.h>

#include <core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiSkin;

// Commits a skin identity only after its complete asset and GPU binding succeeds.
class UiSkinSelection final{
public:
    explicit UiSkinSelection(const Core::Assets::AssetRef<UiSkin>& requested)noexcept
        : m_requested(requested)
        , m_requestedChange(requested)
    {}


public:
    template<typename TTryBind>
    [[nodiscard]] UiSkinSelectionResult::Enum ensure(
        const Core::Assets::AssetRef<UiSkin>& basicSkin,
        TTryBind&& tryBind
    )noexcept(noexcept(static_cast<b8>(tryBind(m_selected))) && noexcept(static_cast<b8>(tryBind(basicSkin))) && noexcept(static_cast<b8>(true || !tryBind(basicSkin)))){
        if(m_selected.valid())
            return tryBind(m_selected) ? UiSkinSelectionResult::Selected : UiSkinSelectionResult::Failed;
        if(!basicSkin.valid())
            return UiSkinSelectionResult::Failed;

        const Core::Assets::AssetRef<UiSkin> preferred = m_requested.valid() ? m_requested : basicSkin;
        if(tryBind(preferred)){
            m_selected = preferred;
            return UiSkinSelectionResult::Selected;
        }
        if(preferred == basicSkin || !tryBind(basicSkin))
            return UiSkinSelectionResult::Failed;
        m_selected = basicSkin;
        return UiSkinSelectionResult::DefaultFallback;
    }

    void requestChange(const Core::Assets::AssetRef<UiSkin>& requested)noexcept{
        m_requestedChange = requested;
        m_changePending = true;
        m_changeFailed = false;
    }

    // A failed request remains recorded, but retries only after explicit request or successful resource validation.
    void resourcesValidated()noexcept{
        if(m_changeFailed)
            m_changePending = true;
    }

    template<typename TTryBind>
    [[nodiscard]] UiSkinChangeResult::Enum applyChangeIfReady(
        const b8 resourcesReady,
        const b8 gpuFramePending,
        const b8 layoutPending,
        const Core::Assets::AssetRef<UiSkin>& basicSkin,
        TTryBind&& tryBind
    )noexcept(noexcept(static_cast<b8>(!tryBind(basicSkin, DeclVal<const u64&>())))){
        if(!m_changePending || !resourcesReady || gpuFramePending || layoutPending)
            return UiSkinChangeResult::Deferred;
        m_changePending = false;
        const Core::Assets::AssetRef<UiSkin> candidate = m_requestedChange.valid() ? m_requestedChange : basicSkin;
        if(!m_selected.valid() || !candidate.valid() || m_generation == Limit<u64>::s_Max){
            m_changeFailed = true;
            return UiSkinChangeResult::Failed;
        }
        const u64 nextGeneration = m_generation + 1u;
        if(!tryBind(candidate, nextGeneration)){
            m_changeFailed = true;
            return UiSkinChangeResult::Failed;
        }
        m_selected = candidate;
        m_generation = nextGeneration;
        m_changeFailed = false;
        return UiSkinChangeResult::Applied;
    }

    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& requested()const noexcept{ return m_requested; }

    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& selected()const noexcept{ return m_selected; }
    // The latest desired reference may be empty (engine default) or differ from the last accepted selection.
    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& requestedChange()const noexcept{ return m_requestedChange; }

    [[nodiscard]] u64 generation()const noexcept{ return m_generation; }

    [[nodiscard]] b8 changePending()const noexcept{ return m_changePending; }

    [[nodiscard]] b8 changeFailed()const noexcept{ return m_changeFailed; }


private:
    Core::Assets::AssetRef<UiSkin> m_requested;
    Core::Assets::AssetRef<UiSkin> m_selected;
    Core::Assets::AssetRef<UiSkin> m_requestedChange;
    u64 m_generation = 1u;
    b8 m_changePending = false;
    b8 m_changeFailed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

