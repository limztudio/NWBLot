// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>
#include <core/metascript/adhd/sema/type_layout_analyzer.h>

#include "cpp_type_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_cpp_type_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsSynthesizedArenaParamCandidate(const ParamDecl& p, const SymbolTable& symbols)noexcept{
    if(!p.type || p.type->isAuto || p.type->isFuncType)
        return false;
    if(symbols.resolveRefMode(p.type) != ParamPassingMode::None)
        return false;
    const TypeRef* unwrapped = symbols.unwrapTypeAlias(p.type);
    return unwrapped && unwrapped->refMode == ParamPassingMode::None && !symbols.hasTypeSymbol(unwrapped->name) && SymbolTable::IsArenaBackedContainer(unwrapped->name);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MStringView CppTypeEmitter::MapBuiltinCppType(const MStringView typeName)noexcept{
    if(BuiltinTypeSemantics::IsScalarType(typeName))
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
    if(const BuiltinEnumInfo* enumInfo = FindBuiltinEnum(typeName))
        return enumInfo->cppTypeName;
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
        const b8 injectsCallerArena = type.funcReturnType
            && m_symbols.resolveRefMode(type.funcReturnType) == ParamPassingMode::None
            && TypeLayoutAnalyzer(m_symbols).requiresMotherArena(*type.funcReturnType);
        out.append("::InplaceFunction<");
        AppendUnsignedDecimal(out, type.inferredCaptureBytes);
        out.append("u, ");
        if(type.funcReturnType)
            emitTypeCpp(out, *type.funcReturnType);
        else
            out.append("void");
        out.push_back('(');
        if(injectsCallerArena){
            out.append("::ArenaBase&");
            if(!type.funcParams.empty())
                out.append(", ");
        }
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
    if(const EnumDecl* enumDecl = m_symbols.findEnum(name)){
        out.append(name.data(), name.size());
        out.append(enumDecl->isFlag ? "::Flag" : "::Enum");
        return;
    }
    if(m_symbols.hasTypeSymbol(name)){
        out.append(name.data(), name.size());
        if(!type.templateArgs.empty())
            emitTypeArgListCpp(out, type.templateArgs);
        return;
    }
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
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(name); objInfo && objInfo->isArenaBacked){
        emitTypeWithCustomArenaCpp(out, type, "::ArenaBase");
        return;
    }
    if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(name); genericInfo && genericInfo->isArenaBacked){
        emitTypeWithCustomArenaCpp(out, type, "::ArenaBase");
        return;
    }
    if(const MStringView mappedBuiltin = MapBuiltinCppType(name); !mappedBuiltin.empty()){
        out.append(mappedBuiltin.data(), mappedBuiltin.size());
        if(!type.templateArgs.empty())
            emitTypeArgListCpp(out, type.templateArgs);
        return;
    }

    out.append(name.data(), name.size());
    if(!type.templateArgs.empty())
        emitTypeArgListCpp(out, type.templateArgs);
}

void CppTypeEmitter::emitTypeWithCustomArenaCpp(MString& out, const TypeRef& type, const MStringView arenaExpr)const{
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    const TypeRef& target = unwrapped ? *unwrapped : type;
    const MStringView name = target.name;
    if(!m_symbols.hasTypeSymbol(name)){
        if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(name); objInfo && objInfo->isArenaBacked){
            const MStringView cppObjName = !objInfo->cppExportName.empty() ? objInfo->cppExportName : name;
            out.append(cppObjName.data(), cppObjName.size());
            out.push_back('<');
            out.append(arenaExpr.data(), arenaExpr.size());
            out.push_back('>');
            return;
        }
        if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(name); genericInfo && genericInfo->isArenaBacked){
            const MStringView cppGenericName = !genericInfo->cppExportName.empty() ? genericInfo->cppExportName : name;
            out.append(cppGenericName.data(), cppGenericName.size());
            out.push_back('<');
            if(genericInfo->emitsFirstTypeArgOnlyInCpp){
                if(!target.templateArgs.empty() && target.templateArgs[0].type){
                    emitTypeCpp(out, *target.templateArgs[0].type);
                    out.append(", ");
                }
                out.append(arenaExpr.data(), arenaExpr.size());
            }
            else{
                for(usize i = 0u; i < target.templateArgs.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    const TypeArg& arg = target.templateArgs[i];
                    if(arg.type)
                        emitTypeCpp(out, *arg.type);
                    else if(!arg.constantText.empty())
                        out.append(arg.constantText.data(), arg.constantText.size());
                }
                if(!target.templateArgs.empty())
                    out.append(", ");
                out.append(arenaExpr.data(), arenaExpr.size());
            }
            out.push_back('>');
            return;
        }
    }
    emitTypeCpp(out, type);
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
        if(!m_symbols.hasTypeSymbol(type.name)){
            if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(type.name); genericInfo && genericInfo->isBorrowView && type.templateArgs.size() == 1u && type.templateArgs[0].type){
                if(topLevelConst)
                    out.append("const ");
                const MStringView cppViewName = !genericInfo->cppExportName.empty() ? genericInfo->cppExportName : MStringView(type.name);
                out.append(cppViewName.data(), cppViewName.size());
                out.push_back('<');
                emitTypeCpp(out, *type.templateArgs[0].type);
                out.push_back('>');
                return;
            }
        }
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

