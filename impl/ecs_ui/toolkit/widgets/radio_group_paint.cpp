// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintRadioGroup(const Item& item, const LayoutBox& box){
    if(item.radioGroup >= m_scope->m_radioGroups.size())
        return false;
    RadioGroupFrame& frame = *m_scope->m_radioGroups[item.radioGroup];
    if(!radioGroupMatches(frame) || frame.m_labels.size() != frame.m_choices.count)
        return false;
    const auto placement = RadioGroupLayout::Place(box.rectangle, visibleClip(box.clip), frame.m_choices, frame.m_metrics);
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
    const WidgetId choices = MakeWidgetId(item.state.id, "choices");
    for(u32 index = 0u; index < placement->count; ++index){
        const RadioGroupChoicePlacement& row = placement->rows[index];
        const WidgetId part = MakeWidgetPartId(choices, row.key);
        const InputRouter& input = m_context.input();
        const bool enabled = frame.m_options.enabled && row.enabled;
        const bool checked = frame.m_snapshot.selectedKey == row.key;
        const bool hover = input.hover() == part;
        const bool pressed = input.capture() == part && input.primaryDown() && hover;
        const Name& preferred = checked ? frame.m_style.checked : !enabled ? frame.m_style.disabled
            : pressed ? frame.m_style.pressed : hover ? frame.m_style.hover : frame.m_style.normal;
        const Name& fallback = checked ? frame.m_style.checkedFallback : frame.m_style.normal;
        const UiSkinRegion* indicator = region(preferred, fallback);
        if(!indicator)
            indicator = region(frame.m_style.normal, frame.m_style.fallback);
        const UiSkinRegion* mark = region(frame.m_style.mark, frame.m_style.markFallback);
        if(!indicator || !mark)
            return false;
        const Color tint = !enabled ? frame.m_style.disabledTint : pressed ? frame.m_style.pressedTint
            : hover ? frame.m_style.hoverTint : Color{};
        m_paint.pushClip(row.clip);
        bool painted = m_paint.drawRegion(indicator->name, row.indicator, tint);
        if(painted && checked)
            painted = m_paint.drawRegion(mark->name, row.mark, tint);
        const bool focused = enabled && input.focus() == item.state.id && frame.m_snapshot.cursorKey == row.key;
        if(painted && focused && m_skin->findRegion(frame.m_style.focus))
            painted = m_paint.drawRegion(frame.m_style.focus, row.rectangle);
        const TextLayout& label = frame.m_labels[index];
        const Point origin{ row.rectangle.x + Min(row.rectangle.width, row.indicator.width + frame.m_metrics.gap), row.rectangle.y + Max(0.0f, (row.rectangle.height - label.measure().y) * 0.5f) };
        m_paint.pushClip(row.textClip);
        if(painted)
            painted = m_text.paint(m_paint, label, origin, enabled ? frame.m_widgetStyle.text : frame.m_widgetStyle.disabledText);
        const bool textPopped = m_paint.popClip();
        const bool rowPopped = m_paint.popClip();
        if(!painted || !textPopped || !rowPopped)
            return false;
        if(item.enabled){
            HitTarget target;
            target.rectangle = row.rectangle;
            target.clip = row.clip;
            target.enabled = enabled;
            target.activatable = enabled;
            target.control = frame.m_token;
            target.value = row.key;
            if(!m_context.addPartTarget(item.state, part, target))
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

