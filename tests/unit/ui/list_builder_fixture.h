// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "widget_fixture.h"
#include "widget_id_assertion.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiListTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;
using namespace Impl::Ui;
using namespace UiWidgetTests;

inline constexpr AStringView s_PanelKey = "panel";
inline constexpr AStringView s_ListKey = "list";
inline constexpr AStringView s_PopupKey = "popup";
inline constexpr AStringView s_UpperPopupKey = "upper";
inline constexpr AStringView s_RowsKey = "rows";
inline constexpr AStringView s_ScrollbarKey = "scrollbar";
inline constexpr AStringView s_FirstRowText = "Ax";


// This source represents a large dataset arithmetically; no allocation or full scan depends on rowCount().
class ArithmeticSource final : public IListDataSource{
public:
    virtual ~ArithmeticSource()noexcept override = default;


public:
    [[nodiscard]] virtual u64 instanceGeneration()const noexcept override{ invalidateText(); return generation; }
    [[nodiscard]] virtual u64 revision()const noexcept override{ invalidateText(); return contentRevision; }
    [[nodiscard]] virtual u64 rowCount()const noexcept override{ invalidateText(); return count; }

    [[nodiscard]] virtual u64 key(const u64 index)const noexcept override{
        invalidateText();
        ++keyCalls;
        if(duplicate)
            return 1u;
        return rawKey(index);
    }

    [[nodiscard]] virtual Expected<u64> indexOf(const u64 value)const noexcept override{
        u64 index = 0u;
        invalidateText();
        ++lookupCalls;
        if(malformedLookup){
            index = count;
            return index;
        }
        if(value == 0u || value == removed || value > count + (removed != 0u ? 1u : 0u))
            return MakeUnexpected(Failure{});
        const u64 forward = value - 1u - (removed != 0u && value > removed ? 1u : 0u);
        index = reverse ? count - 1u - forward : forward;
        return index < count ? Expected<u64>{ index } : MakeUnexpected(Failure{});
    }

    [[nodiscard]] virtual Expected<u64> findEnabled(const u64 start, const bool backwards)const noexcept override{
        invalidateText();
        return UiWidgetTests::FindSingleDisabledFixtureRow(count, start, backwards, disabled, [this](const u64 index)noexcept{ return rawKey(index); });
    }

    [[nodiscard]] virtual StringView text(const u64 index)const noexcept override{
        ++textCalls;
        if(index < textCallsByIndex.size())
            ++textCallsByIndex[index];
        if(index == specialIndex){
            for(usize byte = 0u; byte < specialText.size(); ++byte)
                temporary[byte] = specialText[byte];
            return { temporary.data(), specialText.size() };
        }
        temporary[0u] = static_cast<char>('A' + index % 26u);
        temporary[1u] = 'x';
        return { temporary.data(), 2u };
    }

    [[nodiscard]] virtual bool enabled(const u64 index)const noexcept override{
        invalidateText();
        return rawKey(index) != disabled;
    }


private:
    [[nodiscard]] u64 rawKey(const u64 index)const noexcept{
        const u64 forward = reverse ? count - 1u - index : index;
        const u64 value = forward + 1u;
        return value + (removed != 0u && value >= removed ? 1u : 0u);
    }

    void invalidateText()const noexcept{
        temporary.fill('?');
    }


public:
    u64 count = 100000u;
    u64 generation = 301u;
    u64 contentRevision = 1u;
    u64 removed = 0u;
    u64 disabled = 0u;
    u64 specialIndex = Limit<u64>::s_Max;
    StringView specialText = "A\xCC\x81\xCC\x81 g\xCC\xA3\nPrevious\nNext";
    bool reverse = false;
    bool duplicate = false;
    bool malformedLookup = false;
    mutable u64 keyCalls = 0u;
    mutable u64 lookupCalls = 0u;
    mutable u64 textCalls = 0u;
    mutable Array<u64, 32u> textCallsByIndex{};
    mutable Array<char, 128u> temporary{};
};

[[nodiscard]] inline ListOptions Options()noexcept{
    ListOptions options;
    options.width = { LayoutSizePolicy::Fixed, 280.0f };
    options.height = { LayoutSizePolicy::Fixed, 120.0f };
    return options;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiListBuilderTests : public WidgetFixture{
protected:
    [[nodiscard]] bool prepare(const u64 generation, const ListOptions& options = Options()){
        return prepareSource(generation, m_source, m_state, options);
    }

    [[nodiscard]] bool prepareSource(const u64 generation, const IListDataSource& source, ListState& state, const ListOptions& options = Options(), const DisplayMetrics& display = { 800.0f, 600.0f, 1.0f, 1.0f }){
        if(!begin(generation, display) || !m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 260.0f }))
            return false;
        m_result = m_builder.virtualList(s_ListKey, source, state, options);
        return m_result.valid && finishPanel();
    }

    [[nodiscard]] bool accept(const u64 generation, const ListOptions& options = Options()){
        return prepare(generation, options) && m_context.commitFrame(generation);
    }

    [[nodiscard]] bool preparePopups(const u64 generation, PopupState& lowerPopup, ListState& lowerState, PopupState* upperPopup = nullptr, ListState* upperState = nullptr){
        if(!begin(generation) || !m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 260.0f }))
            return false;
        if(!m_builder.virtualList(s_ListKey, m_source, m_state, Options()).valid || !m_builder.endPanel())
            return false;
        PopupOptions popup;
        popup.anchor = { 350.0f, 10.0f, 10.0f, 10.0f };
        popup.size = { 320.0f, 180.0f };
        if(m_builder.beginPopup(s_PopupKey, lowerPopup, popup)){
            if(!m_builder.virtualList(s_ListKey, m_source, lowerState, Options()).valid || !m_builder.endPopup())
                return false;
        }
        if(upperPopup && upperState){
            popup.anchor = { 400.0f, 220.0f, 10.0f, 10.0f };
            if(m_builder.beginPopup(s_UpperPopupKey, *upperPopup, popup)){
                if(!m_builder.virtualList(s_ListKey, m_source, *upperState, Options()).valid || !m_builder.endPopup())
                    return false;
            }
        }
        return m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] WidgetId host()const{ return id(s_ListKey, s_PanelKey); }

    [[nodiscard]] WidgetId row(const u64 key)const{
        return UiTestSupport::RequireWidgetId(MakeWidgetPartId(UiTestSupport::RequireWidgetId(MakeWidgetId(host(), s_RowsKey)), key));
    }

    [[nodiscard]] bool wheel(const f64 delta = -1.0){
        const HitTarget* accepted = target(host());
        if(!accepted)
            return false;
        InputEvent event;
        event.type = InputEventType::PointerWheel;
        event.position = Center(accepted->rectangle);
        event.scrollY = delta;
        return send(event).pointerConsumed;
    }


protected:
    ArithmeticSource m_source;
    ListState m_state;
    ListResult m_result;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

