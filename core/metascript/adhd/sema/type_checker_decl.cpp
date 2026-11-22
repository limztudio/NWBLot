// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include <core/metascript/adhd/ast/operator_semantics.h>

#include "type_checker.h"
#include "constructor_argument_checker.h"

#include "type_comparator.h"
#include "type_layout_analyzer.h"
#include "type_resolver.h"

#include <core/metascript/adhd/sema/asset_type_analyzer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_checker_decl{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CollectAssignedThisMembers(const Span<const Stmt* const> stmts, MVector<MStringView>& outMembers, MVector<SourceSpan>& outSpans){
    for(const Stmt* stmt : stmts){
        if(!stmt)
            continue;
        if(IsThisMemberSimpleAssignStmt(*stmt)){
            outMembers.push_back(stmt->expr->lhs->text);
            outSpans.push_back(stmt->expr->lhs->span);
        }
        if(!stmt->statements.empty())
            CollectAssignedThisMembers(stmt->statements, outMembers, outSpans);
        if(!stmt->elseStatements.empty())
            CollectAssignedThisMembers(stmt->elseStatements, outMembers, outSpans);
        for(const SwitchCase& sc : stmt->switchCases)
            CollectAssignedThisMembers(sc.statements, outMembers, outSpans);
    }
}

[[nodiscard]] b8 IsMemberDefinitelyInitializedInStmts(Span<const Stmt* const> stmts, MStringView fieldName)noexcept;

[[nodiscard]] b8 StmtListMayReturnWithoutInit(const Span<const Stmt* const> stmts, const MStringView fieldName)noexcept{
    for(const Stmt* stmt : stmts){
        if(!stmt)
            continue;
        if(stmt->kind == StmtKind::Return)
            return true;
        if(IsThisMemberSimpleAssignStmt(*stmt, fieldName))
            return false;
        if(stmt->kind == StmtKind::Block || stmt->kind == StmtKind::ParallelBlock){
            if(StmtListMayReturnWithoutInit(stmt->statements, fieldName))
                return true;
            if(IsMemberDefinitelyInitializedInStmts(stmt->statements, fieldName))
                return false;
        }
        else if(stmt->kind == StmtKind::If){
            if(StmtListMayReturnWithoutInit(stmt->statements, fieldName) || StmtListMayReturnWithoutInit(stmt->elseStatements, fieldName))
                return true;
            if(!stmt->elseStatements.empty() && IsMemberDefinitelyInitializedInStmts(stmt->statements, fieldName) && IsMemberDefinitelyInitializedInStmts(stmt->elseStatements, fieldName))
                return false;
        }
        else if(stmt->kind == StmtKind::Switch){
            for(const SwitchCase& sc : stmt->switchCases){
                if(StmtListMayReturnWithoutInit(sc.statements, fieldName))
                    return true;
            }
        }
        else if(IsSequentialLoopStmtKind(stmt->kind)){
            if(StmtListMayReturnWithoutInit(stmt->statements, fieldName))
                return true;
        }
    }
    return false;
}

