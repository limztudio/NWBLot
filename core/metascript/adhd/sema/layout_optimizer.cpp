// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layout_optimizer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_layout_optimizer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr usize AlignUp(const usize offset, const usize alignment)noexcept{
    if(alignment <= 1u)
        return offset;
    return (offset + alignment - 1u) & ~(alignment - 1u);
}

[[nodiscard]] usize ParseUnsignedConstant(const MStringView text, const SymbolTable& symbols)noexcept{
    if(text.empty())
        return 1u;

    if(const Stmt* globalConst = symbols.findGlobalConstant(text)){
        if(globalConst->expr && globalConst->expr->kind == ExprKind::IntegerLiteral)
            return ParseUnsignedConstant(globalConst->expr->text, symbols);
        if(globalConst->isCtorInit && globalConst->arguments.size() == 1u && globalConst->arguments[0] && globalConst->arguments[0]->kind == ExprKind::IntegerLiteral)
            return ParseUnsignedConstant(globalConst->arguments[0]->text, symbols);
    }

    usize val = 0u;
    if(text.size() > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')){
        for(usize i = 2u; i < text.size(); ++i){
            const MChar c = text[i];
            if(c >= '0' && c <= '9')
                val = val * 16u + static_cast<usize>(c - '0');
            else if(c >= 'a' && c <= 'f')
                val = val * 16u + static_cast<usize>(10 + (c - 'a'));
            else if(c >= 'A' && c <= 'F')
                val = val * 16u + static_cast<usize>(10 + (c - 'A'));
            else
                break;
        }
        return val > 0u ? val : 1u;
    }
    if(text.size() > 2u && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')){
        for(usize i = 2u; i < text.size(); ++i){
            const MChar c = text[i];
            if(c == '0' || c == '1')
                val = val * 2u + static_cast<usize>(c - '0');
            else
                break;
        }
        return val > 0u ? val : 1u;
    }
    for(usize i = 0u; i < text.size(); ++i){
        const MChar c = text[i];
        if(c >= '0' && c <= '9')
            val = val * 10u + static_cast<usize>(c - '0');
        else
            break;
    }
    return val > 0u ? val : 1u;
}

[[nodiscard]] bool AreTypeRefsEqual(const TypeRef* a, const TypeRef* b, const SymbolTable& symbols)noexcept;

