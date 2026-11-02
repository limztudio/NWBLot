// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_builder_numeric_edit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename Model, typename Bounds>
class NumericActions final : public IEditActionSink{
public:
    NumericActions(const Bounds& bounds, const Context& context, Model& model, NumericEditResult& result, u64& revision)noexcept
        : m_model(model)
        , m_bounds(bounds)
        , m_result(result)
        , m_revision(revision)
        , m_context(context)
    {}
    virtual ~NumericActions()noexcept override = default;


public:
    [[nodiscard]] virtual b8 apply(EditModel& draft, const EditAction::Enum action, const b8 readOnly)noexcept(
        noexcept(static_cast<b8>(DeclVal<const Context&>().failed() || &draft != &DeclVal<Model&>().draft() || DeclVal<Model&>().revision() != DeclVal<u64&>()))
        && noexcept(DeclVal<NumericEditResult&>() = DeclVal<Model&>().submit(DeclVal<const Bounds&>()))
        && noexcept(DeclVal<NumericEditResult&>() = DeclVal<Model&>().cancel())
        && noexcept(DeclVal<NumericEditResult&>() = DeclVal<Model&>().abandon())
        && noexcept(DeclVal<NumericEditResult&>() = DeclVal<Model&>().blur(DeclVal<const Bounds&>()))
        && noexcept(DeclVal<NumericEditResult&>() = (readOnly ? DeclVal<Model&>().abandon() : DeclVal<Model&>().blur(DeclVal<const Bounds&>())))
        && noexcept(DeclVal<u64&>() = DeclVal<Model&>().revision())
    )override{
        if(m_context.failed() || &draft != &m_model.draft() || m_model.revision() != m_revision){
            m_result.valid = false;
            return false;
        }
        NumericEditResult result;
        switch(action){
        case EditAction::Submit:
            if(readOnly)
                result.valid = true;
            else
                result = m_model.submit(m_bounds);
            break;
        case EditAction::Cancel: result = m_model.cancel(); break;
        case EditAction::Blur: result = readOnly ? m_model.abandon() : m_model.blur(m_bounds); break;
        case EditAction::Abandon: result = m_model.abandon(); break;
        default: break;
        }
        m_revision = m_model.revision();
        m_result.valid &= result.valid;
        m_result.committed |= result.committed;
        m_result.valueChanged |= result.valueChanged;
        m_result.cancelled |= result.cancelled;
        m_result.rejected |= result.rejected;
        m_result.clamped |= result.clamped;
        m_result.restored |= result.restored;
        return result.valid && !m_context.failed();
    }


private:
    Model& m_model;
    const Bounds& m_bounds;
    NumericEditResult& m_result;
    u64& m_revision;
    const Context& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename FrameT, typename ModelT, typename OptionsT, typename ValidateBoundsF, typename FrameListT>
NumericEditBoxResult Builder::declareNumericEdit(const AStringView stableKey, ModelT& model, EditBoxState& state, const OptionsT& options, const ValidateBoundsF validateBounds, FrameListT& frames){
    NumericEditBoxResult result;
    result.numeric.valid = true;
    if(declarationBlocked() || !validateBounds(options.bounds)){
        m_context.fail();
        result.numeric.valid = false;
        return result;
    }
    FrameT frame;
    frame.model = &model;
    frame.state = &state;
    frame.revision = model.revision();
    __hidden_builder_numeric_edit::NumericActions actions(options.bounds, m_context, model, result.numeric, frame.revision);
    const usize previousCount = m_scope->m_items.size();
    if constexpr(IsSame_V<FrameT, IntegerEditFrame>)
        result.edit = declareEditBox(stableKey, options.edit, model.lendDraft(), state, &actions, &frame);
    else
        result.edit = declareEditBox(stableKey, options.edit, model.lendDraft(), state, &actions, nullptr, &frame);
    result.numeric.valid &= result.edit.valid;
    if(result.edit.valid && m_scope->m_items.size() > previousCount){
        frame.item = static_cast<u32>(previousCount);
        frames.push_back(frame);
    }
    return result;
}

NumericEditBoxResult Builder::integerEdit(const AStringView stableKey, IntegerEditModel& model, EditBoxState& state, const IntegerEditOptions& options){
    return declareNumericEdit<IntegerEditFrame>(stableKey, model, state, options, ValidateIntegerBounds, m_scope->m_integerEdits);
}

NumericEditBoxResult Builder::floatEdit(const AStringView stableKey, FloatEditModel& model, EditBoxState& state, const FloatEditOptions& options){
    return declareNumericEdit<FloatEditFrame>(stableKey, model, state, options, ValidateFloatBounds, m_scope->m_floatEdits);
}

void Builder::snapshotNumericEdit(NumericEditFrame& frame, const EditModel& draft)noexcept{
    frame.draftGeneration = draft.instanceGeneration();
    frame.draftRevision = draft.revision();
    frame.draftExternalRevision = draft.externalRevision();
    frame.draftCompositionGeneration = draft.compositionGeneration();
    frame.draftSelectionGeneration = draft.selectionGeneration();
    frame.anchor = draft.anchor();
    frame.caret = draft.caret();
}

b8 Builder::numericDraftMatches(const NumericEditFrame& frame, const EditModel& draft)const noexcept{
    return
        popupAncestorsVisible() && frame.state && draft.instanceGeneration() == frame.draftGeneration
        && draft.revision() == frame.draftRevision && draft.externalRevision() == frame.draftExternalRevision
        && draft.compositionGeneration() == frame.draftCompositionGeneration && draft.selectionGeneration() == frame.draftSelectionGeneration
        && draft.anchor() == frame.anchor && draft.caret() == frame.caret
        && frame.state->modelGeneration == frame.draftGeneration && frame.state->revision == frame.draftRevision
        && frame.state->selectionGeneration == frame.draftSelectionGeneration
        && frame.state->anchor == frame.anchor && frame.state->caret == frame.caret && frame.state->focused == frame.focused
    ;
}

b8 Builder::numericEditMatches(const Item& item)const noexcept{
    if(item.integerEdit != s_LayoutNoParent){
        if(item.integerEdit >= m_scope->m_integerEdits.size() || !numericEditFrameMatches(m_scope->m_integerEdits[item.integerEdit]))
            return false;
    }
    if(item.floatEdit != s_LayoutNoParent){
        if(item.floatEdit >= m_scope->m_floatEdits.size() || !numericEditFrameMatches(m_scope->m_floatEdits[item.floatEdit]))
            return false;
    }
    return true;
}

b8 Builder::numericStateAvailable(const EditBoxState& state)const noexcept{
    for(usize index = 0u; index <= m_popupFrameCount; ++index){
        const BuilderScopeFrame& scope = index == 0u ? m_scopeFrame : *m_popupFrames[index - 1u];
        for(const auto& frame : scope.m_integerEdits){
            if(frame.state == &state)
                return false;
        }
        for(const auto& frame : scope.m_floatEdits){
            if(frame.state == &state)
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

