// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "symbol_table.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_symbol_table{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TypeRef* SubstituteAliasTypeRef(
    const TypeRef& origType,
    const MVector<TemplateParamDecl>& templateParams,
    const MVector<TypeArg>& templateArgs,
    MetaArena& arena
){
    const usize count = templateParams.size() < templateArgs.size() ? templateParams.size() : templateArgs.size();
    for(usize p = 0u; p < count; ++p){
        if(!templateParams[p].constValueType && origType.name == templateParams[p].name && origType.templateArgs.empty() && templateArgs[p].type){
            if(origType.refMode != ParamPassingMode::None && templateArgs[p].type->refMode == ParamPassingMode::None){
                TypeRef* withMode = AllocateAstNode<TypeRef>(arena);
                *withMode = *templateArgs[p].type;
                withMode->refMode = origType.refMode;
                return withMode;
            }
            return templateArgs[p].type;
        }
    }

    if(origType.templateArgs.empty() && !origType.isFuncType)
        return const_cast<TypeRef*>(&origType);

    TypeRef* cloned = AllocateAstNode<TypeRef>(arena);
    cloned->name = origType.name;
    cloned->refMode = origType.refMode;
    cloned->funcReturnType = origType.funcReturnType
        ? SubstituteAliasTypeRef(*origType.funcReturnType, templateParams, templateArgs, arena)
        : nullptr;
    cloned->inferredCaptureBytes = origType.inferredCaptureBytes;
    cloned->span = origType.span;
    cloned->isFuncType = origType.isFuncType;
    cloned->isAuto = origType.isAuto;
    for(const FuncParamType& fp : origType.funcParams){
        FuncParamType subFp = fp;
        if(fp.type)
            subFp.type = SubstituteAliasTypeRef(*fp.type, templateParams, templateArgs, arena);
        cloned->funcParams.push_back(subFp);
    }
    for(const TypeArg& arg : origType.templateArgs){
        TypeArg subArg = arg;
        if(arg.type){
            bool matchedConstParam = false;
            for(usize p = 0u; p < count; ++p){
                if(templateParams[p].constValueType && arg.type->name == templateParams[p].name){
                    subArg.type = nullptr;
                    subArg.constantText = templateArgs[p].type ? templateArgs[p].type->name : templateArgs[p].constantText;
                    matchedConstParam = true;
                    break;
                }
            }
            if(!matchedConstParam)
                subArg.type = SubstituteAliasTypeRef(*arg.type, templateParams, templateArgs, arena);
        }
        else if(!arg.constantText.empty()){
            for(usize p = 0u; p < count; ++p){
                if(templateParams[p].constValueType && arg.constantText == templateParams[p].name){
                    subArg.constantText = templateArgs[p].type ? templateArgs[p].type->name : templateArgs[p].constantText;
                    break;
                }
            }
        }
        cloned->templateArgs.push_back(subArg);
    }
    return cloned;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SymbolTable::build(TranslationUnit& unit, MVector<ParseError>& errors){
    bool ok = true;

    for(EnumDecl* decl : unit.enums){
        if(hasTypeSymbol(decl->name)){
            ReportAdhdDiagnostic(m_arena, errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_enums.push_back(decl);
    }

    for(InterfaceDecl* decl : unit.interfaces){
        if(hasTypeSymbol(decl->name)){
            ReportAdhdDiagnostic(m_arena, errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_interfaces.push_back(decl);
    }

    for(ClassDecl* decl : unit.classes){
        if(hasTypeSymbol(decl->name)){
            ReportAdhdDiagnostic(m_arena, errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_classes.push_back(decl);
    }

    for(TypeAliasDecl* decl : unit.typeAliases){
        if(hasTypeSymbol(decl->name)){
            ReportAdhdDiagnostic(m_arena, errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_typeAliases.push_back(decl);
    }

    for(MethodDecl* fn : unit.freeFunctions)
        m_freeFunctions.push_back(fn);

    for(Stmt* c : unit.globalConstants){
        if(findGlobalConstant(c->name)){
            ReportAdhdDiagnostic(m_arena, errors, c->span, "duplicate file-level 'const' name");
            ok = false;
            continue;
        }
        m_globalConstants.push_back(c);
    }

    return ok;
}

EnumDecl* SymbolTable::findEnum(const MStringView name)const noexcept{
    for(EnumDecl* decl : m_enums){
        if(decl->name == name)
            return decl;
    }
    return nullptr;
}

InterfaceDecl* SymbolTable::findInterface(const MStringView name)const noexcept{
    for(InterfaceDecl* decl : m_interfaces){
        if(decl->name == name)
            return decl;
    }
    return nullptr;
}

ClassDecl* SymbolTable::findClass(const MStringView name)const noexcept{
    for(ClassDecl* decl : m_classes){
        if(decl->name == name)
            return decl;
    }
    return nullptr;
}

TypeAliasDecl* SymbolTable::findTypeAlias(const MStringView name)const noexcept{
    for(TypeAliasDecl* decl : m_typeAliases){
        if(decl->name == name)
            return decl;
    }
    return nullptr;
}

MethodDecl* SymbolTable::findFreeFunction(const MStringView name, const usize* preferredArgCount)const noexcept{
    MethodDecl* firstMatch = nullptr;
    for(MethodDecl* fn : m_freeFunctions){
        if(fn->name == name){
            if(!preferredArgCount || fn->params.size() == *preferredArgCount)
                return fn;
            if(!firstMatch)
                firstMatch = fn;
        }
    }
    return firstMatch;
}

Stmt* SymbolTable::findGlobalConstant(const MStringView name)const noexcept{
    for(Stmt* c : m_globalConstants){
        if(c->name == name)
            return c;
    }
    return nullptr;
}

MethodDecl* SymbolTable::findInterfaceMethod(const TypeRef& ifaceType, const MStringView methodName, const usize* preferredArgCount, const u32 depth)const noexcept{
    if(depth > 16u)
        return nullptr;
    const TypeRef* resolvedIface = unwrapTypeAlias(&ifaceType);
    if(!resolvedIface)
        return nullptr;
    const InterfaceDecl* iface = findInterface(resolvedIface->name);
    if(!iface)
        return nullptr;

    const bool hasSubst = !iface->templateParams.empty() && !resolvedIface->templateArgs.empty();
    auto substituteMethodIfNeeded = [&](MethodDecl* method) -> MethodDecl*{
        if(!method || !hasSubst)
            return method;
        MethodDecl* cloned = AllocateAstNode<MethodDecl>(m_arena);
        cloned->access = method->access;
        cloned->name = method->name;
        cloned->returnType = method->returnType
            ? substituteTypeParams(*method->returnType, iface->templateParams, resolvedIface->templateArgs)
            : nullptr;
        cloned->templateParams = method->templateParams;
        for(const ParamDecl& p : method->params){
            ParamDecl subP = p;
            if(p.type)
                subP.type = substituteTypeParams(*p.type, iface->templateParams, resolvedIface->templateArgs);
            cloned->params.push_back(subP);
        }
        cloned->span = method->span;
        cloned->isConstructor = method->isConstructor;
        cloned->isDestructor = method->isDestructor;
        cloned->isOperator = method->isOperator;
        cloned->isConst = method->isConst;
        cloned->isOverride = method->isOverride;
        cloned->isInline = method->isInline;
        cloned->isNoInline = method->isNoInline;
        cloned->isReflect = method->isReflect;
        return cloned;
    };

    MethodDecl* firstMatch = nullptr;
    for(MethodDecl* method : iface->methods){
        if(method->name == methodName){
            if(!preferredArgCount || method->params.size() == *preferredArgCount)
                return substituteMethodIfNeeded(method);
            if(!firstMatch)
                firstMatch = substituteMethodIfNeeded(method);
        }
    }

    for(const TypeRef* baseType : iface->baseInterfaces){
        if(!baseType)
            continue;
        const TypeRef* subBase = hasSubst
            ? substituteTypeParams(*baseType, iface->templateParams, resolvedIface->templateArgs)
            : baseType;
        if(!subBase)
            continue;
        MethodDecl* inherited = findInterfaceMethod(*subBase, methodName, preferredArgCount, depth + 1u);
        if(inherited){
            if(!preferredArgCount || inherited->params.size() == *preferredArgCount)
                return inherited;
            if(!firstMatch)
                firstMatch = inherited;
        }
    }

    return firstMatch;
}

void SymbolTable::collectAllInterfaceMethods(const TypeRef& ifaceType, MVector<MethodDecl*>& outMethods, const u32 depth)const{
    if(depth > 16u)
        return;
    const TypeRef* resolvedIface = unwrapTypeAlias(&ifaceType);
    if(!resolvedIface)
        return;
    const InterfaceDecl* iface = findInterface(resolvedIface->name);
    if(!iface)
        return;

    const bool hasSubst = !iface->templateParams.empty() && !resolvedIface->templateArgs.empty();
    for(const TypeRef* baseType : iface->baseInterfaces){
        if(!baseType)
            continue;
        const TypeRef* subBase = hasSubst
            ? substituteTypeParams(*baseType, iface->templateParams, resolvedIface->templateArgs)
            : baseType;
        if(subBase)
            collectAllInterfaceMethods(*subBase, outMethods, depth + 1u);
    }

    for(MethodDecl* method : iface->methods){
        if(!method)
            continue;
        if(!hasSubst){
            outMethods.push_back(method);
            continue;
        }
        MethodDecl* cloned = AllocateAstNode<MethodDecl>(m_arena);
        cloned->access = method->access;
        cloned->name = method->name;
        cloned->returnType = method->returnType
            ? substituteTypeParams(*method->returnType, iface->templateParams, resolvedIface->templateArgs)
            : nullptr;
        cloned->templateParams = method->templateParams;
        for(const ParamDecl& p : method->params){
            ParamDecl subP = p;
            if(p.type)
                subP.type = substituteTypeParams(*p.type, iface->templateParams, resolvedIface->templateArgs);
            cloned->params.push_back(subP);
        }
        cloned->span = method->span;
        cloned->isConstructor = method->isConstructor;
        cloned->isDestructor = method->isDestructor;
        cloned->isOperator = method->isOperator;
        cloned->isConst = method->isConst;
        cloned->isOverride = method->isOverride;
        cloned->isInline = method->isInline;
        cloned->isNoInline = method->isNoInline;
        cloned->isReflect = method->isReflect;
        outMethods.push_back(cloned);
    }
}

const TypeRef* SymbolTable::unwrapTypeAlias(const TypeRef* type)const noexcept{
    if(!type)
        return nullptr;
    ParamPassingMode::Enum outerRefMode = type->refMode;
    const TypeRef* cur = type;
    for(u32 depth = 0u; cur && !cur->isFuncType && !cur->isAuto && depth < 16u; ++depth){
        const TypeAliasDecl* aliasDecl = findTypeAlias(cur->name);
        if(!aliasDecl || !aliasDecl->aliasedType)
            break;
        if(!aliasDecl->templateParams.empty() && !cur->templateArgs.empty()){
            cur = substituteTypeParams(
                *aliasDecl->aliasedType,
                aliasDecl->templateParams,
                cur->templateArgs
            );
        }
        else{
            cur = aliasDecl->aliasedType;
        }
        if(cur && outerRefMode == ParamPassingMode::None && cur->refMode != ParamPassingMode::None)
            outerRefMode = cur->refMode;
    }
    if(cur && outerRefMode != ParamPassingMode::None && cur->refMode != outerRefMode){
        TypeRef* withMode = AllocateAstNode<TypeRef>(m_arena);
        *withMode = *cur;
        withMode->refMode = outerRefMode;
        return withMode;
    }
    return cur;
}

ParamPassingMode::Enum SymbolTable::resolveRefMode(const TypeRef* type)const noexcept{
    if(!type)
        return ParamPassingMode::None;
    if(type->refMode != ParamPassingMode::None)
        return type->refMode;
    const TypeRef* unwrapped = unwrapTypeAlias(type);
    return unwrapped ? unwrapped->refMode : ParamPassingMode::None;
}

TypeRef* SymbolTable::substituteTypeParams(
    const TypeRef& origType,
    const MVector<TemplateParamDecl>& templateParams,
    const MVector<TypeArg>& templateArgs
)const{
    return __hidden_adhd_symbol_table::SubstituteAliasTypeRef(origType, templateParams, templateArgs, m_arena);
}

MStringView SymbolTable::resolveBaseTypeName(MStringView typeName)const noexcept{
    for(u32 depth = 0u; !typeName.empty() && depth < 16u; ++depth){
        const TypeAliasDecl* aliasDecl = findTypeAlias(typeName);
        if(!aliasDecl || !aliasDecl->aliasedType || aliasDecl->aliasedType->isFuncType)
            break;
        typeName = aliasDecl->aliasedType->name;
    }
    return typeName;
}

bool SymbolTable::isSelfDefaultedType(const MStringView typeName)const noexcept{
    const MStringView baseName = resolveBaseTypeName(typeName);
    if(const ClassDecl* cls = findClass(baseName)){
        bool hasAnyCtor = false;
        for(const MethodDecl* m : cls->methods){
            if(m && m->isConstructor){
                hasAnyCtor = true;
                if(m->params.empty())
                    return true;
            }
        }
        return !hasAnyCtor;
    }
    return isArenaBackedContainer(baseName)
        || findEnum(baseName) != nullptr
        || baseName == "Optional"
        || baseName == "FixedVector"
        || baseName == "Array"
        || baseName == "AssetRef"
        || baseName == "CompactString"
        || baseName == "Path";
}

bool SymbolTable::isArenaBackedContainer(const MStringView typeName)noexcept{
    return typeName == "Vector"
        || typeName == "ParallelVector"
        || typeName == "ForwardList"
        || typeName == "List"
        || typeName == "Deque"
        || typeName == "Set"
        || typeName == "HashSet"
        || typeName == "ParallelHashSet"
        || typeName == "Map"
        || typeName == "HashMap"
        || typeName == "ParallelHashMap"
        || typeName == "Queue"
        || typeName == "ParallelQueue"
        || typeName == "ParallelBlockQueue"
        || typeName == "String";
}

bool SymbolTable::isSimdVectorType(const MStringView typeName, u32& outLanes)noexcept{
    if(typeName == "f64x2"){
        outLanes = 2u;
        return true;
    }
    if(typeName == "f64x3"){
        outLanes = 3u;
        return true;
    }
    if(typeName == "f32x4" || typeName == "i32x4" || typeName == "u32x4"
        || typeName == "f32x3_i32" || typeName == "f32x3_u32"
        || typeName == "f64x4" || typeName == "i64x4" || typeName == "u64x4"
        || typeName == "f64x3_i64" || typeName == "f64x3_u64")
    {
        outLanes = 4u;
        return true;
    }
    return false;
}

bool SymbolTable::isSimdMatrixType(const MStringView typeName)noexcept{
    return typeName == "f32x3x4"
        || typeName == "f32x4x4"
        || typeName == "f64x3x3"
        || typeName == "f64x3x4"
        || typeName == "f64x4x4";
}

bool SymbolTable::isSimdType(const MStringView typeName)noexcept{
    u32 lanes = 0u;
    return isSimdVectorType(typeName, lanes) || isSimdMatrixType(typeName);
}

bool SymbolTable::isStringSwitchType(const MStringView typeName)noexcept{
    return typeName == "String"
        || typeName == "StringView"
        || typeName == "CompactString"
        || typeName == "Name";
}

MStringView SymbolTable::mapBuiltinCppType(const MStringView typeName)noexcept{
    if(typeName == "String") return "AString";
    if(typeName == "StringView") return "AStringView";
    if(typeName == "CompactString") return "ACompactString";
    if(typeName == "AssetRef") return "::NWB::Core::Assets::AssetRef";
    if(typeName == "f32x4") return "Float4";
    if(typeName == "i32x4") return "Int4";
    if(typeName == "u32x4") return "UInt4";
    if(typeName == "f32x3_i32") return "Float3Int";
    if(typeName == "f32x3_u32") return "Float3UInt";
    if(typeName == "f32x3x4") return "Float34";
    if(typeName == "f32x4x4") return "Float44";
    if(typeName == "f64x2") return "Double2";
    if(typeName == "f64x3") return "Double3";
    if(typeName == "f64x4") return "Double4";
    if(typeName == "i64x4") return "Int64x4";
    if(typeName == "u64x4") return "UInt64x4";
    if(typeName == "f64x3_i64") return "Double3Int";
    if(typeName == "f64x3_u64") return "Double3UInt";
    if(typeName == "f64x3x3") return "Double33";
    if(typeName == "f64x3x4") return "Double34";
    if(typeName == "f64x4x4") return "Double44";
    return {};
}

bool SymbolTable::hasTypeSymbol(const MStringView name)const noexcept{
    return findEnum(name) != nullptr
        || findInterface(name) != nullptr
        || findClass(name) != nullptr
        || findTypeAlias(name) != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

