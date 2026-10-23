// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SearchComboResult Builder::searchComboBox(AStringView stableKey, ISearchableListDataSource& source, SearchComboState& state, const SearchComboOptions& options){
    if(declarationBlocked() || !IsFinite(options.queryHeight) || options.queryHeight <= 0.0f || !IsFinite(options.queryGap) || options.queryGap < 0.0f){
        m_context.fail();
        return {};
    }
    const usize previousCount = m_scope->m_combos.size();
    SearchComboResult result;
    result.combo = declareCombo(stableKey, source, options.combo, &options, state.combo(), &state, &source);
    if(result.combo.valid && m_scope->m_combos.size() > previousCount)
        result.queryChanged = m_scope->m_combos.back().queryChanged;
    return result;
}

bool Builder::prepareComboSearch(const WidgetState& field, ComboFrame& frame){
    SearchComboState& state = *frame.search;
    EditModel& query = state.query();
    if(!frame.open){
        query.cancelComposition();
        snapshotComboQuery(frame);
        return comboMatches(frame);
    }
    const auto declared = m_context.declarePart(field, "query", WidgetKind::EditBox);
    if(!declared)
        return false;
    const WidgetState editor = **declared;
    if(!m_context.claimState(editor, query.instanceGeneration()))
        return false;
    Item item(m_arena);
    item.state = editor;
    item.style = m_style;
    item.editStyle = m_editStyle;
    item.editOptions.enabled = frame.options.enabled;
    EditBoxResult result;
    result.valid = true;
    result.focused = m_context.input().focus() == editor.id;
    if(m_editHost)
        result = m_editHost->editInPopup(editor, query, item.editOptions, frame.popupToken);
    if(!result.valid)
        return false;
    frame.queryChanged = result.textChanged;
    frame.editorSubmitted = result.submitted;
    if(result.cancelled){
        ComboBehavior::Close(*frame.state);
        m_context.input().closePopup(frame.popupToken);
        frame.open = false;
    }
    snapshotComboQuery(frame);
    if(!SearchComboBehavior::Filter(state, *frame.searchSource))
        return false;
    frame.results = &frame.searchSource->filtered();
    if(!ListBehavior::Reconcile(frame.state->m_list, *frame.results))
        return false;
    frame.listToken = { frame.state->m_list.inputGeneration(), frame.results->instanceGeneration(), frame.results->revision() };
    frame.resultCount = frame.results->rowCount();
    m_context.input().fenceControl(frame.rows.id, frame.rows.declarationGeneration, frame.listToken);
    if(!comboMatches(frame))
        return false;
    if(!frame.open)
        return true;
    if(!prepareEditBox(item, query, state.m_editor, result))
        return false;
    frame.editor = static_cast<u32>(m_scope->m_comboEditors.size());
    m_scope->m_comboEditors.push_back(Move(item));
    return comboMatches(frame);
}

void Builder::snapshotComboQuery(ComboFrame& frame)noexcept{
    if(!frame.search)
        return;
    const EditModel& query = frame.search->query();
    frame.queryGeneration = query.instanceGeneration();
    frame.queryRevision = query.revision();
    frame.queryCompositionGeneration = query.compositionGeneration();
    frame.querySelectionGeneration = query.selectionGeneration();
    frame.queryExternalRevision = query.externalRevision();
    frame.queryAnchor = query.anchor();
    frame.queryCaret = query.caret();
}

bool Builder::paintComboQuery(ComboFrame& frame, LayoutBox& content){
    if(frame.editor >= m_scope->m_comboEditors.size() || !comboMatches(frame))
        return false;
    const Item& item = m_scope->m_comboEditors[frame.editor];
    const SIMDVectorSingle measured = VectorAdd(VectorAdd(VectorSet(content.rectangle.width, item.editView.visualBounds().height, 0.0f, 0.0f), VectorSet(0.0f, item.padding.top, 0.0f, 0.0f)), VectorSet(0.0f, item.padding.bottom, 0.0f, 0.0f));
    const SIMDVectorSingle querySize = VectorMin(VectorMax(VectorSet(content.rectangle.width, frame.queryHeight, 0.0f, 0.0f), measured), VectorSet(content.rectangle.width, content.rectangle.height, 0.0f, 0.0f));
    LayoutBox box = content;
    box.rectangle.height = VectorGetY(querySize);
    HitTarget navigation;
    if(frame.options.enabled && !item.editView.composing()){
        navigation.commandOwner = frame.rows.id;
        navigation.commandOwnerDeclarationGeneration = frame.rows.declarationGeneration;
        navigation.commandControl = frame.listToken;
    }
    if(!paintEditBox(item, box, &navigation))
        return false;
    const SIMDVectorSingle extent = VectorMin(VectorReplicate(content.rectangle.height), VectorAdd(VectorReplicate(box.rectangle.height), VectorReplicate(frame.queryGap)));
    const SIMDVectorSingle contentOrigin = VectorAdd(VectorSet(content.rectangle.x, content.rectangle.y, 0.0f, 0.0f), VectorSelect(VectorZero(), extent, s_SIMDMaskY));
    const SIMDVectorSingle contentSize = VectorSubtract(VectorSet(content.rectangle.width, content.rectangle.height, 0.0f, 0.0f), VectorSelect(VectorZero(), extent, s_SIMDMaskY));
    content.rectangle.y = VectorGetY(contentOrigin);
    content.rectangle.height = VectorGetY(contentSize);
    return comboMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