[[nodiscard]] bool AreTemplateArgListsEqual(const MVector<TypeArg>& a, const MVector<TypeArg>& b, const SymbolTable& symbols)noexcept{
    if(a.size() != b.size())
        return false;
    for(usize i = 0u; i < a.size(); ++i){
        if(a[i].type && b[i].type){
            if(!AreTypeRefsEqual(a[i].type, b[i].type, symbols))
                return false;
        }
        else{
            const MStringView lhsName = a[i].type ? a[i].type->name : a[i].constantText;
            const MStringView rhsName = b[i].type ? b[i].type->name : b[i].constantText;
            if(lhsName != rhsName)
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool AreTypeRefsEqual(const TypeRef* a, const TypeRef* b, const SymbolTable& symbols)noexcept{
    if(!a || !b)
        return a == b;
    if(a->refMode != b->refMode)
        return false;

    const TypeRef* ua = symbols.unwrapTypeAlias(a);
    const TypeRef* ub = symbols.unwrapTypeAlias(b);
    if(!ua || !ub)
        return ua == ub;
    if(ua->isFuncType != ub->isFuncType || ua->isAuto != ub->isAuto)
        return false;
    if(ua->isFuncType){
        if(ua->inferredCaptureBytes != ub->inferredCaptureBytes || ua->funcParams.size() != ub->funcParams.size())
            return false;
        if(!AreTypeRefsEqual(ua->funcReturnType, ub->funcReturnType, symbols))
            return false;
        for(usize i = 0u; i < ua->funcParams.size(); ++i){
            if(ua->funcParams[i].mode != ub->funcParams[i].mode)
                return false;
            if(!AreTypeRefsEqual(ua->funcParams[i].type, ub->funcParams[i].type, symbols))
                return false;
        }
        return true;
    }
    if(ua->name != ub->name)
        return false;
    return AreTemplateArgListsEqual(ua->templateArgs, ub->templateArgs, symbols);
}

[[nodiscard]] bool IsKnownConcreteTypeName(const MStringView name, const SymbolTable& symbols)noexcept{
    if(name == "bool" || name == "i8" || name == "u8"
        || name == "i16" || name == "u16"
        || name == "i32" || name == "u32"
        || name == "i64" || name == "u64"
        || name == "isize" || name == "usize"
        || name == "f32" || name == "f64" || name == "void"
        || name == "Name" || name == "Path" || name == "String" || name == "StringView" || name == "CompactString"
        || name == "Span" || name == "Optional" || name == "Expected" || name == "AssetRef" || name == "Atomic"
        || name == "Array" || name == "FixedVector")
    {
        return true;
    }
    return SymbolTable::isSimdType(name)
        || SymbolTable::isArenaBackedContainer(name)
        || symbols.findEnum(name) != nullptr
        || symbols.findInterface(name) != nullptr
        || symbols.findClass(name) != nullptr
        || symbols.findTypeAlias(name) != nullptr;
}

[[nodiscard]] bool IsConcreteTypeRef(const TypeRef& type, const SymbolTable& symbols)noexcept{
    if(type.isAuto)
        return false;
    if(type.isFuncType){
        if(type.funcReturnType && !IsConcreteTypeRef(*type.funcReturnType, symbols))
            return false;
        for(const FuncParamType& fp : type.funcParams){
            if(fp.type && !IsConcreteTypeRef(*fp.type, symbols))
                return false;
        }
        return true;
    }
    if(!IsKnownConcreteTypeName(type.name, symbols))
        return false;
    for(const TypeArg& arg : type.templateArgs){
        if(arg.type){
            const MStringView argName = arg.type->name;
            const bool isNumericConst = !argName.empty() && ((argName[0] >= '0' && argName[0] <= '9') || argName == "true" || argName == "false");
            if(!isNumericConst && !symbols.findGlobalConstant(argName) && !IsConcreteTypeRef(*arg.type, symbols))
                return false;
        }
        else if(!arg.constantText.empty()){
            const MStringView ctext = arg.constantText;
            const bool isNumericConst = (ctext[0] >= '0' && ctext[0] <= '9') || ctext == "true" || ctext == "false";
            if(!isNumericConst && !symbols.findGlobalConstant(ctext))
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsConcreteTemplateArgList(
    const MVector<TemplateParamDecl>& params,
    const MVector<TypeArg>& args,
    const SymbolTable& symbols
)noexcept{
    if(params.size() != args.size())
        return false;
    for(usize i = 0u; i < params.size(); ++i){
        if(params[i].constValueType){
            const MStringView ctext = args[i].type ? args[i].type->name : args[i].constantText;
            if(ctext.empty())
                return false;
            const bool isNumericConst = (ctext[0] >= '0' && ctext[0] <= '9') || ctext == "true" || ctext == "false";
            if(!isNumericConst && !symbols.findGlobalConstant(ctext))
                return false;
        }
        else{
            if(!args[i].type || !IsConcreteTypeRef(*args[i].type, symbols))
                return false;
        }
    }
    return true;
}

void SortAndPackFields(
    MVector<FieldDecl*>& orderedFields,
    const bool isSerialize,
    const bool hasInterfaces,
    usize& outTotalSize,
    usize& outTotalAlign
)noexcept{
    if(!isSerialize){
        for(usize i = 1u; i < orderedFields.size(); ++i){
            FieldDecl* key = orderedFields[i];
            usize j = i;
            while(j > 0u){
                const FieldDecl* prev = orderedFields[j - 1u];
                const bool keyBeforePrev = (key->resolvedAlign > prev->resolvedAlign)
                    || (key->resolvedAlign == prev->resolvedAlign && key->resolvedSize > prev->resolvedSize)
                    || (key->resolvedAlign == prev->resolvedAlign && key->resolvedSize == prev->resolvedSize && key->originalIndex < prev->originalIndex);
                if(!keyBeforePrev)
                    break;
                orderedFields[j] = orderedFields[j - 1u];
                --j;
            }
            orderedFields[j] = key;
        }
    }

    usize currentOffset = hasInterfaces ? 8u : 0u;
    usize maxAlign = isSerialize ? 1u : (hasInterfaces ? 8u : 1u);
    for(FieldDecl* field : orderedFields){
        const usize align = isSerialize ? 1u : field->resolvedAlign;
        if(align > maxAlign)
            maxAlign = align;
        currentOffset = AlignUp(currentOffset, align);
        field->resolvedOffset = currentOffset;
        currentOffset += field->resolvedSize;
    }

    outTotalAlign = maxAlign;
    outTotalSize = currentOffset == 0u ? 1u : AlignUp(currentOffset, maxAlign);
}

[[nodiscard]] bool TypeReferencesClassByValue(
    const TypeRef& type,
    const MStringView targetClassName,
    const SymbolTable& symbols,
    const u32 depth = 0u
)noexcept{
    if(depth > 16u || type.refMode != ParamPassingMode::None)
        return false;
    const TypeRef* unwrapped = symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None || unwrapped->isFuncType)
        return false;
    const MStringView name = unwrapped->name;
    if(name == targetClassName)
        return true;
    if(name == "Array" || name == "FixedVector" || name == "Optional"){
        if(!unwrapped->templateArgs.empty() && unwrapped->templateArgs[0].type)
            return TypeReferencesClassByValue(*unwrapped->templateArgs[0].type, targetClassName, symbols, depth + 1u);
        return false;
    }
    if(const ClassDecl* subCls = symbols.findClass(name)){
        for(const FieldDecl* subField : subCls->fields){
            if(subField && subField->type && TypeReferencesClassByValue(*subField->type, targetClassName, symbols, depth + 1u))
                return true;
        }
    }
    return false;
}

[[nodiscard]] bool TypeContainsArenaBackedContainer(
    const TypeRef& type,
    const SymbolTable& symbols,
    const u32 depth = 0u
)noexcept{
    if(depth >= 8u || type.refMode != ParamPassingMode::None)
        return false;
    const TypeRef* unwrapped = symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None)
        return false;
    if(SymbolTable::isArenaBackedContainer(unwrapped->name))
        return true;
    for(const TypeArg& targ : unwrapped->templateArgs){
        if(targ.type && TypeContainsArenaBackedContainer(*targ.type, symbols, depth + 1u))
            return true;
    }
    return false;
}

[[nodiscard]] bool IsTypeBinarySerializable(
    const TypeRef& type,
    const SymbolTable& symbols,
    MStringView& outReason,
    const u32 depth = 0u
)noexcept{
    if(depth >= 16u)
        return true;

    if(type.refMode != ParamPassingMode::None){
        outReason = "'[serialize]' class cannot contain non-transient 'in'/'mut' reference members";
        return false;
    }

    const TypeRef* unwrapped = symbols.unwrapTypeAlias(&type);
    if(!unwrapped)
        return true;

    if(unwrapped->refMode != ParamPassingMode::None){
        outReason = "'[serialize]' class cannot contain non-transient 'in'/'mut' reference members";
        return false;
    }

    if(unwrapped->isFuncType){
        outReason = "'[serialize]' class cannot contain non-transient dynamic heap, view, or callable members; mark with '[transient]' or use fixed-size types";
        return false;
    }

    const MStringView name = unwrapped->name;
    if(SymbolTable::isArenaBackedContainer(name) || name == "Path" || name == "StringView" || name == "Span"){
        outReason = "'[serialize]' class cannot contain non-transient dynamic heap or view members; mark with '[transient]' or use fixed-size types";
        return false;
    }

    if(name == "Array" || name == "FixedVector" || name == "Optional"){
        if(!unwrapped->templateArgs.empty() && unwrapped->templateArgs[0].type){
            if(!IsTypeBinarySerializable(*unwrapped->templateArgs[0].type, symbols, outReason, depth + 1u))
                return false;
        }
        return true;
    }

    if(const ClassDecl* subCls = symbols.findClass(name)){
        if(!subCls->implementedInterfaces.empty()){
            outReason = "'[serialize]' class cannot contain non-transient polymorphic class members implementing an interface";
            return false;
        }
        for(const FieldDecl* subField : subCls->fields){
            if(subField && !subField->isTransient && subField->type){
                if(!IsTypeBinarySerializable(*subField->type, symbols, outReason, depth + 1u))
                    return false;
            }
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LayoutOptimizer::optimizeTranslationUnit(TranslationUnit& unit, MVector<ParseError>& errors){
    m_unit = &unit;
    bool ok = true;
    for(ClassDecl* classDecl : unit.classes){
        if(!computeClassLayout(*classDecl, errors))
            ok = false;
    }
    if(!ok)
        return false;
    for(const u32 pass : { 0u, 1u }){
        const bool wantImported = (pass == 0u);
        m_collectingImported = wantImported;

        for(const Stmt* c : unit.globalConstants){
            if(!c || c->isImported != wantImported)
                continue;
            if(c->varType)
                collectTemplateInstantiationsInType(*c->varType, unit);
            if(c->expr)
                collectTemplateInstantiationsInExpr(*c->expr, unit);
            for(const Expr* arg : c->arguments){
                if(arg)
                    collectTemplateInstantiationsInExpr(*arg, unit);
            }
        }
        for(const TypeAliasDecl* aliasDecl : unit.typeAliases){
            if(aliasDecl && aliasDecl->isImported == wantImported && aliasDecl->templateParams.empty() && aliasDecl->aliasedType)
                collectTemplateInstantiationsInType(*aliasDecl->aliasedType, unit);
        }
        for(const InterfaceDecl* ifaceDecl : unit.interfaces){
            if(!ifaceDecl || ifaceDecl->isImported != wantImported || !ifaceDecl->templateParams.empty())
                continue;
            for(const TypeRef* baseIface : ifaceDecl->baseInterfaces){
                if(baseIface)
                    collectTemplateInstantiationsInType(*baseIface, unit);
            }
            for(const MethodDecl* m : ifaceDecl->methods){
                if(!m)
                    continue;
                if(m->returnType)
                    collectTemplateInstantiationsInType(*m->returnType, unit);
                for(const ParamDecl& p : m->params){
                    if(p.type)
                        collectTemplateInstantiationsInType(*p.type, unit);
                }
            }
        }
        for(ClassDecl* classDecl : unit.classes){
            if(classDecl->isImported == wantImported){
                for(const TypeRef* implIface : classDecl->implementedInterfaces){
                    if(implIface)
                        collectTemplateInstantiationsInType(*implIface, unit);
                }
                for(const FieldDecl* f : classDecl->fields){
                    if(!f)
                        continue;
                    if(f->type)
                        collectTemplateInstantiationsInType(*f->type, unit);
                    if(f->defaultExpr)
                        collectTemplateInstantiationsInExpr(*f->defaultExpr, unit);
                    for(const Expr* arg : f->ctorArgs){
                        if(arg)
                            collectTemplateInstantiationsInExpr(*arg, unit);
                    }
                }
            }
            for(const MethodDecl* m : classDecl->methods){
                if(!m || m->isImported != wantImported)
                    continue;
                if(m->returnType)
                    collectTemplateInstantiationsInType(*m->returnType, unit);
                for(const ParamDecl& p : m->params){
                    if(p.type)
                        collectTemplateInstantiationsInType(*p.type, unit);
                }
                for(const Stmt* s : m->body){
                    if(s)
                        collectTemplateInstantiationsInStmt(*s, unit);
                }
            }
        }
        for(const MethodDecl* fn : unit.freeFunctions){
            if(!fn || fn->isImported != wantImported)
                continue;
            if(fn->returnType)
                collectTemplateInstantiationsInType(*fn->returnType, unit);
            for(const ParamDecl& p : fn->params){
                if(p.type)
                    collectTemplateInstantiationsInType(*p.type, unit);
            }
            for(const Stmt* s : fn->body){
                if(s)
                    collectTemplateInstantiationsInStmt(*s, unit);
            }
        }
    }
    m_collectingImported = false;
    for(ClassDecl* classDecl : unit.classes){
        if(!computeClassLayout(*classDecl, errors))
            ok = false;
    }
    return ok;
}

void LayoutOptimizer::collectTemplateInstantiationsInStmt(const Stmt& stmt, TranslationUnit& unit){
    if(stmt.varType)
        collectTemplateInstantiationsInType(*stmt.varType, unit);
    if(stmt.expr)
        collectTemplateInstantiationsInExpr(*stmt.expr, unit);
    if(stmt.condition)
        collectTemplateInstantiationsInExpr(*stmt.condition, unit);
    if(stmt.stepExpr)
        collectTemplateInstantiationsInExpr(*stmt.stepExpr, unit);
    for(const Expr* arg : stmt.arguments){
        if(arg)
            collectTemplateInstantiationsInExpr(*arg, unit);
    }
    for(const NativeBinding& nb : stmt.nativeBindings){
        if(nb.sourceExpr)
            collectTemplateInstantiationsInExpr(*nb.sourceExpr, unit);
    }
    if(stmt.initStmt)
        collectTemplateInstantiationsInStmt(*stmt.initStmt, unit);
    for(const Stmt* sub : stmt.statements){
        if(sub)
            collectTemplateInstantiationsInStmt(*sub, unit);
    }
    for(const Stmt* sub : stmt.elseStatements){
        if(sub)
            collectTemplateInstantiationsInStmt(*sub, unit);
    }
    for(const SwitchCase& sc : stmt.switchCases){
        if(sc.matchExpr)
            collectTemplateInstantiationsInExpr(*sc.matchExpr, unit);
        for(const Stmt* sub : sc.statements){
            if(sub)
                collectTemplateInstantiationsInStmt(*sub, unit);
        }
    }
}

void LayoutOptimizer::collectTemplateInstantiationsInExpr(const Expr& expr, TranslationUnit& unit){
    if(expr.targetType)
        collectTemplateInstantiationsInType(*expr.targetType, unit);
    for(const TypeArg& targ : expr.callTemplateArgs){
        if(targ.type)
            collectTemplateInstantiationsInType(*targ.type, unit);
    }
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        if(!expr.callTemplateArgs.empty()){
            if(const ClassDecl* cls = m_symbols.findClass(expr.lhs->text)){
                if(!cls->templateParams.empty() && expr.callTemplateArgs.size() == cls->templateParams.size())
                    instantiateClassTemplate(*cls, expr.callTemplateArgs, unit);
            }
            else if(const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(expr.lhs->text)){
                if(aliasDecl->aliasedType && !aliasDecl->templateParams.empty() && expr.callTemplateArgs.size() == aliasDecl->templateParams.size()){
                    const TypeRef* resolvedAlias = m_symbols.substituteTypeParams(
                        *aliasDecl->aliasedType,
                        aliasDecl->templateParams,
                        expr.callTemplateArgs
                    );
                    collectTemplateInstantiationsInType(*resolvedAlias, unit);
                }
            }
        }
        else if(const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(expr.lhs->text)){
            if(aliasDecl->aliasedType && aliasDecl->templateParams.empty())
                collectTemplateInstantiationsInType(*aliasDecl->aliasedType, unit);
        }
    }
    if(expr.lhs)
        collectTemplateInstantiationsInExpr(*expr.lhs, unit);
    if(expr.rhs)
        collectTemplateInstantiationsInExpr(*expr.rhs, unit);
    for(const Expr* arg : expr.arguments){
        if(arg)
            collectTemplateInstantiationsInExpr(*arg, unit);
    }
    if(expr.lambdaReturnType)
        collectTemplateInstantiationsInType(*expr.lambdaReturnType, unit);
    for(const ParamDecl& p : expr.lambdaParams){
        if(p.type)
            collectTemplateInstantiationsInType(*p.type, unit);
    }
    for(const Stmt* s : expr.lambdaBody){
        if(s)
            collectTemplateInstantiationsInStmt(*s, unit);
    }
}

void LayoutOptimizer::collectTemplateInstantiationsInType(const TypeRef& type, TranslationUnit& unit, const u32 depth){
    if(depth > 16u)
        return;
    if(type.isFuncType){
        if(type.funcReturnType)
            collectTemplateInstantiationsInType(*type.funcReturnType, unit, depth + 1u);
        for(const FuncParamType& fp : type.funcParams){
            if(fp.type)
                collectTemplateInstantiationsInType(*fp.type, unit, depth + 1u);
        }
    }
    for(const TypeArg& arg : type.templateArgs){
        if(arg.type)
            collectTemplateInstantiationsInType(*arg.type, unit, depth + 1u);
    }
    if(const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type)){
        if(unwrapped != &type)
            collectTemplateInstantiationsInType(*unwrapped, unit, depth + 1u);
    }
    if(const ClassDecl* cls = m_symbols.findClass(type.name)){
        if(!cls->templateParams.empty() && type.templateArgs.size() == cls->templateParams.size())
            instantiateClassTemplate(*cls, type.templateArgs, unit);
    }
}

void LayoutOptimizer::instantiateClassTemplate(
    const ClassDecl& templateDecl,
    const MVector<TypeArg>& templateArgs,
    TranslationUnit& unit
){
    if(!__hidden_adhd_layout_optimizer::IsConcreteTemplateArgList(templateDecl.templateParams, templateArgs, m_symbols))
        return;

    for(const ClassTemplateInstantiation* existing : unit.templateInstantiations){
        if(existing->templateDecl == &templateDecl && __hidden_adhd_layout_optimizer::AreTemplateArgListsEqual(existing->templateArgs, templateArgs, m_symbols))
            return;
    }

    ClassTemplateInstantiation* inst = AllocateAstNode<ClassTemplateInstantiation>(m_arena);
    inst->templateDecl = &templateDecl;
    inst->isImported = m_collectingImported;
    for(const TypeArg& targ : templateArgs)
        inst->templateArgs.push_back(targ);

    for(const FieldDecl* origField : templateDecl.fields){
        if(!origField || !origField->type)
            continue;

        FieldDecl* clonedField = AllocateAstNode<FieldDecl>(m_arena);
        clonedField->access = origField->access;
        clonedField->name = origField->name;
        clonedField->defaultExpr = origField->defaultExpr;
        clonedField->span = origField->span;
        clonedField->originalIndex = origField->originalIndex;
        clonedField->hasCtorInit = origField->hasCtorInit;
        clonedField->usesClassArena = origField->usesClassArena;
        clonedField->isTransient = origField->isTransient;
        clonedField->isReflect = origField->isReflect;
        for(Expr* arg : origField->ctorArgs)
            clonedField->ctorArgs.push_back(arg);

        TypeRef* resolvedType = m_symbols.substituteTypeParams(
            *origField->type,
            templateDecl.templateParams,
            templateArgs
        );
        clonedField->type = resolvedType;
        if(resolvedType->refMode == ParamPassingMode::None && SymbolTable::isArenaBackedContainer(m_symbols.resolveBaseTypeName(resolvedType->name))){
            clonedField->usesClassArena = true;
            const_cast<ClassDecl&>(templateDecl).needsClassArena = true;
        }
        else if(__hidden_adhd_layout_optimizer::TypeContainsArenaBackedContainer(*resolvedType, m_symbols)){
            const_cast<ClassDecl&>(templateDecl).needsClassArena = true;
        }
        collectTemplateInstantiationsInType(*resolvedType, unit);

        usize fieldSize = 8u;
        usize fieldAlign = 8u;
        resolveTypeSizeAndAlign(*resolvedType, fieldSize, fieldAlign);
        clonedField->resolvedSize = fieldSize;
        clonedField->resolvedAlign = templateDecl.isSerialize ? 1u : fieldAlign;
        inst->orderedFields.push_back(clonedField);
    }

    __hidden_adhd_layout_optimizer::SortAndPackFields(
        inst->orderedFields,
        templateDecl.isSerialize,
        !templateDecl.implementedInterfaces.empty(),
        inst->totalSize,
        inst->totalAlign
    );
    unit.templateInstantiations.push_back(inst);
}

void LayoutOptimizer::resolveTypeSizeAndAlign(const TypeRef& type, usize& outSize, usize& outAlign, const u32 depth)const noexcept{
    if(depth > 16u || type.refMode != ParamPassingMode::None){
        outSize = 8u;
        outAlign = 8u;
        return;
    }

    if(type.isFuncType){
        outSize = (type.inferredCaptureBytes == 0u) ? 8u : (type.inferredCaptureBytes + 8u);
        outAlign = 8u;
        return;
    }

    const MStringView name = type.name;

    if(const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type)){
        if(unwrapped != &type){
            resolveTypeSizeAndAlign(*unwrapped, outSize, outAlign, depth + 1u);
            return;
        }
    }

    if(name == "bool" || name == "i8" || name == "u8"){
        outSize = 1u;
        outAlign = 1u;
        return;
    }
    if(name == "i16" || name == "u16"){
        outSize = 2u;
        outAlign = 2u;
        return;
    }
    if(name == "i32" || name == "u32" || name == "f32"){
        outSize = 4u;
        outAlign = 4u;
        return;
    }
    if(name == "i64" || name == "u64" || name == "isize" || name == "usize" || name == "f64"){
        outSize = 8u;
        outAlign = 8u;
        return;
    }

    if(name == "f32x4" || name == "i32x4" || name == "u32x4" || name == "f32x3_i32" || name == "f32x3_u32"){
        outSize = 16u;
        outAlign = 16u;
        return;
    }
    if(name == "f32x3x4"){
        outSize = 48u;
        outAlign = 16u;
        return;
    }
    if(name == "f32x4x4"){
        outSize = 64u;
        outAlign = 16u;
        return;
    }
    if(name == "f64x2" || name == "f64x3" || name == "f64x4" || name == "i64x4" || name == "u64x4" || name == "f64x3_i64" || name == "f64x3_u64"){
        outSize = 32u;
        outAlign = 32u;
        return;
    }
    if(name == "f64x3x3" || name == "f64x3x4"){
        outSize = 96u;
        outAlign = 32u;
        return;
    }
    if(name == "f64x4x4"){
        outSize = 128u;
        outAlign = 32u;
        return;
    }

    if(name == "Name" || name == "AssetRef"){
        outSize = 8u;
        outAlign = 8u;
        return;
    }
    if(name == "StringView" || name == "Span"){
        outSize = 16u;
        outAlign = 8u;
        return;
    }
    if(name == "String" || name == "Path"){
        outSize = 32u;
        outAlign = 8u;
        return;
    }
    if(name == "CompactString"){
        outSize = 256u;
        outAlign = 1u;
        return;
    }

    if(name == "Atomic"){
        if(!type.templateArgs.empty() && type.templateArgs[0].type){
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, outSize, outAlign, depth + 1u);
            return;
        }
        outSize = 8u;
        outAlign = 8u;
        return;
    }

    if(name == "Optional"){
        if(!type.templateArgs.empty() && type.templateArgs[0].type){
            usize innerSize = 8u;
            usize innerAlign = 8u;
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, innerSize, innerAlign, depth + 1u);
            outAlign = innerAlign;
            outSize = __hidden_adhd_layout_optimizer::AlignUp(innerSize + 1u, innerAlign);
            return;
        }
        outSize = 16u;
        outAlign = 8u;
        return;
    }

    if(name == "Expected"){
        outSize = 24u;
        outAlign = 8u;
        return;
    }

    if(name == "Array"){
        usize elemSize = 4u;
        usize elemAlign = 4u;
        usize count = 1u;
        if(!type.templateArgs.empty() && type.templateArgs[0].type)
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, elemSize, elemAlign, depth + 1u);
        if(type.templateArgs.size() >= 2u){
            const MStringView countText = type.templateArgs[1].type ? type.templateArgs[1].type->name : type.templateArgs[1].constantText;
            count = __hidden_adhd_layout_optimizer::ParseUnsignedConstant(countText, m_symbols);
        }
        outSize = elemSize * count;
        outAlign = elemAlign;
        return;
    }

    if(name == "FixedVector"){
        usize elemSize = 4u;
        usize elemAlign = 4u;
        usize count = 1u;
        if(!type.templateArgs.empty() && type.templateArgs[0].type)
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, elemSize, elemAlign, depth + 1u);
        if(type.templateArgs.size() >= 2u){
            const MStringView countText = type.templateArgs[1].type ? type.templateArgs[1].type->name : type.templateArgs[1].constantText;
            count = __hidden_adhd_layout_optimizer::ParseUnsignedConstant(countText, m_symbols);
        }
        outAlign = elemAlign > 4u ? elemAlign : 4u;
        outSize = __hidden_adhd_layout_optimizer::AlignUp(elemSize * count + 4u, outAlign);
        return;
    }

    if(SymbolTable::isArenaBackedContainer(name)){
        outSize = 32u;
        outAlign = 8u;
        return;
    }

    if(const EnumDecl* enumDecl = m_symbols.findEnum(name)){
        outSize = enumDecl->underlyingSize;
        outAlign = enumDecl->underlyingAlign;
        return;
    }

    if(const ClassDecl* classDecl = m_symbols.findClass(name)){
        if(!type.templateArgs.empty() && m_unit){
            for(const ClassTemplateInstantiation* inst : m_unit->templateInstantiations){
                if(inst && inst->templateDecl == classDecl && __hidden_adhd_layout_optimizer::AreTemplateArgListsEqual(inst->templateArgs, type.templateArgs, m_symbols)){
                    outSize = inst->totalSize;
                    outAlign = inst->totalAlign;
                    return;
                }
            }
        }
        outSize = classDecl->totalSize;
        outAlign = classDecl->totalAlign;
        return;
    }

    outSize = 8u;
    outAlign = 8u;
}

