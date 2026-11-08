// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_layout_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TypeLayoutAnalyzer::containsArenaBackedContainer(const TypeRef& type, const u32 depth)const noexcept{
    if(depth >= 8u || type.refMode != ParamPassingMode::None)
        return false;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None)
        return false;
    if(SymbolTable::IsArenaBackedContainer(unwrapped->name))
        return true;
    for(const TypeArg& targ : unwrapped->templateArgs){
        if(targ.type && containsArenaBackedContainer(*targ.type, depth + 1u))
            return true;
    }
    return false;
}

b8 TypeLayoutAnalyzer::containsSimdRegisterType(const TypeRef& type, const u32 depth)const noexcept{
    if(depth >= 8u || type.isFuncType)
        return false;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->isFuncType)
        return false;
    if(SymbolTable::IsSimdRegisterType(m_symbols.resolveBaseTypeName(unwrapped->name)))
        return true;
    for(const TypeArg& targ : unwrapped->templateArgs){
        if(targ.type && containsSimdRegisterType(*targ.type, depth + 1u))
            return true;
    }
    return false;
}

b8 TypeLayoutAnalyzer::shouldPackSerializeClass(const Span<const FieldDecl* const> fields)const noexcept{
    for(const FieldDecl* field : fields){
        if(!field || !field->type)
            continue;
        const TypeRef* resolved = m_symbols.unwrapTypeAlias(field->type);
        const MStringView baseName = resolved ? resolved->name : field->type->name;
        if(SymbolTable::IsSimdType(baseName) || (m_symbols.findClass(baseName) && !shouldPackSerializeClass(m_symbols.findClass(baseName)->fields)))
            return false;
        if(IsFixedArrayContainerType(baseName) && resolved && !resolved->templateArgs.empty() && resolved->templateArgs[0].type){
            if(SymbolTable::IsSimdType(m_symbols.resolveBaseTypeName(resolved->templateArgs[0].type->name)))
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

