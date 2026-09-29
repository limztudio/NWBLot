// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "builder_scope.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BuilderScopeFrame::BuilderScopeFrame(Core::Alloc::GlobalArena& arena)
    : m_layout(arena)
    , m_items(arena)
    , m_textAreas(arena)
    , m_integerEdits(arena)
    , m_floatEdits(arena)
    , m_lists(arena)
    , m_radioGroups(arena)
    , m_sliders(arena)
    , m_combos(arena)
    , m_comboEditors(arena)
    , m_tooltips(arena)
    , m_contextMenus(arena)
    , m_stack(arena)
    , m_window(arena)
{
    m_items.reserve(s_LayoutMaxNodes);
    m_stack.reserve(64u);
}

void BuilderScopeFrame::reset(){
    m_panelActive = false;
    m_windowActive = false;
    m_window.state = nullptr;
    m_parent = nullptr;
    m_popupState = nullptr;
    m_popupToken = {};
    m_popupVisible = false;
    m_items.clear();
    m_textAreas.clear();
    m_integerEdits.clear();
    m_floatEdits.clear();
    m_lists.clear();
    m_radioGroups.clear();
    m_sliders.clear();
    m_combos.clear();
    m_comboEditors.clear();
    m_tooltips.clear();
    m_contextMenus.clear();
    m_stack.clear();
    m_layout.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

