// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "numeric_edit_scene.h"
#include "edit_selection_probe.h"
#include "smoke_geometry.h"

#include <core/common/log.h>

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_numeric_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_RectNames[]{ GLB_TEXT("integer_bounds"), GLB_TEXT("integer_content"),
    GLB_TEXT("integer_caret"), GLB_TEXT("integer_selection"), GLB_TEXT("float_bounds"), GLB_TEXT("float_content"),
    GLB_TEXT("float_caret"), GLB_TEXT("float_selection"), GLB_TEXT("clipboard_bounds"), GLB_TEXT("clipboard_content"),
    GLB_TEXT("clipboard_caret"), GLB_TEXT("clipboard_selection") };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool Coherent(const Impl::Ui::EditModel& model, const Impl::Ui::EditBoxState& state){
    return
        state.modelGeneration == model.instanceGeneration() && state.revision == model.revision()
        && state.selectionGeneration == model.selectionGeneration()
        && state.anchor == model.anchor() && state.caret == model.caret()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Array<u64, 30u> UiNumericEditSmokeScene::values()const{
    using namespace __hidden_ui_numeric_snapshot;
    const auto& integer = m_integer.draft();
    const auto& floating = m_float.draft();
    return { BitCast<u64>(m_integer.value()), BitCast<u64>(m_float.value()), static_cast<u64>(integer.text().size()),
        HashSmokeText(integer.text()), static_cast<u64>(integer.anchor()), static_cast<u64>(integer.caret()),
        static_cast<u64>(m_integer.status()), static_cast<u64>(m_integer.dirty()), static_cast<u64>(m_states[0u].focused),
        static_cast<u64>(floating.text().size()), HashSmokeText(floating.text()), static_cast<u64>(floating.anchor()),
        static_cast<u64>(floating.caret()), static_cast<u64>(m_float.status(floatBounds())), static_cast<u64>(m_float.dirty()),
        static_cast<u64>(m_states[1u].focused), static_cast<u64>(m_clipboard.text().size()), HashSmokeText(m_clipboard.text()),
        static_cast<u64>(m_clipboard.anchor()), static_cast<u64>(m_clipboard.caret()), static_cast<u64>(m_states[2u].focused),
        static_cast<u64>(m_enabled), static_cast<u64>(m_readOnly), static_cast<u64>(m_clamp), m_commits, m_cancels,
        m_rejects, m_clamps, m_restored, static_cast<u64>(Coherent(integer, m_states[0u]) && Coherent(floating, m_states[1u])
            && Coherent(m_clipboard, m_states[2u])) };
}

void UiNumericEditSmokeScene::observeState(const Impl::Ui::DisplayMetrics& display, Impl::Ui::TextService& text){
    using namespace Impl::Ui;
    using namespace __hidden_ui_numeric_snapshot;
    UiNumericEditSnapshot current;
    current.values = values();
    current.display = display;
    const EditModel* const models[]{ &m_integer.draft(), &m_float.draft(), &m_clipboard };
    for(usize index = 0u; index < m_states.size(); ++index){
        const auto& placement = m_states[index].placement;
        current.rectangles[index * 4u] = placement.bounds;
        current.rectangles[index * 4u + 1u] = placement.content;
        current.rectangles[index * 4u + 2u] = placement.caret;
        current.rectangles[index * 4u + 3u] = CaptureEditSelection(m_arena, text, *models[index], placement, 16.0f);
    }
    bool changed = current.values != m_snapshot.values;
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameSmokeRect(current.rectangles[index], m_snapshot.rectangles[index]);
    const auto& previous = m_snapshot.display;
    const bool displayChanged = display.logicalWidth != previous.logicalWidth || display.logicalHeight != previous.logicalHeight
        || display.pixelScaleX != previous.pixelScaleX || display.pixelScaleY != previous.pixelScaleY;
    if(m_snapshot.sequence != 0u && !changed && !displayChanged)
        return;
    current.sequence = m_snapshot.sequence + 1u;
    m_snapshot = current;
    if(displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("UiNumericEditSmoke: display logical={}x{} scale={}x{}")
            , display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY
        );
    }
    const auto& value = m_snapshot.values;
    NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("UiNumericEditSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}")
        , m_snapshot.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7]
        , value[8], value[9], value[10], value[11], value[12], value[13], value[14], value[15]
        , value[16], value[17], value[18], value[19], value[20], value[21], value[22], value[23]
        , value[24], value[25], value[26], value[27], value[28], value[29]
    );
    for(usize index = 0u; index < m_snapshot.rectangles.size(); ++index)
        LogSmokeRect(GLB_TEXT("UiNumericEditSmoke"), m_snapshot.sequence, s_RectNames[index], m_snapshot.rectangles[index]);
}

void UiNumericEditSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    const f32 y = context.display.logicalHeight - 18.0f;
    usize marker = 0u;
    for(usize index = 0u; index <= m_snapshot.values.size(); ++index){
        const u64 value = index == m_snapshot.values.size() ? m_snapshot.sequence : m_snapshot.values[index];
        const u32 parts = index < 2u ? 6u : index == 3u || index == 10u || index == 17u ? 3u : 2u;
        for(u32 part = 0u; part < parts; ++part){
            context.paint.fillRect({ 8.0f + static_cast<f32>(marker) * 8.0f, y, 6.0f, 10.0f },
                EncodeSmokeColor(value >> (part * 12u)));
            ++marker;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