bool LayoutOptimizer::computeClassLayout(ClassDecl& classDecl, MVector<ParseError>& errors){
    if(classDecl.isSerialize && !classDecl.implementedInterfaces.empty()){
        ReportAdhdDiagnostic(m_arena, errors, classDecl.span, "'[serialize]' class cannot implement an interface (virtual table pointers cannot be binary serialized)");
        return false;
    }

    classDecl.orderedFields.clear();
    classDecl.orderedFields.reserve(classDecl.fields.size());

    for(FieldDecl* field : classDecl.fields){
        if(!field->type)
            continue;

        if(__hidden_adhd_layout_optimizer::TypeReferencesClassByValue(*field->type, classDecl.name, m_symbols)){
            ReportAdhdDiagnostic(m_arena, errors, field->span, "recursive by-value class member layout is prohibited; use 'in'/'mut' reference or 'Vector<T>'");
            return false;
        }

        const MStringView baseFieldTypeName = m_symbols.resolveBaseTypeName(field->type->name);
        if(field->type->refMode == ParamPassingMode::None && SymbolTable::isArenaBackedContainer(baseFieldTypeName)){
            field->usesClassArena = true;
            classDecl.needsClassArena = true;
        }
        else if(__hidden_adhd_layout_optimizer::TypeContainsArenaBackedContainer(*field->type, m_symbols)){
            classDecl.needsClassArena = true;
        }

        usize fieldSize = 8u;
        usize fieldAlign = 8u;
        resolveTypeSizeAndAlign(*field->type, fieldSize, fieldAlign);

        if(classDecl.isSerialize && !field->isTransient){
            MStringView serializeErr;
            if(!__hidden_adhd_layout_optimizer::IsTypeBinarySerializable(*field->type, m_symbols, serializeErr)){
                ReportAdhdDiagnostic(m_arena, errors, field->span, serializeErr);
                return false;
            }
            field->resolvedSize = fieldSize;
            field->resolvedAlign = 1u;
        }
        else{
            field->resolvedSize = fieldSize;
            field->resolvedAlign = fieldAlign;
        }

        classDecl.orderedFields.push_back(field);
    }

    __hidden_adhd_layout_optimizer::SortAndPackFields(
        classDecl.orderedFields,
        classDecl.isSerialize,
        !classDecl.implementedInterfaces.empty(),
        classDecl.totalSize,
        classDecl.totalAlign
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

