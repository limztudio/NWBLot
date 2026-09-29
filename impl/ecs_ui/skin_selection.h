// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiSkin;

inline constexpr Core::Assets::AssetRef<UiSkin> s_DefaultUiSkinRef{ "engine/ui/skins/default/atlas" };

namespace UiSkinSelectionResult{
    enum Enum : u8{ Failed, Selected, DefaultFallback };
};

namespace UiSkinChangeResult{
    enum Enum : u8{ Deferred, Applied, Failed };
};


// Commits a skin identity only after its complete asset and GPU binding succeeds.
class UiSkinSelection final{
public:
    explicit UiSkinSelection(const Core::Assets::AssetRef<UiSkin>& requested)
        : m_requested(requested)
        , m_requestedChange(requested)
    {}


public:
    template<typename TTryBind>
    [[nodiscard]] UiSkinSelectionResult::Enum ensure(TTryBind&& tryBind){
        if(m_selected.valid())
            return tryBind(m_selected) ? UiSkinSelectionResult::Selected : UiSkinSelectionResult::Failed;

        const Core::Assets::AssetRef<UiSkin> preferred = m_requested.valid() ? m_requested : s_DefaultUiSkinRef;
        if(tryBind(preferred)){
            m_selected = preferred;
            return UiSkinSelectionResult::Selected;
        }
        if(preferred == s_DefaultUiSkinRef || !tryBind(s_DefaultUiSkinRef))
            return UiSkinSelectionResult::Failed;
        m_selected = s_DefaultUiSkinRef;
        return UiSkinSelectionResult::DefaultFallback;
    }

    void requestChange(const Core::Assets::AssetRef<UiSkin>& requested){
        m_requestedChange = requested;
        m_changePending = true;
        m_changeFailed = false;
    }

    // A failed request remains recorded, but retries only after explicit request or successful resource validation.
    void resourcesValidated(){
        if(m_changeFailed)
            m_changePending = true;
    }

    template<typename TTryBind>
    [[nodiscard]] UiSkinChangeResult::Enum applyChangeIfReady(
        const bool resourcesReady,
        const bool gpuFramePending,
        const bool layoutPending,
        TTryBind&& tryBind){
        if(!m_changePending || !resourcesReady || gpuFramePending || layoutPending)
            return UiSkinChangeResult::Deferred;
        m_changePending = false;
        if(!m_selected.valid() || m_generation == Limit<u64>::s_Max){
            m_changeFailed = true;
            return UiSkinChangeResult::Failed;
        }
        const Core::Assets::AssetRef<UiSkin> candidate = m_requestedChange.valid() ? m_requestedChange : s_DefaultUiSkinRef;
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

    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& requested()const{ return m_requested; }
    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& selected()const{ return m_selected; }
    // The latest desired reference may be empty (engine default) or differ from the last accepted selection.
    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& requestedChange()const{ return m_requestedChange; }
    [[nodiscard]] u64 generation()const{ return m_generation; }
    [[nodiscard]] bool changePending()const{ return m_changePending; }
    [[nodiscard]] bool changeFailed()const{ return m_changeFailed; }


private:
    Core::Assets::AssetRef<UiSkin> m_requested;
    Core::Assets::AssetRef<UiSkin> m_selected;
    Core::Assets::AssetRef<UiSkin> m_requestedChange;
    u64 m_generation = 1u;
    bool m_changePending = false;
    bool m_changeFailed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

