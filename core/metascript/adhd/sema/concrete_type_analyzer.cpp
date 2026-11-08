// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "concrete_type_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ConcreteTypeAnalyzer::isKnownConcreteTypeName(const MStringView name)const noexcept{
    if(name == "void")
        return true;
    if(BuiltinTypeSemantics::IsScalarType(name) || SymbolTable::IsSimdType(name) || SymbolTable::IsUnalignedVectorType(name) || m_symbols.hasTypeSymbol(name))
        return true;
    if(FindBuiltinObjectType(name) != nullptr)
        return true;
    if(FindBuiltinGenericType(name) != nullptr)
        return true;
    return false;
}

b8 ConcreteTypeAnalyzer::isConcreteTypeRef(const TypeRef& type)const noexcept{
    if(type.isAuto)
        return false;
    if(type.isFuncType){
        if(type.funcReturnType && !isConcreteTypeRef(*type.funcReturnType))
            return false;
        for(const FuncParamType& fp : type.funcParams){
            if(fp.type && fp.mode == ParamPassingMode::None && !isConcreteTypeRef(*fp.type))
                return false;
        }
        return true;
    }
    if(IsAssetHandleType(type.name))
        return type.templateArgs.size() == 1u && type.templateArgs[0].type != nullptr;
    if(!isKnownConcreteTypeName(type.name))
        return false;
    for(const TypeArg& arg : type.templateArgs){
        if(arg.type){
            if(!IsConcreteConstTemplateArgText(arg.type->name, m_symbols) && !isConcreteTypeRef(*arg.type))
                return false;
        }
        else if(!arg.constantText.empty()){
            if(!IsConcreteConstTemplateArgText(arg.constantText, m_symbols))
                return false;
        }
    }
    return true;
}

b8 ConcreteTypeAnalyzer::isConcreteTemplateArgList(const Span<const TemplateParamDecl> params, const Span<const TypeArg> args)const noexcept{
    if(params.size() != args.size())
        return false;
    for(usize i = 0u; i < params.size(); ++i){
        if(params[i].constValueType){
            if(!IsConcreteConstTemplateArgText(args[i].constantOrTypeName(), m_symbols))
                return false;
        }
        else{
            if(!args[i].type || !isConcreteTypeRef(*args[i].type))
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

