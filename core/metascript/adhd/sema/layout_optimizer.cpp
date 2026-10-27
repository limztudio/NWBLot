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
    }

    usize val = 0u;
    for(usize i = 0u; i < text.size(); ++i){
        const MChar c = text[i];
        if(c >= '0' && c <= '9')
            val = val * 10u + static_cast<usize>(c - '0');
        else
            break;
    }
    return val > 0u ? val : 1u;
}

TypeRef* SubstituteTemplateType(
    const TypeRef& origType,
    const MVector<TemplateParamDecl>& templateParams,
    const MVector<TypeArg>& templateArgs,
    MetaArena& arena
){
    const usize count = templateParams.size() < templateArgs.size() ? templateParams.size() : templateArgs.size();
    for(usize p = 0u; p < count; ++p){
        if(!templateParams[p].constValueType && origType.name == templateParams[p].name && origType.templateArgs.empty() && templateArgs[p].type)
            return templateArgs[p].type;
    }

    if(origType.templateArgs.empty() && !origType.isFuncType)
        return const_cast<TypeRef*>(&origType);

    TypeRef* cloned = AllocateAstNode<TypeRef>(arena);
    cloned->name = origType.name;
    cloned->refMode = origType.refMode;
    cloned->funcReturnType = origType.funcReturnType
        ? SubstituteTemplateType(*origType.funcReturnType, templateParams, templateArgs, arena)
        : nullptr;
    cloned->inferredCaptureBytes = origType.inferredCaptureBytes;
    cloned->span = origType.span;
    cloned->isFuncType = origType.isFuncType;
    cloned->isAuto = origType.isAuto;
    for(const FuncParamType& fp : origType.funcParams){
        FuncParamType subFp = fp;
        if(fp.type)
            subFp.type = SubstituteTemplateType(*fp.type, templateParams, templateArgs, arena);
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
                subArg.type = SubstituteTemplateType(*arg.type, templateParams, templateArgs, arena);
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

[[nodiscard]] bool AreTemplateArgListsEqual(const MVector<TypeArg>& a, const MVector<TypeArg>& b)noexcept{
    if(a.size() != b.size())
        return false;
    for(usize i = 0u; i < a.size(); ++i){
        const MStringView lhsName = a[i].type ? a[i].type->name : a[i].constantText;
        const MStringView rhsName = b[i].type ? b[i].type->name : b[i].constantText;
        if(lhsName != rhsName)
            return false;
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
    for(ClassDecl* classDecl : unit.classes){
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
        for(const MethodDecl* m : classDecl->methods){
            if(!m)
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
        if(!fn)
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
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier && !expr.callTemplateArgs.empty()){
        if(const ClassDecl* cls = m_symbols.findClass(expr.lhs->text)){
            if(!cls->templateParams.empty() && expr.callTemplateArgs.size() == cls->templateParams.size())
                instantiateClassTemplate(*cls, expr.callTemplateArgs, unit);
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

void LayoutOptimizer::collectTemplateInstantiationsInType(const TypeRef& type, TranslationUnit& unit){
    if(type.isFuncType){
        if(type.funcReturnType)
            collectTemplateInstantiationsInType(*type.funcReturnType, unit);
        for(const FuncParamType& fp : type.funcParams){
            if(fp.type)
                collectTemplateInstantiationsInType(*fp.type, unit);
        }
    }
    for(const TypeArg& arg : type.templateArgs){
        if(arg.type)
            collectTemplateInstantiationsInType(*arg.type, unit);
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
    for(const ClassTemplateInstantiation* existing : unit.templateInstantiations){
        if(existing->templateDecl == &templateDecl && __hidden_adhd_layout_optimizer::AreTemplateArgListsEqual(existing->templateArgs, templateArgs))
            return;
    }

    ClassTemplateInstantiation* inst = AllocateAstNode<ClassTemplateInstantiation>(m_arena);
    inst->templateDecl = &templateDecl;
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
        for(Expr* arg : origField->ctorArgs)
            clonedField->ctorArgs.push_back(arg);

        TypeRef* resolvedType = __hidden_adhd_layout_optimizer::SubstituteTemplateType(
            *origField->type,
            templateDecl.templateParams,
            templateArgs,
            m_arena
        );
        clonedField->type = resolvedType;
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

void LayoutOptimizer::resolveTypeSizeAndAlign(const TypeRef& type, usize& outSize, usize& outAlign)const noexcept{
    if(type.refMode != ParamPassingMode::None){
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

    if(const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(name)){
        if(aliasDecl->aliasedType){
            if(!aliasDecl->templateParams.empty() && !type.templateArgs.empty()){
                const TypeRef* resolvedAlias = __hidden_adhd_layout_optimizer::SubstituteTemplateType(
                    *aliasDecl->aliasedType,
                    aliasDecl->templateParams,
                    type.templateArgs,
                    m_arena
                );
                resolveTypeSizeAndAlign(*resolvedAlias, outSize, outAlign);
                return;
            }
            resolveTypeSizeAndAlign(*aliasDecl->aliasedType, outSize, outAlign);
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

    if(name == "f32x4" || name == "i32x4" || name == "u32x4" || name == "f32x3_i32" || name == "f32x3_u32" || name == "f64x2"){
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
    if(name == "f64x3" || name == "f64x4" || name == "i64x4" || name == "u64x4" || name == "f64x3_i64" || name == "f64x3_u64"){
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
        outSize = 64u;
        outAlign = 8u;
        return;
    }

    if(name == "Atomic"){
        if(!type.templateArgs.empty() && type.templateArgs[0].type){
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, outSize, outAlign);
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
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, innerSize, innerAlign);
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
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, elemSize, elemAlign);
        if(type.templateArgs.size() >= 2u){
            const MStringView countText = type.templateArgs[1].type ? type.templateArgs[1].type->name : type.templateArgs[1].constantText;
            count = __hidden_adhd_layout_optimizer::ParseUnsignedConstant(countText, m_symbols);
        }
        outSize = elemSize * count;
        outAlign = elemAlign;
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
                if(inst && inst->templateDecl == classDecl && __hidden_adhd_layout_optimizer::AreTemplateArgListsEqual(inst->templateArgs, type.templateArgs)){
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

        if(SymbolTable::isArenaBackedContainer(field->type->name)){
            field->usesClassArena = true;
            classDecl.needsClassArena = true;
        }

        usize fieldSize = 8u;
        usize fieldAlign = 8u;
        resolveTypeSizeAndAlign(*field->type, fieldSize, fieldAlign);

        if(classDecl.isSerialize && !field->isTransient){
            if(field->type->refMode != ParamPassingMode::None){
                ReportAdhdDiagnostic(m_arena, errors, field->span, "'[serialize]' class cannot contain non-transient 'in'/'mut' reference members");
                return false;
            }
            if(SymbolTable::isArenaBackedContainer(field->type->name) || field->type->name == "String" || field->type->name == "StringView" || field->type->name == "Span"){
                ReportAdhdDiagnostic(m_arena, errors, field->span, "'[serialize]' class cannot contain non-transient dynamic heap or view members; mark with '[transient]' or use fixed-size types");
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

