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
    [[nodiscard]] virtual u64 instanceGeneration()const noexcept override;
    [[nodiscard]] virtual u64 revision()const noexcept override;
    [[nodiscard]] virtual u64 rowCount()const noexcept override;
    [[nodiscard]] virtual u64 key(u64 index)const noexcept override;
    [[nodiscard]] virtual Expected<u64> indexOf(u64 value)const noexcept override;
    [[nodiscard]] virtual Expected<u64> findEnabled(u64 start, b8 backwards)const noexcept override;
    [[nodiscard]] virtual StringView text(u64 index)const noexcept override;
    [[nodiscard]] virtual b8 enabled(u64 index)const noexcept override;


private:
    [[nodiscard]] u64 rawKey(const u64 index)const noexcept{
        return UiWidgetTests::ArithmeticFixtureRowKey(count, removed, reverse, index);
    }
    void invalidateText()const noexcept{ temporary.fill('?'); }


public:
    u64 count = 100000u;
    u64 generation = 301u;
    u64 contentRevision = 1u;
    u64 removed = 0u;
    u64 disabled = 0u;
    u64 specialIndex = Limit<u64>::s_Max;
    StringView specialText = "A\xCC\x81\xCC\x81 g\xCC\xA3\nPrevious\nNext";
    mutable u64 keyCalls = 0u;
    mutable u64 lookupCalls = 0u;
    mutable u64 textCalls = 0u;
    mutable Array<u64, 32u> textCallsByIndex{};
    b8 reverse = false;
    b8 duplicate = false;
    b8 malformedLookup = false;
    mutable Array<char, 128u> temporary{};
};

[[nodiscard]] ListOptions Options()noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiListBuilderTests : public WidgetFixture{
protected:
    [[nodiscard]] b8 prepare(const u64 generation, const ListOptions& options = Options()){
        return prepareSource(generation, m_source, m_state, options);
    }
    [[nodiscard]] b8 prepareSource(u64 generation, const IListDataSource& source, ListState& state, const ListOptions& options = Options(), const DisplayMetrics& display = { 800.0f, 600.0f, 1.0f, 1.0f });
    [[nodiscard]] b8 accept(const u64 generation, const ListOptions& options = Options()){
        return prepare(generation, options) && m_context.commitFrame(generation);
    }
    [[nodiscard]] b8 preparePopups(u64 generation, PopupState& lowerPopup, ListState& lowerState, PopupState* upperPopup = nullptr, ListState* upperState = nullptr);
    [[nodiscard]] WidgetId row(const u64 key)const{
        return UiTestSupport::RequireWidgetPartId(UiTestSupport::RequireWidgetId(id(s_ListKey, s_PanelKey), s_RowsKey), key);
    }
    [[nodiscard]] b8 wheel(f64 delta = -1.0);


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