[[nodiscard]] b8 IsMemberDefinitelyInitializedInStmts(const Span<const Stmt* const> stmts, const MStringView fieldName)noexcept{
    for(const Stmt* stmt : stmts){
        if(!stmt)
            continue;
        if(IsTerminalJumpStmtKind(stmt->kind))
            return false;
        if(IsThisMemberSimpleAssignStmt(*stmt, fieldName))
            return true;
        else if(stmt->kind == StmtKind::Block || stmt->kind == StmtKind::ParallelBlock){
            if(IsMemberDefinitelyInitializedInStmts(stmt->statements, fieldName))
                return true;
            if(StmtListMayReturnWithoutInit(stmt->statements, fieldName))
                return false;
        }
        else if(stmt->kind == StmtKind::If){
            if(!stmt->elseStatements.empty() && IsMemberDefinitelyInitializedInStmts(stmt->statements, fieldName) && IsMemberDefinitelyInitializedInStmts(stmt->elseStatements, fieldName))
                return true;
            if(StmtListMayReturnWithoutInit(stmt->statements, fieldName) || StmtListMayReturnWithoutInit(stmt->elseStatements, fieldName))
                return false;
        }
        else if(stmt->kind == StmtKind::Switch){
            b8 hasDefault = false;
            b8 allCasesInit = !stmt->switchCases.empty();
            for(const SwitchCase& sc : stmt->switchCases){
                if(sc.isDefault)
                    hasDefault = true;
                if(!IsMemberDefinitelyInitializedInStmts(sc.statements, fieldName)){
                    allCasesInit = false;
                    break;
                }
            }
            if(hasDefault && allCasesInit)
                return true;
            for(const SwitchCase& sc : stmt->switchCases){
                if(StmtListMayReturnWithoutInit(sc.statements, fieldName))
                    return false;
            }
        }
        else if(IsSequentialLoopStmtKind(stmt->kind)){
            if(StmtListMayReturnWithoutInit(stmt->statements, fieldName))
                return false;
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TypeChecker::checkInterfaceDecl(InterfaceDecl& iface, MVector<ParseError>& errors){
    if(iface.isImported || iface.isExtern)
        return true;

    MethodDecl ifaceScope(m_arena);
    ifaceScope.templateParams = iface.templateParams;
    MVector<MethodDecl*> inheritedMethods(m_arena);
    b8 ok = checkTemplateParamBounds(iface.templateParams, nullptr, &ifaceScope, errors);
    for(usize i = 0u; i < iface.baseInterfaces.size(); ++i){
        const TypeRef* baseType = iface.baseInterfaces[i];
        if(!baseType)
            continue;
        const MStringView baseName = m_symbols.resolveBaseTypeName(baseType->name);
        if(TypeComparator(m_symbols).hasEquivalentInPrefix(Span<const TypeRef* const>(iface.baseInterfaces.data(), i), baseType)){
            reportError(errors, iface.span, "duplicate base interface in interface declaration");
            ok = false;
        }
        if(m_symbols.findClass(baseName)){
            reportError(errors, iface.span, "'interface' cannot inherit from a concrete 'class'");
            ok = false;
        }
        else if(m_symbols.findInterface(baseName)){
            TypeRef selfRef(m_arena);
            selfRef.name = iface.name;
            if(interfaceInheritsFrom(*baseType, selfRef)){
                reportError(errors, iface.span, "cyclic interface inheritance is prohibited");
                ok = false;
            }
            else
                m_symbols.collectAllInterfaceMethods(*baseType, inheritedMethods);
        }
        else if(IsNonCopyableBaseInterfaceName(baseName)){
        }
        else{
            reportError(errors, iface.span, "unknown base interface name");
            ok = false;
        }
        if(!checkTypeRef(*baseType, nullptr, &ifaceScope, errors))
            ok = false;
    }
    for(usize i = 0u; i < iface.methods.size(); ++i){
        MethodDecl* m = iface.methods[i];
        if(!m)
            continue;
        for(const ParamDecl& param : m->params){
            if(param.retainsBorrow){
                reportError(errors, param.span, "'in borrow' cannot change an interface contract");
                ok = false;
            }
        }
        if(m->allowsCppUnwind){
            if(iface.isExtern || m->isExtern){
                reportError(errors, m->span, "'[cpp_unwind]' requires an authored interface contract");
                ok = false;
            }
            for(const MethodDecl* base : inheritedMethods){
                if(!base->allowsCppUnwind && base->name == m->name && base->isConst == m->isConst && TypeComparator(m_symbols).doParamListsMatch(base->params, m->params)){
                    reportError(errors, m->span, "'[cpp_unwind]' cannot weaken an inherited nonthrowing interface contract");
                    ok = false;
                    break;
                }
            }
        }
        if(!iface.isExtern && !m->isExtern){
            if(!m->isOperator && !m->name.empty() && !IsAsciiLower(m->name[0])){
                reportError(errors, m->span, "member function name must start with a lowercase letter");
                ok = false;
            }
            for(usize j = 0u; j < i; ++j){
                const MethodDecl* prev = iface.methods[j];
                if(prev && prev->isOperator == m->isOperator && prev->name == m->name && prev->isConst == m->isConst && TypeComparator(m_symbols).doParamListsMatch(prev->params, m->params)){
                    reportError(errors, m->span, "duplicate interface method declaration with identical signature");
                    ok = false;
                    break;
                }
            }
        }
        if(!checkCallableSignature(nullptr, *m, errors))
            ok = false;
    }
    return ok;
}

b8 TypeChecker::checkCallableSignature(const ClassDecl* enclosingClass, MethodDecl& method, MVector<ParseError>& errors){
    b8 ok = checkTemplateParamBounds(method.templateParams, enclosingClass, &method, errors);
    if(method.returnType && !checkReturnTypeRef(*method.returnType, enclosingClass, &method, method.span, errors))
        ok = false;
    if(!method.initializedCppBase.empty()){
        if(!enclosingClass || enclosingClass->isExtern || !method.isConstructor || method.isOutOfLine || !method.hasBody){
            reportError(errors, method.span, "'[cpp_base]' requires a constructor defined inside its authored class");
            return false;
        }
        b8 foundBase = false;
        for(const TypeRef* base : enclosingClass->implementedInterfaces){
            if(!base || base->name != method.initializedCppBase)
                continue;
            const MStringView baseName = m_symbols.resolveBaseTypeName(base->name);
            const ClassDecl* declared = m_symbols.findClass(baseName);
            const BuiltinObjectTypeInfo* builtin = FindBuiltinObjectType(baseName);
            foundBase = declared ? declared->isExtern && declared->isPolymorphicBase : builtin && builtin->isPolymorphicBase;
            break;
        }
        if(!foundBase){
            reportError(errors, method.span, "'[cpp_base]' must name a direct external polymorphic base");
            return false;
        }
        for(const MStringView argument : method.cppBaseArguments){
            b8 foundArgument = false;
            for(const ParamDecl& param : method.params){
                if(param.name == argument && (param.mode == ParamPassingMode::Mut || param.retainsBorrow)
                    && !param.forwardsArgument && !param.copiesArgument){
                    foundArgument = true;
                    break;
                }
            }
            if(!foundArgument){
                reportError(errors, method.span, "'[cpp_base]' must forward a 'mut' or 'in borrow' constructor parameter");
                return false;
            }
        }
    }
    if(method.exportsRvalueOverload){
        const MStringView result = method.returnType ? m_symbols.resolveBaseTypeName(method.returnType->name) : MStringView{};
        if(!enclosingClass || enclosingClass->isExtern || method.isExtern || method.isOverride || method.isStatic || method.isOperator
            || method.isConstructor || method.isDestructor || !method.templateParams.empty() || method.params.size() != 1u
            || method.params[0].mode != ParamPassingMode::Mut || !method.params[0].type
            || m_symbols.resolveRefMode(method.params[0].type) != ParamPassingMode::None
            || !method.returnType || method.returnsReference || (result != "void" && !BuiltinTypeSemantics::IsScalarType(result) && !m_symbols.findEnum(result))){
            reportError(errors, method.span, "'[cpp_rvalue_overload]' requires an ordinary instance method with one mutable value and a scalar, enum, or void result");
            return false;
        }
    }
    if(method.exportsExplicitBool || method.exportsTakingAssignment){
        if(!enclosingClass || enclosingClass->isExtern || method.isExtern || method.isOverride || method.isStatic || method.isOperator
            || method.isConstructor || method.isDestructor || !method.templateParams.empty() || method.returnsReference || !method.returnType){
            reportError(errors, method.span, "C++ operator exports require an ordinary non-template instance method");
            return false;
        }
        const MStringView result = m_symbols.resolveBaseTypeName(method.returnType->name);
        if(method.exportsExplicitBool && (!method.isConst || !method.params.empty() || result != "b8")){
            reportError(errors, method.span, "'[cpp_explicit_bool]' requires a const parameterless method returning b8");
            return false;
        }
        if(method.exportsTakingAssignment && (method.isConst || method.params.size() != 1u || method.params[0].mode != ParamPassingMode::Mut
            || !method.params[0].type || m_symbols.resolveRefMode(method.params[0].type) != ParamPassingMode::None
            || m_symbols.resolveBaseTypeName(method.params[0].type->name) == enclosingClass->name || result != "void")){
            reportError(errors, method.span, "'[cpp_take_assignment]' requires a mutable instance method taking one mutable value of another type and returning void");
            return false;
        }
        for(const MethodDecl* other : enclosingClass->methods){
            if(other == &method)
                break;
            if(other && ((method.exportsExplicitBool && other->exportsExplicitBool) || (method.exportsTakingAssignment && other->exportsTakingAssignment))){
                reportError(errors, method.span, "the class already exports this C++ operator");
                return false;
            }
        }
    }
    if(method.returnsFromOwnerArena){
        TypeRef ownerType(m_arena);
        if(enclosingClass)
            ownerType.name = enclosingClass->name;
        const TypeLayoutAnalyzer layout(m_symbols);
        if(!enclosingClass || method.isConstructor || method.isDestructor || method.isOverride || method.isStatic || method.isOperator
            || !(enclosingClass->isArenaOwner || layout.isNonRootMotherArenaType(ownerType))
            || !method.returnType || !layout.requiresMotherArena(*method.returnType)){
            reportError(errors, method.span, "'[owner_result]' requires an arena-backed result and an instance method with a mother arena");
            return false;
        }
    }
    if(method.allowsCppUnwind){
        if(method.isOperator || method.assetCookRole != AssetCookRole::None){
            reportError(errors, method.span, "'[cpp_unwind]' requires an ordinary function or method without an imposed C++ contract");
            return false;
        }
        if(enclosingClass && (method.isConstructor || method.isDestructor)){
            if(!enclosingClass->assetTypeName.empty() || !enclosingClass->systemTypeName.empty()
                || !enclosingClass->subsystemTypeName.empty() || !enclosingClass->componentTypeName.empty()){
                reportError(errors, method.span, "'[cpp_unwind]' cannot change a registered class construction or destruction contract");
                return false;
            }
            if(method.isDestructor){
                for(const TypeRef* base : enclosingClass->implementedInterfaces){
                    if(base && !IsNonCopyableBaseInterfaceName(m_symbols.resolveBaseTypeName(base->name))){
                        reportError(errors, method.span, "'[cpp_unwind]' cannot weaken an inherited destructor contract");
                        return false;
                    }
                }
            }
        }
    }
    if(!method.pairedSidecar.empty()){
        const Stmt* extension = m_symbols.findGlobalConstant(method.pairedSidecar);
        const auto* exported = extension ? nullptr : FindBuiltinGlobalConstant(method.pairedSidecar);
        if(exported)
            recordRequiredHeader(exported->requiredHeader);
        if(enclosingClass || !method.hasBody || method.isExtern || !method.ownerClassName.empty()
            || ((!extension || !extension->varType || extension->varType->name != "StringView") && (!exported || exported->adhdTypeName != "StringView"))){
            reportError(errors, method.span, "'[cook_sidecar]' requires a free function definition and a StringView extension constant");
            return false;
        }
    }
    if(method.assetCookRole != AssetCookRole::None){
        if(enclosingClass || !method.ownerClassName.empty() || method.isExtern || !method.hasBody || !method.templateParams.empty() || !method.returnType){
            reportError(errors, method.span, "an asset cook role requires a non-template free function definition");
            return false;
        }
        if(method.assetCookRole == AssetCookRole::DocumentParser || method.assetCookRole == AssetCookRole::VolumePrepare){
            const b8 document = method.assetCookRole == AssetCookRole::DocumentParser;
            if(method.returnType->name != (document ? "AssetMetadataParseResult" : "b8") || method.params.size() != 1u || method.params[0].mode != ParamPassingMode::Mut
                || method.params[0].type->name != (document ? "AssetDocumentMetadataParseContext" : "CookVolumePrepareContext")){
                reportError(errors, method.span, "a cook callback must match its registry context and return type");
                return false;
            }
        }
        else if(method.assetCookRole == AssetCookRole::Setup){
            if(method.returnType->name != "void" || !method.params.empty()){
                reportError(errors, method.span, "'[cook_setup]' requires a void function with no parameters");
                return false;
            }
        }
        else if(method.assetCookRole != AssetCookRole::Entry && method.returnType->name != "b8"){
            reportError(errors, method.span, "asset writers and cook registrars must return b8");
            return false;
        }
        if(method.assetCookRole == AssetCookRole::Entry){
            const TypeRef* result = method.returnType;
            const ClassDecl* asset = (result->name == "Expected" && result->templateArgs.size() == 1u) ? m_symbols.findClass(result->templateArgs.front().constantOrTypeName()) : nullptr;
            const AssetTypeAnalyzer assetTypes(m_symbols);
            if(!asset || asset->assetTypeName.empty() || !assetTypes.findAssetLoadBinaryMethod(*asset) || (method.cookCodec.empty() && assetTypes.findImplementedAssetInterface(*asset))
                || method.params.size() != 1u || !m_symbols.findClass(m_symbols.resolveBaseTypeName(method.params[0].type->name))){
                reportError(errors, method.span, "'[cook_entry]' requires an entry parameter and must return Expected<Asset> for an auto-generated asset codec");
                return false;
            }
            if(!method.cookCodec.empty()){
                TypeRef alias(m_arena);
                alias.name = method.cookCodec;
                const TypeRef* codec = m_symbols.unwrapTypeAlias(&alias);
                if(!codec || codec->name != "FamilyMemberAssetCodec" || codec->templateArgs.size() != 3u || codec->templateArgs[1].constantOrTypeName() != asset->name){
                    reportError(errors, method.span, "'[cook_entry]' codec must be a visible family member codec alias for the built asset");
                    return false;
                }
            }
            const MethodDecl* reader = assetTypes.findCookEntryReader(method.cookValueReader, method.cookAcceptsValue);
            const Stmt* diagnostic = m_symbols.findGlobalConstant(method.cookDiagnosticPrefix);
            if(!diagnostic || !diagnostic->varType || diagnostic->varType->name != "StringView"){
                reportError(errors, method.span, "'[cook_entry]' requires a StringView diagnostic prefix constant");
                return false;
            }
            if(!reader || reader->isExtern || !reader->returnType || reader->returnType->name != "Expected" || reader->returnType->templateArgs.size() != 1u
                || reader->returnType->templateArgs.front().constantOrTypeName() != method.params[0].type->name){
                reportError(errors, method.span, "'[cook_entry]' requires a visible typed value reader returning Expected<Entry> and taking (in Name, in Path, in MetascriptValue) and optionally in CpuTaskScheduler, or a document_only reader taking (in Path, in StringView, in Path, in MetascriptDocument)");
                return false;
            }
        }
        else if(method.assetCookRole == AssetCookRole::GatherMerge){
            if(method.params.size() != 3u
                || method.params[0].mode != ParamPassingMode::In || method.params[0].type->name != "Name"
                || method.params[1].mode != ParamPassingMode::In || method.params[1].type->name != "BinaryByteView"
                || method.params[2].mode != ParamPassingMode::Mut || method.params[2].type->name != "BinaryUpdateContext"){
                reportError(errors, method.span, "'[gather_merge]' requires (in Name, in BinaryByteView, mut BinaryUpdateContext)");
                return false;
            }
        }
        else if(method.assetCookRole == AssetCookRole::Writer){
            const ClassDecl* asset = method.params.empty() ? nullptr : m_symbols.findClass(m_symbols.resolveBaseTypeName(method.params.front().type->name));
            const TypeRef* bytes = method.params.size() == 2u ? method.params[1].type : nullptr;
            const AssetTypeAnalyzer assetTypes(m_symbols);
            if(!asset || asset->assetTypeName.empty() || !assetTypes.findAssetLoadBinaryMethod(*asset) || assetTypes.findImplementedAssetInterface(*asset)
                || !bytes || method.params[0].mode != ParamPassingMode::In || method.params[1].mode != ParamPassingMode::Mut
                || bytes->name != "Vector" || bytes->templateArgs.size() != 1u || bytes->templateArgs[0].constantOrTypeName() != "u8"){
                reportError(errors, method.span, "'[asset_writer]' requires (in Asset, mut Vector<u8>) for an asset with an auto-generated codec");
                return false;
            }
        }
        else if(method.assetCookRole == AssetCookRole::Registrar && (method.params.size() != 1u || method.params[0].mode != ParamPassingMode::Mut
            || (method.params[0].type->name != "CookEntryRegistry" && method.params[0].type->name != "CookDependencyRegistry"))){
            reportError(errors, method.span, "'[cook_registrar]' requires a mutable cook entry or dependency registry");
            return false;
        }
    }
    if(method.isOperator && !method.isExtern){
        if(!method.templateParams.empty()){
            reportError(errors, method.span, "operator overload cannot declare template parameters");
            ok = false;
        }
        if(method.params.size() == 1u && method.params[0].mode == ParamPassingMode::Mut){
            reportError(errors, method.params[0].span, "operator overload parameter must be passed by 'in', not 'mut'");
            ok = false;
        }
        if(OperatorSemantics::IsBoolReturnOperatorSymbol(method.name)){
            const MStringView retBase = method.returnType ? m_symbols.resolveBaseTypeName(method.returnType->name) : MStringView{};
            if(!method.returnType || !BuiltinTypeSemantics::IsBoolScalarType(retBase) || m_symbols.resolveRefMode(method.returnType) != ParamPassingMode::None){
                reportError(errors, method.span, "comparison and logical '!' operator overloads must return 'b8' by value");
                ok = false;
            }
        }
    }
    for(usize pIdx = 0u; pIdx < method.params.size(); ++pIdx){
        const ParamDecl& p = method.params[pIdx];
        if(p.forwardsArgument || p.copiesArgument){
            b8 typeParameter = false;
            for(const TemplateParamDecl& param : method.templateParams){
                if(!param.constValueType && p.type && p.type->name == param.name && p.type->templateArgs.empty())
                    typeParameter = true;
            }
            if(method.returnsReference || (method.returnType && IsBorrowViewType(method.returnType->name))){
                reportError(errors, p.span, "a generic function with a forwarded or copied parameter cannot return a reference or borrowed view");
                return false;
            }
            if((!typeParameter && !(p.copiesArgument && p.type && BuiltinTypeSemantics::IsIntegerScalarType(p.type->name)))
                || method.templateParams.empty() || enclosingClass || !method.ownerClassName.empty() || !method.hasBody){
                reportError(errors, p.span, "'mut forward' requires an unwrapped type parameter, and 'mut copy' requires a type parameter or integer on a generic free function definition");
                return false;
            }
        }
        if(p.retainsBorrow && (method.isOverride || method.isOperator || method.assetCookRole != AssetCookRole::None)){
            reportError(errors, p.span, "'in borrow' cannot change an imposed callable contract");
            ok = false;
        }
        if(p.type){
            if(!checkTypeRef(*p.type, enclosingClass, &method, errors))
                ok = false;
            if(!method.isExtern){
                const MStringView paramBase = m_symbols.resolveBaseTypeName(p.type->name);
                if(!p.type->isFuncType && !m_symbols.hasTypeSymbol(paramBase) && SymbolTable::IsSimdStorageType(paramBase) && IsOverAlignedBuiltinType(paramBase)){
                    reportError(errors, p.span, "SIMD memory-layout type cannot be used as a parameter; use 'VectorSingle', 'VectorDouble', 'MatrixSingle', or 'MatrixDouble' instead");
                    ok = false;
                }
            }
        }
        if(!method.hasBody && !method.isExtern && !p.name.empty()){
            for(usize prevP = 0u; prevP < pIdx; ++prevP){
                if(method.params[prevP].name == p.name){
                    reportError(errors, p.span, "duplicate parameter name");
                    ok = false;
                    break;
                }
            }
        }
    }
    return ok;
}

b8 TypeChecker::hasFieldInlineOrAutoDefault(const ClassDecl& classDecl, const FieldDecl& field)const noexcept{
    const TypeRef* unwrappedFieldType = field.type ? m_symbols.unwrapTypeAlias(field.type) : nullptr;
    const b8 isAutoDefaultField = field.type && m_symbols.resolveRefMode(field.type) == ParamPassingMode::None && (
        field.type->isFuncType
        || (unwrappedFieldType && unwrappedFieldType->isFuncType)
        || m_symbols.isSelfDefaultedType(field.type->name, &classDecl)
        || FindTemplateParam(&classDecl, nullptr, field.type->name) != nullptr
    );
    return (field.defaultExpr != nullptr) || field.hasCtorInit || isAutoDefaultField;
}

b8 TypeChecker::checkClassDecl(ClassDecl& classDecl, MVector<ParseError>& errors){
    if(classDecl.isExtern)
        return true;
    if(classDecl.isImported){
        b8 ok = true;
        if(!classDecl.systemTypeName.empty()){
            for(MethodDecl* m : classDecl.methods){
                if(!m->isConstructor && !m->isDestructor && m->templateParams.empty() && ContractSemantics::IsAutoOverrideMethod(ClassRoleKind::System, m->name))
                    m->isOverride = true;
            }
        }
        for(FieldDecl* field : classDecl.fields){
            if(!field)
                continue;
            if(TypeRef* targetFuncType = TypeResolver(m_symbols).extractUnderlyingFuncType(field->type); targetFuncType && field->defaultExpr && field->defaultExpr->kind == ExprKind::Lambda){
                if(!field->defaultExpr->lambdaCaptures.empty() && targetFuncType->inferredCaptureBytes < s_InferredLambdaCaptureBytes)
                    targetFuncType->inferredCaptureBytes = s_InferredLambdaCaptureBytes;
            }
        }
        for(MethodDecl* m : classDecl.methods){
            if(!m)
                continue;
            const b8 isLocalMethod = !m->isImported && !m->isExtern && m->hasBody;
            if(m->isOverride && m->templateParams.empty() && (classDecl.isExtensible || isLocalMethod)){
                if(classDecl.concreteBase){
                    const usize arity = m->params.size();
                    const MethodDecl* inherited = m_symbols.findClassMethod(*classDecl.concreteBase, m->name, &arity, m->isConst);
                    if(inherited && inherited->isOverride && m->isConst == inherited->isConst
                        && TypeComparator(m_symbols).areEquivalent(m->returnType, inherited->returnType)
                        && TypeComparator(m_symbols).doParamListsMatchWithSubst(m->params, inherited->params, {}, {})){
                        for(usize index = 0u; index < arity; ++index)
                            m->params[index].cppConstReference |= inherited->params[index].cppConstReference;
                    }
                }
                for(const TypeRef* baseType : classDecl.implementedInterfaces){
                    if(!baseType)
                        continue;
                    const MStringView baseName = m_symbols.resolveBaseTypeName(baseType->name);
                    if(const ClassDecl* sdk = m_symbols.findClass(baseName); sdk && sdk->isExtern && sdk->isPolymorphicBase){
                        for(const MethodDecl* candidate : sdk->methods){
                            if(candidate->name != m->name || candidate->isConst != m->isConst
                                || !TypeComparator(m_symbols).areEquivalent(m->returnType, candidate->returnType)
                                || !TypeComparator(m_symbols).doParamListsMatchWithSubst(m->params, candidate->params, {}, {}))
                                continue;
                            for(usize pi = 0u; pi < m->params.size(); ++pi)
                                m->params[pi].cppConstReference |= candidate->params[pi].cppConstReference;
                            break;
                        }
                        continue;
                    }
                    const auto* signature = FindBuiltinReceiverSignature(baseName, m->name);
                    if(!signature || signature->arity != m->params.size())
                        continue;
                    for(usize pi = 0u; pi < m->params.size(); ++pi)
                        m->params[pi].cppConstReference |= (signature->cppConstReferenceMask & (1u << pi)) != 0u;
                }
            }
            if(!isLocalMethod)
                continue;
            if(m->isConstructor && !checkConstructorInitialization(classDecl, *m, errors))
                ok = false;
            if(!checkCallableSignature(&classDecl, *m, errors))
                ok = false;
            const b8 prevRecordHeaders = m_recordGlobalHeaders;
            m_recordGlobalHeaders = true;
            if(!m_parallelValidator.validateMethod(*m, errors))
                ok = false;
            if(!checkMethodBody(&classDecl, *m, errors))
                ok = false;
            m_recordGlobalHeaders = prevRecordHeaders;
        }
        return ok;
    }

    b8 ok = checkTemplateParamBounds(classDecl.templateParams, &classDecl, nullptr, errors);
    if((classDecl.isExtensible || classDecl.concreteBase) && (classDecl.isExtern || !classDecl.templateParams.empty() || classDecl.isSerialize
        || classDecl.isReflect || classDecl.recordAlignment || classDecl.isMoveOnly || !classDecl.assetTypeName.empty()
        || !classDecl.subsystemTypeName.empty() || !classDecl.componentTypeName.empty() || !classDecl.systemTypeName.empty())){
        reportError(errors, classDecl.span, "authored concrete bases require ordinary non-template classes");
        return false;
    }
    if(classDecl.concreteBase){
        b8 hasConstructor = false;
        b8 hasAccessibleDefault = false;
        for(const MethodDecl* ctor : classDecl.concreteBase->methods){
            if(ctor->isConstructor){
                hasConstructor = true;
                hasAccessibleDefault |= ctor->params.empty() && ctor->access != AccessSpecifier::Private;
            }
        }
        if(hasConstructor && !hasAccessibleDefault){
            reportError(errors, classDecl.span, "a derived class requires an accessible default constructor on its concrete base");
            return false;
        }
    }
    if(classDecl.concreteBase && classDecl.isArenaOwner){
        reportError(errors, classDecl.span, "a derived class must use the caller's storage for its concrete base");
        return false;
    }
    for(const TypeAliasDecl* alias : classDecl.cppTypeAliases){
        const MStringView name = alias->name;
        b8 collides = name == classDecl.name || name == alias->aliasedType->name || SymbolTable::FindClassField(classDecl, name)
            || FindTemplateParam(&classDecl, nullptr, name) || BuiltinTypeSemantics::IsReservedPrimitiveOrContainerTypeName(name);
        for(const MethodDecl* method : classDecl.methods)
            collides |= method && method->name == name;
        for(const TypeAliasDecl* previous : classDecl.cppTypeAliases){
            if(previous == alias)
                break;
            collides |= previous->name == name;
        }
        if(collides){
            reportError(errors, alias->span, "C++ nested alias must have a distinct member name");
            ok = false;
        }
        if(!checkTypeRef(*alias->aliasedType, &classDecl, nullptr, errors))
            ok = false;
        recordBuiltinTypeHeader(alias->aliasedType->name);
    }
    for(const MStringView peer : classDecl.friendClasses){
        const ClassDecl* known = m_symbols.findClass(peer);
        if(BuiltinTypeSemantics::IsReservedPrimitiveOrContainerTypeName(peer) || (m_symbols.hasTypeSymbol(peer) && (!known || !known->templateParams.empty()))){
            reportError(errors, classDecl.span, "a friend must name a non-template peer class");
            ok = false;
        }
    }


    if(classDecl.isArenaOwner && classDecl.isSerialize){
        reportError(errors, classDecl.span, "'[arena]' class cannot be marked '[serialize]' (binary-serialized structs cannot own an arena)");
        ok = false;
    }
    if(classDecl.isModular && !classDecl.templateParams.empty()){
        reportError(errors, classDecl.span, "'[modular]' class cannot be a template");
        ok = false;
    }

    if(!classDecl.assetTypeName.empty()){
        if(!classDecl.isArenaOwner){
            reportError(errors, classDecl.span, "'[asset(\"...\")]' class must be marked '[arena, asset(\"...\")]'");
            ok = false;
        }
        if(classDecl.isSerialize){
            reportError(errors, classDecl.span, "'[asset(\"...\")]' class cannot be marked '[serialize]' (assets implement 'TypedAsset<T>' with 'loadBinary')");
            ok = false;
        }
        if(!classDecl.templateParams.empty()){
            reportError(errors, classDecl.span, "'[asset(\"...\")]' class cannot be a template");
            ok = false;
        }
    }

    if(!classDecl.systemTypeName.empty()){
        if(!classDecl.isArenaOwner){
            reportError(errors, classDecl.span, "'[system(\"...\")]' class must be marked '[arena, system(\"...\")]'");
            ok = false;
        }
        if(classDecl.isSerialize){
            reportError(errors, classDecl.span, "'[system(\"...\")]' class cannot be marked '[serialize]' (systems implement 'ISystem')");
            ok = false;
        }
        if(!classDecl.assetTypeName.empty() || !classDecl.subsystemTypeName.empty() || !classDecl.componentTypeName.empty()){
            reportError(errors, classDecl.span, "'[system(\"...\")]' cannot be combined with asset, subsystem, or component attributes");
            ok = false;
        }
        if(!classDecl.templateParams.empty()){
            reportError(errors, classDecl.span, "'[system(\"...\")]' class cannot be a template");
            ok = false;
        }
    }

    if(!classDecl.subsystemTypeName.empty()){
        if(!classDecl.isArenaOwner){
            reportError(errors, classDecl.span, "'[subsystem(\"...\")]' class must be marked '[arena, subsystem(\"...\")]'");
            ok = false;
        }
        if(classDecl.isSerialize || classDecl.isModular || !classDecl.assetTypeName.empty() || !classDecl.componentTypeName.empty() || !classDecl.templateParams.empty() || !classDecl.implementedInterfaces.empty() || !classDecl.methods.empty()){
            reportError(errors, classDecl.span, "'[subsystem(\"...\")]' class may only declare 'public' AssetRef<T> or Vector<AssetRef<T>> fields");
            ok = false;
        }
        if(classDecl.fields.empty()){
            reportError(errors, classDecl.span, "'[subsystem(\"...\")]' class must declare at least one asset field");
            ok = false;
        }
        for(const FieldDecl* field : classDecl.fields){
            if(!field)
                continue;
            if(field->access != AccessSpecifier::Public || field->defaultExpr || field->hasCtorInit || !field->type){
                reportError(errors, field->span, "'[subsystem(\"...\")]' field must be 'public' without an inline initializer");
                ok = false;
                continue;
            }
            MStringView assetName;
            b8 isVectorField = false;
            if(!AssetTypeAnalyzer(m_symbols).tryResolveSubsystemFieldAssetType(*field, assetName, isVectorField)){
                reportError(errors, field->span, "'[subsystem(\"...\")]' field must have type 'AssetRef<TAsset>' or 'Vector<AssetRef<TAsset>>'");
                ok = false;
            }
        }
    }

    if(!classDecl.componentTypeName.empty()){
        if(!classDecl.isArenaOwner){
            reportError(errors, classDecl.span, "'[component(\"...\")]' class must be marked '[arena, component(\"...\")]'");
            ok = false;
        }
        if(classDecl.isSerialize || !classDecl.assetTypeName.empty() || !classDecl.subsystemTypeName.empty() || !classDecl.systemTypeName.empty()){
            reportError(errors, classDecl.span, "'[component(\"...\")]' cannot be combined with '[serialize]', '[asset(\"...\")]', '[subsystem(\"...\")]', or '[system(\"...\")]'");
            ok = false;
        }
    }

    MVector<const MethodDecl*> matchedOverrideImpls(m_arena);
    for(usize i = 0u; i < classDecl.implementedInterfaces.size(); ++i){
        const TypeRef* ifaceType = classDecl.implementedInterfaces[i];
        if(!ifaceType)
            continue;
        if(TypeComparator(m_symbols).hasEquivalentInPrefix(Span<const TypeRef* const>(classDecl.implementedInterfaces.data(), i), ifaceType)){
            reportError(errors, classDecl.span, "duplicate implemented interface in class declaration");
            ok = false;
        }
        const MStringView ifaceName = m_symbols.resolveBaseTypeName(ifaceType->name);
        if(const ClassDecl* baseCls = m_symbols.findClass(ifaceName)){
            if(!classDecl.isExtern && !baseCls->isExtensible && !(baseCls->isExtern && baseCls->isPolymorphicBase)){
                reportError(errors, classDecl.span, "concrete class inheritance is prohibited in adhd; classes are implicitly final and may only implement interfaces");
                ok = false;
            }
            recordRequiredHeader(baseCls->requiredHeader);
            continue;
        }
        if(!m_symbols.findInterface(ifaceName)){
            const BuiltinObjectTypeInfo* builtinObj = FindBuiltinObjectType(ifaceName);
            const b8 isBuiltinInterface = IsNonCopyableBaseInterfaceName(ifaceName) || (builtinObj && builtinObj->isPolymorphicBase);
            if(
                (!classDecl.isExtern && !isBuiltinInterface)
                || m_symbols.findEnum(ifaceName)
                || FindBuiltinEnum(ifaceName)
                || BuiltinTypeSemantics::IsScalarType(ifaceName)
                || (builtinObj && !builtinObj->isPolymorphicBase)
                || FindBuiltinGenericType(ifaceName)
                || SymbolTable::IsSimdType(ifaceName)
                || SymbolTable::IsUnalignedVectorType(ifaceName)
            ){
                reportError(errors, ifaceType->span, "unknown interface in class implementation list; classes may only implement declared 'interface' types");
                ok = false;
            }
            else if(!checkTypeRef(*ifaceType, &classDecl, nullptr, errors)){
                ok = false;
            }
            continue;
        }
        if(!checkTypeRef(*ifaceType, &classDecl, nullptr, errors))
            ok = false;
        if(classDecl.isExtern)
            continue;
        MVector<MethodDecl*> requiredInterfaceMethods(m_arena);
        m_symbols.collectAllInterfaceMethods(*ifaceType, requiredInterfaceMethods);
        const TypeRef* resolvedIfaceType = m_symbols.unwrapTypeAlias(ifaceType);
        const InterfaceDecl* directIfaceDecl = m_symbols.findInterface(ifaceName);
        const Span<const TemplateParamDecl> ifaceParams = directIfaceDecl ? Span<const TemplateParamDecl>(directIfaceDecl->templateParams) : Span<const TemplateParamDecl>{};
        const Span<const TypeArg> ifaceArgs = resolvedIfaceType ? Span<const TypeArg>(resolvedIfaceType->templateArgs) : Span<const TypeArg>(ifaceType->templateArgs);

        for(MethodDecl* req : requiredInterfaceMethods){
            MethodDecl* impl = nullptr;
            for(MethodDecl* m : classDecl.methods){
                if(!m->isConstructor && !m->isDestructor && m->templateParams.empty() && m->isOperator == req->isOperator && m->name == req->name && m->params.size() == req->params.size()){
                    const b8 exactParamsMatch = (m->isConst == req->isConst)
                        && TypeComparator(m_symbols).doParamListsMatchWithSubst(m->params, req->params, ifaceParams, ifaceArgs);
                    if(exactParamsMatch){
                        impl = m;
                        break;
                    }
                    b8 alreadyMatched = false;
                    for(const MethodDecl* prevMatched : matchedOverrideImpls){
                        if(prevMatched == m){
                            alreadyMatched = true;
                            break;
                        }
                    }
                    if(!alreadyMatched && (!impl || m->isConst == req->isConst))
                        impl = m;
                }
            }
            if(!impl){
                reportError(errors, classDecl.span, "class does not implement required interface method");
                ok = false;
            }
            else{
                matchedOverrideImpls.push_back(impl);
                if(impl->allowsCppUnwind && !req->allowsCppUnwind){
                    reportError(errors, impl->span, "'[cpp_unwind]' cannot weaken a nonthrowing interface contract");
                    ok = false;
                }
                if(!impl->isOverride){
                    reportError(errors, impl->span, "method implementing an interface contract must be explicitly marked 'override'");
                    ok = false;
                }
                if(impl->access != AccessSpecifier::Public){
                    reportError(errors, impl->span, "method implementing an interface contract must be 'public'");
                    ok = false;
                }
                if(impl->isConst != req->isConst){
                    reportError(errors, impl->span, "override method 'const' qualifier does not match interface contract");
                    ok = false;
                }
                if(!TypeComparator(m_symbols).areEquivalentWithSubst(impl->returnType, req->returnType, ifaceParams, ifaceArgs)){
                    reportError(errors, impl->span, "override method return type does not match interface contract");
                    ok = false;
                }
                if(usize mismatchIdx = 0u; !TypeComparator(m_symbols).doParamListsMatchWithSubst(impl->params, req->params, ifaceParams, ifaceArgs, &mismatchIdx)){
                    reportError(errors, impl->params[mismatchIdx].span, "override method parameter 'in'/'mut' mode or type does not match interface contract");
                    ok = false;
                }
            }
        }
    }

    for(MethodDecl* m : classDecl.methods){
        if(
            !classDecl.systemTypeName.empty()
            && !m->isConstructor
            && !m->isDestructor
            && m->templateParams.empty()
            && ContractSemantics::IsAutoOverrideMethod(ClassRoleKind::System, m->name)
        ){
            m->isOverride = true;
        }
        if(m->isOverride){
            if(!m->templateParams.empty()){
                reportError(errors, m->span, "templated methods cannot be marked 'override'");
                ok = false;
                continue;
            }
            b8 matchesInterface = false;
            for(const MethodDecl* matchedImpl : matchedOverrideImpls){
                if(matchedImpl == m){
                    matchesInterface = true;
                    break;
                }
            }
            if(!matchesInterface && classDecl.concreteBase){
                const usize arity = m->params.size();
                const MethodDecl* inherited = m_symbols.findClassMethod(*classDecl.concreteBase, m->name, &arity, m->isConst);
                if(inherited && inherited->isOverride && m->isConst == inherited->isConst
                    && TypeComparator(m_symbols).areEquivalent(m->returnType, inherited->returnType)
                    && TypeComparator(m_symbols).doParamListsMatchWithSubst(m->params, inherited->params, {}, {})){
                    matchesInterface = true;
                    if(m->allowsCppUnwind && !inherited->allowsCppUnwind){
                        reportError(errors, m->span, "'[cpp_unwind]' cannot weaken a nonthrowing interface contract");
                        ok = false;
                    }
                    for(usize index = 0u; index < arity; ++index)
                        m->params[index].cppConstReference |= inherited->params[index].cppConstReference;
                }
            }
            b8 sdkAllowsUnwind = false;
            for(const TypeRef* baseType : classDecl.implementedInterfaces){
                if(!baseType)
                    continue;
                const MStringView baseName = m_symbols.resolveBaseTypeName(baseType->name);
                if(const ClassDecl* sdk = m_symbols.findClass(baseName); sdk && sdk->isExtern && sdk->isPolymorphicBase){
                    for(const MethodDecl* candidate : sdk->methods){
                        if(candidate->name != m->name || candidate->isConst != m->isConst
                            || !TypeComparator(m_symbols).areEquivalent(m->returnType, candidate->returnType)
                            || !TypeComparator(m_symbols).doParamListsMatchWithSubst(m->params, candidate->params, {}, {}))
                            continue;
                        matchesInterface = true;
                        if(m->allowsCppUnwind && !candidate->allowsCppUnwind){
                            reportError(errors, m->span, "'[cpp_unwind]' cannot weaken a nonthrowing SDK override contract");
                            ok = false;
                        }
                        for(usize pi = 0u; pi < m->params.size(); ++pi)
                            m->params[pi].cppConstReference |= candidate->params[pi].cppConstReference;
                        break;
                    }
                    continue;
                }
                const auto* signature = FindBuiltinReceiverSignature(baseName, m->name);
                if(!signature || signature->arity != m->params.size())
                    continue;
                if(m->allowsCppUnwind && !signature->allowsCppUnwind){
                    reportError(errors, m->span, "'[cpp_unwind]' cannot weaken a nonthrowing SDK override contract");
                    ok = false;
                }
                sdkAllowsUnwind |= signature->allowsCppUnwind;
                for(usize pi = 0u; pi < m->params.size(); ++pi)
                    m->params[pi].cppConstReference |= (signature->cppConstReferenceMask & (1u << pi)) != 0u;
            }
            if(m->allowsCppUnwind && !matchesInterface && !sdkAllowsUnwind){
                reportError(errors, m->span, "'[cpp_unwind]' on an override requires an authored interface contract");
                ok = false;
            }
            if(!matchesInterface && !classDecl.systemTypeName.empty()){
                const ClassRoleContractInfo* sysContract = ContractSemantics::FindClassRoleContract(ClassRoleKind::System);
                if(sysContract && FindBuiltinReceiverMethod(sysContract->implicitInterfaceAdhdName, m->name))
                    matchesInterface = true;
            }
            if(!matchesInterface){
                for(const TypeRef* ifaceType : classDecl.implementedInterfaces){
                    if(!ifaceType)
                        continue;
                    const MStringView ifaceName = m_symbols.resolveBaseTypeName(ifaceType->name);
                    if((!classDecl.concreteBase || classDecl.concreteBase->name != ifaceName) && !m_symbols.findInterface(ifaceName) && !IsNonCopyableBaseInterfaceName(ifaceName)){
                        matchesInterface = true;
                        break;
                    }
                }
            }
            if(!matchesInterface){
                reportError(errors, m->span, "method marked 'override' does not match any method in an implemented interface");
                ok = false;
            }
        }
    }

    b8 hasConstructor = false;
    for(MethodDecl* m : classDecl.methods){
        if(m->isConstructor){
            hasConstructor = true;
            if(m->hasBody && !m->isImported && !checkConstructorInitialization(classDecl, *m, errors))
                ok = false;
        }
    }

    for(usize i = 0u; i < classDecl.fields.size(); ++i){
        FieldDecl* field = classDecl.fields[i];
        if(!field)
            continue;
        for(usize j = 0u; j < i; ++j){
            if(classDecl.fields[j] && classDecl.fields[j]->name == field->name){
                reportError(errors, field->span, "duplicate class member field name");
                ok = false;
                break;
            }
        }
        if(field->type){
            if(!checkTypeRef(*field->type, &classDecl, nullptr, errors))
                ok = false;
            if(!classDecl.isExtern && !classDecl.isImported){
                const MStringView baseFieldType = m_symbols.resolveBaseTypeName(field->type->name);
                if(m_symbols.resolveRefMode(field->type) == ParamPassingMode::None && (m_symbols.findInterface(baseFieldType) || m_symbols.isAbstractBuiltinType(baseFieldType))){
                    reportError(errors, field->span, "class member of 'interface' type must specify 'in' or 'mut'; interfaces cannot be stored by value");
                    ok = false;
                }
                if(TypeLayoutAnalyzer(m_symbols).containsSimdRegisterType(*field->type)){
                    reportError(errors, field->span, "class member cannot hold SIMD calculation type ('VectorSingle', 'VectorDouble', 'MatrixSingle', 'MatrixDouble'); use memory-layout types instead");
                    ok = false;
                }
            }
        }
        const TypeRef* unwrappedFieldType = field->type ? m_symbols.unwrapTypeAlias(field->type) : nullptr;
        if(TypeRef* targetFuncType = TypeResolver(m_symbols).extractUnderlyingFuncType(field->type); targetFuncType && field->defaultExpr && field->defaultExpr->kind == ExprKind::Lambda){
            if(!field->defaultExpr->lambdaCaptures.empty() && targetFuncType->inferredCaptureBytes < s_InferredLambdaCaptureBytes)
                targetFuncType->inferredCaptureBytes = s_InferredLambdaCaptureBytes;
        }
        MethodDecl fieldInitDummy(m_arena);
        MVector<ScopedVarInfo> emptyScope(m_arena);
        if(unwrappedFieldType && !m_symbols.hasTypeSymbol(unwrappedFieldType->name) && RequiresTCharAdaptation(unwrappedFieldType->name)){
            if(field->defaultExpr)
                markStringToPathAdaptation(&classDecl, &fieldInitDummy, emptyScope, *field->defaultExpr);
            for(Expr* arg : field->ctorArgs){
                if(arg)
                    markStringToPathAdaptation(&classDecl, &fieldInitDummy, emptyScope, *arg);
            }
        }
        if(field->defaultExpr || field->hasCtorInit){
            const ParamPassingMode::Enum fieldRefMode = field->type ? m_symbols.resolveRefMode(field->type) : ParamPassingMode::None;
            const b8 savedClassArenaCtx = m_inClassArenaContext;
            m_inClassArenaContext = true;
            if(field->defaultExpr){
                if(!checkExpr(&classDecl, emptyScope, fieldRefMode == ParamPassingMode::Mut, fieldInitDummy, *field->defaultExpr, errors))
                    ok = false;
                if(fieldRefMode == ParamPassingMode::In && isRvalueTemporaryExpr(&classDecl, &fieldInitDummy, *field->defaultExpr, emptyScope)){
                    reportError(errors, field->span, "cannot bind 'in' reference member default initializer to a literal or rvalue temporary");
                    ok = false;
                }
            }
            if(field->hasCtorInit){
                if(!ConstructorArgumentChecker(*this).check(&classDecl, unwrappedFieldType, emptyScope, field->span, fieldInitDummy, field->ctorArgs, errors))
                    ok = false;
            }
            m_inClassArenaContext = savedClassArenaCtx;
        }

        if(!classDecl.isExtern && !classDecl.isImported && !field->isStatic && !hasFieldInlineOrAutoDefault(classDecl, *field) && !hasConstructor){
            reportError(errors, field->span, "class member has no inline default value and class defines no constructor to initialize it");
            ok = false;
        }
    }

    for(usize i = 0u; i < classDecl.methods.size(); ++i){
        MethodDecl* m = classDecl.methods[i];
        if(!m)
            continue;
        if(!classDecl.isExtern && !m->isExtern){
            if(!m->isConstructor && !m->isDestructor && !m->isOperator && !m->name.empty() && !IsAsciiLower(m->name[0])){
                reportError(errors, m->span, "member function name must start with a lowercase letter");
                ok = false;
            }
            for(usize j = 0u; j < i; ++j){
                const MethodDecl* prev = classDecl.methods[j];
                if(
                    prev
                    && prev->isConstructor == m->isConstructor
                    && prev->isDestructor == m->isDestructor
                    && prev->isOperator == m->isOperator
                    && prev->name == m->name
                    && prev->isConst == m->isConst
                    && prev->templateParams.size() == m->templateParams.size()
                    && TypeComparator(m_symbols).doParamListsMatch(prev->params, m->params)
                ){
                    reportError(errors, m->span, "duplicate class method declaration with identical signature");
                    ok = false;
                    break;
                }
            }
        }
        if(!checkCallableSignature(&classDecl, *m, errors))
            ok = false;
        if(!m->hasBody){
            if(!classDecl.isImported && !classDecl.isExtern && !m->isExtern && classDecl.assetTypeName.empty() && classDecl.systemTypeName.empty()){
                if(classDecl.isModular){
                    if(!m->templateParams.empty() || m->hasSynthesizedArenaParams){
                        reportError(errors, m->span, "templated or arena-polymorphic method in '[modular]' class must define its body in the declaring class file");
                        ok = false;
                    }
                }
                else{
                    reportError(errors, m->span, "declared class method has no implementation body");
                    ok = false;
                }
            }
            continue;
        }
        if(m->isImported || m->isExtern || classDecl.isExtern)
            continue;
        const b8 prevRecordHeaders = m_recordGlobalHeaders;
        m_recordGlobalHeaders = true;
        if(!m_parallelValidator.validateMethod(*m, errors))
            ok = false;
        if(!checkMethodBody(&classDecl, *m, errors))
            ok = false;
        m_recordGlobalHeaders = prevRecordHeaders;
    }

    return ok;
}

b8 TypeChecker::checkConstructorInitialization(ClassDecl& classDecl, MethodDecl& ctor, MVector<ParseError>& errors){
    b8 ok = true;
    MVector<MStringView> assignedMembers(m_arena);
    MVector<SourceSpan> assignedSpans(m_arena);
    __hidden_adhd_type_checker_decl::CollectAssignedThisMembers(ctor.body, assignedMembers, assignedSpans);

    for(usize i = 0u; i < assignedMembers.size(); ++i){
        const FieldDecl* field = SymbolTable::FindClassField(classDecl, assignedMembers[i]);
        if(!field || field->declaringClass != &classDecl)
            continue;
        if(field->defaultExpr != nullptr || field->hasCtorInit){
            reportError(errors, assignedSpans[i], "double-initialization is prohibited in adhd; member already has an inline default value");
            ok = false;
        }
    }

    for(const FieldDecl* field : classDecl.fields){
        if(hasFieldInlineOrAutoDefault(classDecl, *field))
            continue;

        if(!__hidden_adhd_type_checker_decl::IsMemberDefinitelyInitializedInStmts(ctor.body, field->name)){
            reportError(errors, ctor.span, "constructor must initialize all class members that do not have an inline default value");
            ok = false;
        }
    }

    return ok;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

