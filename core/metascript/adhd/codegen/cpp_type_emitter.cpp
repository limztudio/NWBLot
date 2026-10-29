// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpp_type_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CppTypeEmitter::emitTypeCpp(MString& out, const TypeRef& type)const{
    if(type.refMode != ParamPassingMode::None){
        out.append("::NotNull<");
        if(type.refMode == ParamPassingMode::In)
            out.append("const ");
        TypeRef innerType = type;
        innerType.refMode = ParamPassingMode::None;
        emitTypeCpp(out, innerType);
        out.append("*>");
        return;
    }

    if(type.isAuto){
        out.append("auto");
        return;
    }

    if(type.isFuncType){
        out.append("::InplaceFunction<");
        AppendAdhdUnsigned(out, type.inferredCaptureBytes);
        out.append("u, ");
        if(type.funcReturnType)
            emitTypeCpp(out, *type.funcReturnType);
        else
            out.append("void");
        out.push_back('(');
        for(usize i = 0u; i < type.funcParams.size(); ++i){
            if(i > 0u)
                out.append(", ");
            if(type.funcParams[i].type)
                emitParamTypeCpp(out, type.funcParams[i].mode, *type.funcParams[i].type, false);
        }
        out.append(")>");
        return;
    }

    const MStringView name = type.name;
    if(const MStringView mappedBuiltin = SymbolTable::mapBuiltinCppType(name); !mappedBuiltin.empty()){
        out.append(mappedBuiltin.data(), mappedBuiltin.size());
        if(!type.templateArgs.empty())
            emitTypeArgListCpp(out, type.templateArgs);
        return;
    }

    if(m_symbols.findEnum(name)){
        out.append(name.data(), name.size());
        out.append("::Enum");
        return;
    }

    out.append(name.data(), name.size());
    if(!type.templateArgs.empty())
        emitTypeArgListCpp(out, type.templateArgs);
}

void CppTypeEmitter::emitTypeArgListCpp(MString& out, const MVector<TypeArg>& args)const{
    out.push_back('<');
    for(usize i = 0u; i < args.size(); ++i){
        if(i > 0u)
            out.append(", ");
        const TypeArg& arg = args[i];
        if(arg.type)
            emitTypeCpp(out, *arg.type);
        else if(!arg.constantText.empty())
            out.append(arg.constantText.data(), arg.constantText.size());
    }
    out.push_back('>');
}

void CppTypeEmitter::emitParamTypeCpp(
    MString& out,
    const ParamPassingMode::Enum mode,
    const TypeRef& type,
    const b8 topLevelConst
)const{
    if(mode == ParamPassingMode::Mut){
        emitTypeCpp(out, type);
        out.push_back('&');
        return;
    }

    if(isSmallValueType(type.name, m_symbols) && type.templateArgs.empty() && !type.isFuncType){
        if(topLevelConst)
            out.append("const ");
        emitTypeCpp(out, type);
        return;
    }

    out.append("const ");
    emitTypeCpp(out, type);
    out.push_back('&');
}

void CppTypeEmitter::emitTemplateHeaderCpp(MString& out, const MVector<TemplateParamDecl>& params)const{
    if(params.empty())
        return;

    out.append("template<");
    for(usize i = 0u; i < params.size(); ++i){
        if(i > 0u)
            out.append(", ");
        const TemplateParamDecl& p = params[i];
        if(p.constValueType){
            emitTypeCpp(out, *p.constValueType);
            out.push_back(' ');
            out.append(p.name.data(), p.name.size());
        }
        else{
            out.append("typename ");
            out.append(p.name.data(), p.name.size());
        }
    }
    out.append(">\r\n");
}

void CppTypeEmitter::emitMethodSignature(
    MString& out,
    const ClassDecl* ownerClass,
    const MethodDecl& method,
    const b8 isDeclarationInClass
)const{
    if(!method.isConstructor && !method.isDestructor){
        if(method.returnType)
            emitTypeCpp(out, *method.returnType);
        else
            out.append("void");
        out.push_back(' ');
    }

    if(!isDeclarationInClass && ownerClass){
        out.append(ownerClass->name.data(), ownerClass->name.size());
        out.append("::");
    }

    if(method.isDestructor)
        out.push_back('~');
    if(method.isOperator)
        out.append("operator");
    out.append(method.name.data(), method.name.size());

    out.push_back('(');
    for(usize i = 0u; i < method.params.size(); ++i){
        if(i > 0u)
            out.append(", ");
        const ParamDecl& p = method.params[i];
        if(p.type)
            emitParamTypeCpp(out, p.mode, *p.type);
        out.push_back(' ');
        out.append(p.name.data(), p.name.size());
    }
    out.push_back(')');

    if(method.isConst)
        out.append("const noexcept");
    else
        out.append("noexcept");
    if(isDeclarationInClass && method.isOverride)
        out.append(" override");
}

b8 CppTypeEmitter::isSmallValueType(MStringView typeName, const SymbolTable& symbols)noexcept{
    typeName = symbols.resolveBaseTypeName(typeName);
    if(typeName == "b8"
        || IsIntegerScalarType(typeName)
        || IsFloatScalarType(typeName)
        || typeName == "EntityID"
        || typeName == "Name" || typeName == "StringView")
    {
        return true;
    }
    if(symbols.findEnum(typeName))
        return true;
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

