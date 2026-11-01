// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::separator(const AStringView stableKey, const SeparatorOptions& options){
    if(itemDeclarationBlocked() || options.direction > SeparatorDirection::Vertical || !IsFinite(options.thickness) || options.thickness < 0.0f){
        m_context.fail();
        return false;
    }
    const UiSkinRegion* skinRegion = region(m_style.separator, m_style.separator);
    const auto state = m_context.declare(stableKey, WidgetKind::Separator);
    if(!skinRegion || !state){
        m_context.fail();
        return false;
    }
    const b8 horizontal = options.direction == SeparatorDirection::Horizontal;
    const f32 minimum = horizontal ? skinRegion->minimumHeight : skinRegion->minimumWidth;
    const f32 thickness = options.thickness > 0.0f ? options.thickness : Max(1.0f, minimum);
    Item item(m_arena);
    item.style = m_style;
    item.state = **state;
    LayoutNodeDesc description;
    description.width = horizontal ? options.length : LayoutSize{ LayoutSizePolicy::Fixed, thickness };
    description.height = horizontal ? LayoutSize{ LayoutSizePolicy::Fixed, thickness } : options.length;
    description.intrinsicSize = { skinRegion->minimumWidth, skinRegion->minimumHeight };
    if(!addItemNode(description, item))
        return false;
    m_scope->m_items.push_back(Move(item));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