void CppTypeEmitter::emitParamListCpp(MString& out, const Span<const ParamDecl> params, const b8 injectCallerArena)const{
    out.push_back('(');
    b8 first = true;
    if(injectCallerArena){
        out.append("::ArenaBase& __adhd_caller_arena");
        first = false;
    }
    for(usize i = 0u; i < params.size(); ++i){
        if(!first)
            out.append(", ");
        first = false;
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

void CppTypeEmitter::emitCallableTemplateHeaderCpp(MString& out, const MethodDecl& callable, const b8 includeDefaults, const MStringView indent)const{
    usize synthCount = 0u;
    if(callable.hasSynthesizedArenaParams){
        for(const ParamDecl& p : callable.params){
            if(__hidden_adhd_cpp_type_emitter::IsSynthesizedArenaParamCandidate(p, m_symbols))
                ++synthCount;
        }
    }
    if(callable.templateParams.empty() && synthCount == 0u)
        return;

    out.append(indent.data(), indent.size());
    out.append("template<");
    b8 first = true;
    for(usize i = 0u; i < callable.templateParams.size(); ++i){
        if(!first)
            out.append(", ");
        first = false;
        const TemplateParamDecl& p = callable.templateParams[i];
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
    for(usize k = 0u; k < synthCount; ++k){
        if(!first)
            out.append(", ");
        first = false;
        out.append("typename __AdhdArena");
        AppendUnsignedDecimal(out, k);
        if(includeDefaults)
            out.append(" = ::ArenaBase");
    }
    out.append(">\r\n");
}

void CppTypeEmitter::emitMethodSignature(MString& out, const ClassDecl* ownerClass, const MethodDecl& method, const b8 isDeclarationInClass)const{
    emitMethodSignature(out, ownerClass, method, isDeclarationInClass, ownerClass ? ownerClass->needsClassArena : false);
}

void CppTypeEmitter::emitMethodSignature(MString& out, const ClassDecl* ownerClass, const MethodDecl& method, const b8 isDeclarationInClass, const b8 ownerNeedsMotherArena)const{
    const b8 isOverrideDestructor = method.isDestructor && ownerClass && !ownerClass->implementedInterfaces.empty();
    const b8 prependMotherArena = method.isConstructor
        && ownerClass
        && !ownerClass->isArenaOwner
        && !ownerClass->isSerialize
        && ownerClass->subsystemTypeName.empty()
        && ownerClass->componentAssetTypeName.empty()
        && ownerNeedsMotherArena;
    const b8 prependCallerArena = !method.isConstructor && !method.isDestructor && method.injectsCallerArena;

    if(!method.isConstructor && !method.isDestructor){
        if(isDeclarationInClass && method.isOverride)
            out.append("virtual ");
        if(method.returnType)
            emitTypeCpp(out, *method.returnType);
        else
            out.append("void");
        out.push_back(' ');
    }
    else if(isDeclarationInClass && isOverrideDestructor){
        out.append("virtual ");
    }
    else if(isDeclarationInClass && prependMotherArena && method.params.empty()){
        out.append("explicit ");
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

    out.push_back('(');
    b8 firstParam = true;
    if(prependMotherArena){
        out.append("::ArenaBase& __adhd_mother_arena");
        firstParam = false;
    }
    else if(prependCallerArena){
        out.append("::ArenaBase& __adhd_caller_arena");
        firstParam = false;
    }
    usize synthIdx = 0u;
    for(usize i = 0u; i < method.params.size(); ++i){
        if(!firstParam)
            out.append(", ");
        firstParam = false;
        const ParamDecl& p = method.params[i];
        if(p.type){
            if(method.hasSynthesizedArenaParams && __hidden_adhd_cpp_type_emitter::IsSynthesizedArenaParamCandidate(p, m_symbols)){
                char arenaNameBuf[32];
                usize pos = 0u;
                constexpr MStringView prefix = "__AdhdArena";
                for(char ch : prefix)
                    arenaNameBuf[pos++] = ch;
                char digits[16];
                usize digitCount = 0u;
                usize v = synthIdx++;
                do{
                    digits[digitCount++] = static_cast<char>('0' + (v % 10u));
                    v /= 10u;
                }while(v > 0u);
                while(digitCount > 0u)
                    arenaNameBuf[pos++] = digits[--digitCount];
                const MStringView arenaNameView(arenaNameBuf, pos);
                if(p.mode == ParamPassingMode::Mut){
                    emitTypeWithCustomArenaCpp(out, *p.type, arenaNameView);
                    out.push_back('&');
                }
                else{
                    out.append("const ");
                    emitTypeWithCustomArenaCpp(out, *p.type, arenaNameView);
                    out.push_back('&');
                }
            }
            else{
                emitParamTypeCpp(out, p.mode, *p.type);
            }
        }
        out.push_back(' ');
        out.append(p.name.data(), p.name.size());
    }
    out.push_back(')');

    if(method.isConst)
        out.append("const noexcept");
    else
        out.append("noexcept");
    if(isDeclarationInClass && (method.isOverride || isOverrideDestructor))
        out.append(" override");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

