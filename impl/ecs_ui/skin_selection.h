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


// Commits a skin identity only after its complete asset and GPU binding succeeds.
class UiSkinSelection final{
public:
    explicit UiSkinSelection(const Core::Assets::AssetRef<UiSkin>& requested)
        : m_requested(requested)
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

    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& requested()const{ return m_requested; }
    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& selected()const{ return m_selected; }


private:
    Core::Assets::AssetRef<UiSkin> m_requested;
    Core::Assets::AssetRef<UiSkin> m_selected;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

