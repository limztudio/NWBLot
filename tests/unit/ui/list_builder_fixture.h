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


// Arithmetic list source that computes rows on demand without allocating per row.
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
        invalidateText();
        ++lookupCalls;
        if(malformedLookup)
            return count;
        return UiWidgetTests::FindArithmeticFixtureRowIndex(count, removed, reverse, value);
    }

    [[nodiscard]] virtual Expected<u64> findEnabled(const u64 start, const b8 backwards)const noexcept override{
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

    [[nodiscard]] virtual b8 enabled(const u64 index)const noexcept override{
        invalidateText();
        return rawKey(index) != disabled;
    }


private:
    [[nodiscard]] u64 rawKey(const u64 index)const noexcept{
        return UiWidgetTests::ArithmeticFixtureRowKey(count, removed, reverse, index);
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
    b8 reverse = false;
    b8 duplicate = false;
    b8 malformedLookup = false;
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
    [[nodiscard]] b8 prepare(const u64 generation, const ListOptions& options = Options()){
        return prepareSource(generation, m_source, m_state, options);
    }

    [[nodiscard]] b8 prepareSource(const u64 generation, const IListDataSource& source, ListState& state, const ListOptions& options = Options(), const DisplayMetrics& display = { 800.0f, 600.0f, 1.0f, 1.0f }){
        if(!begin(generation, display) || !m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 260.0f }))
            return false;
        m_result = m_builder.virtualList(s_ListKey, source, state, options);
        return m_result.valid && finishPanel();
    }

    [[nodiscard]] b8 accept(const u64 generation, const ListOptions& options = Options()){
        return prepare(generation, options) && m_context.commitFrame(generation);
    }

    [[nodiscard]] b8 preparePopups(const u64 generation, PopupState& lowerPopup, ListState& lowerState, PopupState* upperPopup = nullptr, ListState* upperState = nullptr){
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

    [[nodiscard]] b8 wheel(const f64 delta = -1.0){
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

