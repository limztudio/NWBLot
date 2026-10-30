// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "scroll.h"
#include <impl/ecs_ui/toolkit/widgets/list_style.adhd.h>
#include <impl/ecs_ui/toolkit/input/input.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Keys are nonzero and unique within an instance. Lookups must avoid scanning the complete dataset per frame.
interface IListDataSource{
    virtual ~IListDataSource()noexcept = default;


public:
    [[nodiscard]] virtual u64 instanceGeneration()const = 0;
    [[nodiscard]] virtual u64 revision()const = 0;
    [[nodiscard]] virtual u64 rowCount()const = 0;
    [[nodiscard]] virtual u64 key(u64 index)const = 0;
    [[nodiscard]] virtual Expected<u64> indexOf(u64 key)const = 0;
    // Inclusive search from start in the requested direction; failure means no enabled row remains.
    [[nodiscard]] virtual Expected<u64> findEnabled(u64 start, b8 reverse)const = 0;
    // Text may be temporary until the next source call; the list shapes/copies it before that call.
    [[nodiscard]] virtual StringView text(u64 index)const = 0;
    [[nodiscard]] virtual b8 enabled(u64 index)const = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ListState final : NoCopy{
    friend class Builder;
    friend class BuilderScopeFrame;
    friend class ListBehavior;


private:
    struct MeasureKey{
        u64 sourceGeneration = 0u;
        u64 sourceRevision = 0u;
        u64 rowCount = 0u;
        u64 textIdentity = 0u;
        u64 textGeneration = 0u;
        f32 fontSize = 0.0f;
        f32 pixelScaleX = 0.0f;
        f32 pixelScaleY = 0.0f;
        Array<f32, s_ListRowMetricCount> rowMetrics{};
        Array<char, s_TextMaxLanguageBytes + 1u> language{};
        u32 scriptTag = 0u;


    public:
        [[nodiscard]] b8 operator==(const MeasureKey&)const noexcept = default;
    };


public:
    ListState()noexcept;
    ListState(ListState&&) = delete;


public:
    ListState& operator=(ListState&&) = delete;


public:
    [[nodiscard]] u64 instanceGeneration()const noexcept{ return m_scroll.instanceGeneration(); }

    [[nodiscard]] u64 inputGeneration()const noexcept{ return m_inputGeneration; }

    [[nodiscard]] u64 selectedKey()const noexcept{ return m_selected; }

    [[nodiscard]] u64 cursorKey()const noexcept{ return m_cursor; }

    [[nodiscard]] f64 scrollOffset()const noexcept{ return m_scroll.offset(); }

    [[nodiscard]] const ScrollPlacement& placement()const noexcept{ return m_placement; }
    // Explicit application changes start a new input lifetime even when the value is unchanged.
    void select(u64 key)noexcept;
    [[nodiscard]] b8 scrollTo(f64 offset)noexcept;


private:
    MeasureKey m_measureKey;
    f32 m_rowHeight = 0.0f;
    f32 m_scrollRowHeight = 0.0f;
    u64 m_scrollSourceGeneration = 0u;
    ScrollState m_scroll;
    ScrollPlacement m_placement;
    u64 m_inputGeneration = 0u;
    u64 m_selected = 0u;
    u64 m_cursor = 0u;
    u64 m_sourceGeneration = 0u;
    u64 m_sourceRevision = 0u;
    b8 m_ensureCursor = false;
};

class ListBehavior final{
public:
    [[nodiscard]] static b8 Reconcile(ListState& state, const IListDataSource& source);
    [[nodiscard]] static b8 Apply(ListState& state, ListResult& result, const IListDataSource& source, const ListOptions& options, const ControlAction& action);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

