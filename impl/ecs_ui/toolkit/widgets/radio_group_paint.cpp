// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::paintRadioGroup(const Item& item, const LayoutBox& box){
    if(item.radioGroup >= m_scope->m_radioGroups.size())
        return false;
    RadioGroupFrame& frame = m_scope->m_radioGroups[item.radioGroup];
    if(!radioGroupMatches(frame) || frame.m_labels.size() != frame.m_choices.count)
        return false;
    const auto placement = PlaceRadioGroup(box.rectangle, visibleClip(box.clip), frame.m_choices, frame.m_metrics);
    if(!placement)
        return false;
    HitTarget host;
    host.rectangle = placement->bounds;
    host.clip = placement->clip;
    host.enabled = item.enabled;
    host.focusable = item.enabled;
    host.navigable = item.enabled;
    host.horizontalNavigation = item.enabled;
    host.contextMenu = item.contextMenu;
    host.control = frame.m_token;
    host.focusOnCommit = frame.m_focusOnCommit;
    if(!m_context.addTarget(item.state, host))
        return false;
    const auto choices = TryMakeWidgetId(item.state.id, s_WidgetPartChoices);
    if(!choices)
        return false;
    const UiSkinRegion* mark = placement->count != 0u ? region(frame.m_style.mark, frame.m_style.markFallback) : nullptr;
    for(u32 index = 0u; index < placement->count; ++index){
        const RadioGroupChoicePlacement& row = placement->rows[index];
        const auto part = TryMakeWidgetPartId(*choices, row.key);
        if(!part)
            return false;
        const InputRouter& input = m_context.input();
        const b8 enabled = frame.m_options.enabled && row.enabled;
        const b8 checked = frame.m_snapshot.selectedKey == row.key;
        const b8 hover = input.hover() == *part;
        const b8 pressed = input.pressed(*part) || (frame.m_snapshot.cursorKey == row.key && input.pressed(item.state.id));
        const b8 focused = enabled && input.focus() == item.state.id && frame.m_snapshot.cursorKey == row.key;
        if(!mark || !PaintRadioGroupChoiceRow(
            m_paint,
            m_text,
            *m_skin,
            frame.m_style,
            frame.m_widgetStyle,
            frame.m_metrics,
            row,
            frame.m_labels[index].text,
            frame.m_labels[index].bounds,
            mark->name,
            enabled,
            checked,
            pressed,
            hover,
            focused
        ))
            return false;
        if(item.enabled){
            HitTarget target;
            target.rectangle = row.rectangle;
            target.clip = row.clip;
            target.enabled = enabled;
            target.activatable = enabled;
            target.control = frame.m_token;
            target.value = row.key;
            if(!m_context.addPartTarget(item.state, *part, target))
                return false;
        }
    }
    if(!radioGroupMatches(frame))
        return false;
    // Diagnostics describe prepared placement; input still uses the last published owned hit targets.
    frame.m_state.m_placement = *placement;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

