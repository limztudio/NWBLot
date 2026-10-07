// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_builder_numeric_edit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename Model, typename Bounds>
class NumericActions final : public IEditActionSink{
public:
    NumericActions(Model& model, const Bounds& bounds, NumericEditResult& result, u64& revision, const Context& context)
        : m_model(model)
        , m_bounds(bounds)
        , m_result(result)
        , m_revision(revision)
        , m_context(context)
    {}
    virtual ~NumericActions()override = default;


public:
    [[nodiscard]] virtual bool apply(EditModel& draft, const EditAction::Enum action, const bool readOnly)override{
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


NumericEditBoxResult Builder::integerEdit(const AStringView stableKey, IntegerEditModel& model,
    EditBoxState& state, const IntegerEditOptions& options
){
    NumericEditBoxResult result;
    result.numeric.valid = true;
    if(declarationBlocked() || !ValidateIntegerBounds(options.bounds)){
        m_context.fail();
        result.numeric.valid = false;
        return result;
    }
    IntegerEditFrame frame;
    frame.model = &model;
    frame.state = &state;
    frame.revision = model.revision();
    __hidden_builder_numeric_edit::NumericActions actions(model, options.bounds, result.numeric, frame.revision, m_context);
    const usize previousCount = m_scope->m_items.size();
    result.edit = declareEditBox(stableKey, model.lendDraft(), state, options.edit, &actions, &frame);
    result.numeric.valid &= result.edit.valid;
    if(result.edit.valid && m_scope->m_items.size() > previousCount){
        frame.item = static_cast<u32>(previousCount);
        m_scope->m_integerEdits.push_back(frame);
    }
    return result;
}

NumericEditBoxResult Builder::floatEdit(const AStringView stableKey, FloatEditModel& model,
    EditBoxState& state, const FloatEditOptions& options
){
    NumericEditBoxResult result;
    result.numeric.valid = true;
    if(declarationBlocked() || !ValidateFloatBounds(options.bounds)){
        m_context.fail();
        result.numeric.valid = false;
        return result;
    }
    FloatEditFrame frame;
    frame.model = &model;
    frame.state = &state;
    frame.revision = model.revision();
    __hidden_builder_numeric_edit::NumericActions actions(model, options.bounds, result.numeric, frame.revision, m_context);
    const usize previousCount = m_scope->m_items.size();
    result.edit = declareEditBox(stableKey, model.lendDraft(), state, options.edit, &actions, nullptr, &frame);
    result.numeric.valid &= result.edit.valid;
    if(result.edit.valid && m_scope->m_items.size() > previousCount){
        frame.item = static_cast<u32>(previousCount);
        m_scope->m_floatEdits.push_back(frame);
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

