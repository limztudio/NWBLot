// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpp_type_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendCamelToSnakeCase(MString& out, const MStringView text){
    for(usize i = 0u; i < text.size(); ++i){
        const char ch = text[i];
        if(IsAsciiUpper(ch)){
            if(i > 0u){
                const char prev = text[i - 1u];
                const b8 prevLowerOrDigit = IsAsciiLower(prev) || IsAsciiDigit(prev);
                const b8 nextLower = (i + 1u < text.size()) && IsAsciiLower(text[i + 1u]);
                if(prevLowerOrDigit || nextLower)
                    out.push_back('_');
            }
            out.push_back(static_cast<char>(ch + ('a' - 'A')));
        }
        else{
            out.push_back(ch);
        }
    }
}

void AppendAdhdTextCrlf(MString& out, const MStringView text){
    for(usize i = 0u; i < text.size(); ++i){
        const char ch = text[i];
        if(ch == '\r')
            continue;
        if(ch == '\n')
            out.append("\r\n");
        else
            out.push_back(ch);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MStringView CppTypeEmitter::MapBuiltinCppType(const MStringView typeName)noexcept{
    if(IsScalarType(typeName))
        return typeName;
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(typeName))
        return objInfo->cppExportName;
    if(const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(typeName))
        return unalignedInfo->cppExportName;
    if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(typeName))
        return genericInfo->cppExportName;
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(typeName))
        return vecInfo->cppExportName;
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(typeName))
        return matInfo->cppExportName;
    return {};
}

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
    if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(name); genericInfo && (genericInfo->inlineLayoutKind == InlineLayoutKind::Expected || genericInfo->inlineLayoutKind == InlineLayoutKind::Optional) && !type.templateArgs.empty() && type.templateArgs[0].type){
        const MStringView valBase = m_symbols.resolveBaseTypeName(type.templateArgs[0].type->name);
        if(const BuiltinSimdVectorInfo* simdVecInfo = FindBuiltinSimdVectorType(valBase); simdVecInfo && simdVecInfo->cppExportName == simdVecInfo->regCppType){
            const MStringView cppGenericName = !genericInfo->cppExportName.empty() ? genericInfo->cppExportName : name;
            out.append(cppGenericName.data(), cppGenericName.size());
            out.append(simdVecInfo->isDoubleReg ? "<::SIMDVectorDoubleResult" : "<::SIMDVectorSingleResult");
            for(usize i = 1u; i < type.templateArgs.size(); ++i){
                out.append(", ");
                const TypeArg& arg = type.templateArgs[i];
                if(arg.type)
                    emitTypeCpp(out, *arg.type);
                else if(!arg.constantText.empty())
                    out.append(arg.constantText.data(), arg.constantText.size());
            }
            out.push_back('>');
            return;
        }
    }
    if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(name); genericInfo && genericInfo->isBorrowView && type.templateArgs.size() == 1u && type.templateArgs[0].type){
        const MStringView cppViewName = !genericInfo->cppExportName.empty() ? genericInfo->cppExportName : name;
        out.append(cppViewName.data(), cppViewName.size());
        out.append("<const ");
        emitTypeCpp(out, *type.templateArgs[0].type);
        out.push_back('>');
        return;
    }
    if(const MStringView mappedBuiltin = MapBuiltinCppType(name); !mappedBuiltin.empty()){
        out.append(mappedBuiltin.data(), mappedBuiltin.size());
        if(!type.templateArgs.empty())
            emitTypeArgListCpp(out, type.templateArgs);
        return;
    }

    if(const EnumDecl* enumDecl = m_symbols.findEnum(name)){
        out.append(name.data(), name.size());
        out.append(enumDecl->isFlag ? "::Flag" : "::Enum");
        return;
    }

    out.append(name.data(), name.size());
    if(!type.templateArgs.empty())
        emitTypeArgListCpp(out, type.templateArgs);
}

void CppTypeEmitter::emitTypeArgListCpp(MString& out, const Span<const TypeArg> args)const{
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

void CppTypeEmitter::emitParamTypeCpp(MString& out, const ParamPassingMode::Enum mode, const TypeRef& type, const b8 topLevelConst)const{
    if(mode == ParamPassingMode::Mut){
        emitTypeCpp(out, type);
        out.push_back('&');
        return;
    }

    if(m_symbols.isSmallValueType(type.name) && (type.templateArgs.empty() || IsBorrowViewType(m_symbols.resolveBaseTypeName(type.name))) && !type.isFuncType){
        if(topLevelConst)
            out.append("const ");
        emitTypeCpp(out, type);
        return;
    }

    out.append("const ");
    emitTypeCpp(out, type);
    out.push_back('&');
}

void CppTypeEmitter::emitParamListCpp(MString& out, const Span<const ParamDecl> params)const{
    out.push_back('(');
    for(usize i = 0u; i < params.size(); ++i){
        if(i > 0u)
            out.append(", ");
        const ParamDecl& p = params[i];
        if(p.type)
            emitParamTypeCpp(out, p.mode, *p.type);
        out.push_back(' ');
        out.append(p.name.data(), p.name.size());
    }
    out.push_back(')');
}

void CppTypeEmitter::emitTemplateHeaderCpp(MString& out, const Span<const TemplateParamDecl> params)const{
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

void CppTypeEmitter::emitMethodSignature(MString& out, const ClassDecl* ownerClass, const MethodDecl& method, const b8 isDeclarationInClass)const{
    if(!method.isConstructor && !method.isDestructor){
        if(isDeclarationInClass && method.isOverride)
            out.append("virtual ");
        if(method.returnType)
            emitTypeCpp(out, *method.returnType);
        else
            out.append("void");
        out.push_back(' ');
    }

    if(!isDeclarationInClass && ownerClass){
        out.append(ownerClass->name.data(), ownerClass->name.size());
        if(!ownerClass->templateParams.empty()){
            out.push_back('<');
            for(usize i = 0u; i < ownerClass->templateParams.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                out.append(ownerClass->templateParams[i].name.data(), ownerClass->templateParams[i].name.size());
            }
            out.push_back('>');
        }
        out.append("::");
    }

    if(method.isDestructor)
        out.push_back('~');
    if(method.isOperator)
        out.append("operator");
    out.append(method.name.data(), method.name.size());

    emitParamListCpp(out, method.params);

    if(method.isConst)
        out.append("const noexcept");
    else
        out.append("noexcept");
    if(isDeclarationInClass && method.isOverride)
        out.append(" override");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

