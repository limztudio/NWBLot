// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MStringView TypeChecker::inferExprTypeName(
    ClassDecl* ownerClass,
    const MethodDecl* method,
    const Expr& expr,
    const MVector<ScopedVarInfo>& scope
)const noexcept{
    if(expr.kind == ExprKind::Identifier){
        if(const ScopedVarInfo* local = findScopedVar(scope, expr.text)){
            if(!local->inferredTypeName.empty())
                return local->inferredTypeName;
            if(local->type)
                return m_symbols.resolveBaseTypeName(local->type->name);
        }
        if(const Stmt* gc = m_symbols.findGlobalConstant(expr.text); gc && gc->varType)
            return m_symbols.resolveBaseTypeName(gc->varType->name);
#define NWB_ADHD_EXPORT_GLOBAL_CONSTANT(adhdName, cppExportName, adhdTypeName) \
        if(expr.text == adhdName) return adhdTypeName;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_GLOBAL_CONSTANT
    }
    else if(expr.kind == ExprKind::ThisMember && ownerClass){
        if(const FieldDecl* field = findClassField(*ownerClass, expr.text)){
            if(field->type)
                return m_symbols.resolveBaseTypeName(field->type->name);
        }
    }
    else if(expr.kind == ExprKind::MemberAccess && expr.lhs){
        const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs, scope);
        u32 simdLanes = 0u;
        if(SymbolTable::isSimdVectorType(lhsType, simdLanes) && expr.text.size() == 1u){
            const MChar c = expr.text[0];
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
            if(lhsType == adhdName) return (c == 'w' || c == 'a') ? MStringView(wScalarType) : MStringView(xyzScalarType);
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
            return lhsType.substr(0u, 3u);
        }
        u32 unalignedLanes = 0u;
        if(SymbolTable::isUnalignedVectorType(lhsType, unalignedLanes) && expr.text.size() == 1u)
            return lhsType.substr(0u, 3u);
        if(expr.isSwizzle)
            return lhsType;
        if(const TypeRef* memberType = inferExprTypeRef(ownerClass, method, expr, scope))
            return m_symbols.resolveBaseTypeName(memberType->name);
    }
    else if(expr.kind == ExprKind::Index && expr.lhs){
        const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs, scope);
        u32 simdLanes = 0u;
        if(SymbolTable::isSimdVectorType(lhsType, simdLanes))
            return lhsType.substr(0u, 3u);
        u32 unalignedLanes = 0u;
        if(SymbolTable::isUnalignedVectorType(lhsType, unalignedLanes))
            return lhsType.substr(0u, 3u);
        if(const TypeRef* indexedType = inferExprTypeRef(ownerClass, method, expr, scope))
            return m_symbols.resolveBaseTypeName(indexedType->name);
    }
    else if(expr.kind == ExprKind::Unary && expr.lhs){
        if(const TypeRef* unaryTypeRef = inferExprTypeRef(ownerClass, method, expr, scope))
            return m_symbols.resolveBaseTypeName(unaryTypeRef->name);
        if(expr.op == TokenType::Bang)
            return "b8";
        return inferExprTypeName(ownerClass, method, *expr.lhs, scope);
    }
    else if(expr.kind == ExprKind::Binary){
        if(const TypeRef* binTypeRef = inferExprTypeRef(ownerClass, method, expr, scope))
            return m_symbols.resolveBaseTypeName(binTypeRef->name);
        if(expr.op == TokenType::EqualEqual || expr.op == TokenType::BangEqual
            || expr.op == TokenType::Less || expr.op == TokenType::LessEqual
            || expr.op == TokenType::Greater || expr.op == TokenType::GreaterEqual
            || expr.op == TokenType::AmpersandAmpersand || expr.op == TokenType::PipePipe)
        {
            return "b8";
        }
        const MStringView lhsT = expr.lhs ? inferExprTypeName(ownerClass, method, *expr.lhs, scope) : MStringView{};
        const MStringView rhsT = expr.rhs ? inferExprTypeName(ownerClass, method, *expr.rhs, scope) : MStringView{};
        if(SymbolTable::isSimdType(lhsT))
            return lhsT;
        if(SymbolTable::isSimdType(rhsT))
            return rhsT;
        if(m_symbols.findEnum(lhsT))
            return lhsT;
        if(m_symbols.findEnum(rhsT))
            return rhsT;
        return !lhsT.empty() ? lhsT : rhsT;
    }
    else if(expr.kind == ExprKind::Call && expr.lhs){
        if(const TypeRef* callType = inferExprTypeRef(ownerClass, method, expr, scope))
            return m_symbols.resolveBaseTypeName(callType->name);
        if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const MStringView mName = expr.lhs->text;
            u8 memberRetRule = 0u;
            constexpr u8 s_MemberRetNone = 0u;
            constexpr u8 s_MemberRetVoid = 0u;
            constexpr u8 s_MemberRetUsize = 1u;
            constexpr u8 s_MemberRetBool = 2u;
            constexpr u8 s_MemberRetStringView = 3u;
            constexpr u8 s_MemberRetOptionalOrExpectedValue = 0u;
            constexpr u8 s_MemberRetExpectedError = 0u;
            constexpr u8 s_MemberRetFirstTypeArg = 0u;
            constexpr u8 s_MemberRetAtomicValue = 0u;
#define NWB_ADHD_EXPORT_MEMBER_METHOD(methodName, isMutating, adaptsFirstArgToElement, returnRule) \
            if(mName == methodName) memberRetRule = s_MemberRet##returnRule;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_MEMBER_METHOD
            if(memberRetRule == s_MemberRetUsize)
                return "usize";
            if(memberRetRule == s_MemberRetBool)
                return "b8";
            if(memberRetRule == s_MemberRetStringView)
                return "StringView";
        }
        if(expr.lhs->kind == ExprKind::Identifier){
            const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
            if(m_symbols.findClass(baseCallee) || SymbolTable::isSimdType(baseCallee)
                || SymbolTable::isArenaBackedContainer(baseCallee) || m_symbols.isSelfDefaultedType(baseCallee))
            {
                return baseCallee;
            }
            if(!expr.arguments.empty() && expr.arguments[0]){
                const MStringView fnName = expr.lhs->text;
                u8 returnRule = 0u;
                constexpr u8 s_RulePromoteSimd = 1u;
                constexpr u8 s_RuleLaneScalar = 2u;
                constexpr u8 s_RuleMatrixMultiply = 3u;
                constexpr u8 s_RuleBool = 4u;
#define NWB_ADHD_EXPORT_FUNCTION(adhdName, cppExportName, arity, rule) \
                if(fnName == adhdName) returnRule = s_Rule##rule;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_FUNCTION
                if(returnRule == s_RuleBool)
                    return "b8";
                if(returnRule == s_RuleLaneScalar){
                    const MStringView argType = inferExprTypeName(ownerClass, method, *expr.arguments[0], scope);
                    u32 simdLanes = 0u;
                    if(SymbolTable::isSimdVectorType(argType, simdLanes))
                        return argType.substr(0u, 3u);
                    return argType;
                }
                if(returnRule == s_RuleMatrixMultiply){
                    const MStringView lhsT = inferExprTypeName(ownerClass, method, *expr.arguments[0], scope);
                    if(expr.arguments.size() == 2u && expr.arguments[1]){
                        const MStringView rhsT = inferExprTypeName(ownerClass, method, *expr.arguments[1], scope);
                        u32 rhsLanes = 0u;
                        if(SymbolTable::isSimdMatrixType(lhsT) && SymbolTable::isSimdVectorType(rhsT, rhsLanes))
                            return rhsT;
                    }
                    return lhsT;
                }
                if(returnRule == s_RulePromoteSimd){
                    for(const Expr* arg : expr.arguments){
                        if(!arg)
                            continue;
                        const MStringView argT = inferExprTypeName(ownerClass, method, *arg, scope);
                        if(SymbolTable::isSimdType(argT))
                            return argT;
                    }
                    return inferExprTypeName(ownerClass, method, *expr.arguments[0], scope);
                }
            }
        }
    }
    else if((expr.kind == ExprKind::Cast || expr.kind == ExprKind::BitCast) && expr.targetType){
        return m_symbols.resolveBaseTypeName(expr.targetType->name);
    }
    else if(expr.kind == ExprKind::ScopeAccess && !expr.scopeChain.empty()){
        const MStringView resolvedRoot = m_symbols.resolveBaseTypeName(expr.scopeChain[0]);
        if(m_symbols.findEnum(resolvedRoot))
            return resolvedRoot;
    }
    else if(expr.kind == ExprKind::StringLiteral){
        return "StringView";
    }
    else if(expr.kind == ExprKind::BoolLiteral){
        return "b8";
    }
    else if(expr.kind == ExprKind::FloatLiteral){
        return "f32";
    }
    else if(expr.kind == ExprKind::DoubleLiteral){
        return "f64";
    }
    else if(expr.kind == ExprKind::IntegerLiteral){
        const MStringView t = expr.text;
        b8 hasU = false;
        b8 hasL = false;
        for(usize i = t.size(); i > 0u; --i){
            const MChar c = t[i - 1u];
            if(c == 'u' || c == 'U')
                hasU = true;
            else if(c == 'l' || c == 'L')
                hasL = true;
            else
                break;
        }
        if(hasL)
            return hasU ? "u64" : "i64";
        return hasU ? "u32" : "i32";
    }
    return {};
}

const TypeRef* TypeChecker::inferExprTypeRef(
    ClassDecl* ownerClass,
    const MethodDecl* method,
    const Expr& expr,
    const MVector<ScopedVarInfo>& scope
)const noexcept{
    if(expr.kind == ExprKind::Identifier){
        if(const ScopedVarInfo* local = findScopedVar(scope, expr.text))
            return m_symbols.unwrapTypeAlias(local->type);
        if(const Stmt* gc = m_symbols.findGlobalConstant(expr.text))
            return m_symbols.unwrapTypeAlias(gc->varType);
    }
    else if(expr.kind == ExprKind::ThisMember && ownerClass){
        if(const FieldDecl* field = findClassField(*ownerClass, expr.text))
            return m_symbols.unwrapTypeAlias(field->type);
    }
    else if(expr.kind == ExprKind::MemberAccess && expr.lhs){
        const TypeRef* lhsTypeRef = inferExprTypeRef(ownerClass, method, *expr.lhs, scope);
        const MStringView lhsType = lhsTypeRef
            ? m_symbols.resolveBaseTypeName(lhsTypeRef->name)
            : inferExprTypeName(ownerClass, method, *expr.lhs, scope);
        if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
            if(const FieldDecl* field = findClassField(*cls, expr.text)){
                if(!field->type)
                    return nullptr;
                const TypeRef* ft = (lhsTypeRef && !cls->templateParams.empty() && !lhsTypeRef->templateArgs.empty())
                    ? m_symbols.substituteTypeParams(*field->type, cls->templateParams, lhsTypeRef->templateArgs)
                    : field->type;
                return m_symbols.unwrapTypeAlias(ft);
            }
        }
    }
    else if(expr.kind == ExprKind::Index && expr.lhs){
        const TypeRef* containerType = inferExprTypeRef(ownerClass, method, *expr.lhs, scope);
        if(containerType){
            const MStringView baseName = m_symbols.resolveBaseTypeName(containerType->name);
            b8 isMapContainer = false;
            b8 isIndexableSeq = false;
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap) \
            if constexpr(isReflectedMap){ \
                if(baseName == adhdName) isMapContainer = true; \
            } \
            if constexpr(isIndexableSequence){ \
                if(baseName == adhdName) isIndexableSeq = true; \
            }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
            if(isMapContainer && containerType->templateArgs.size() >= 2u && containerType->templateArgs[1].type)
                return m_symbols.unwrapTypeAlias(containerType->templateArgs[1].type);
            if(isIndexableSeq && !containerType->templateArgs.empty() && containerType->templateArgs[0].type)
                return m_symbols.unwrapTypeAlias(containerType->templateArgs[0].type);
        }
        const MStringView lhsType = containerType
            ? m_symbols.resolveBaseTypeName(containerType->name)
            : inferExprTypeName(ownerClass, method, *expr.lhs, scope);
        const usize oneArg = 1u;
        const b8 recvIsConst = method ? isReceiverConst(ownerClass, *method, *expr.lhs, scope) : false;
        if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
            if(const MethodDecl* opIdx = findClassOperator(*cls, "[]", &oneArg, recvIsConst, ownerClass != cls)){
                if(!opIdx->returnType)
                    return nullptr;
                const TypeRef* rt = (containerType && !cls->templateParams.empty() && !containerType->templateArgs.empty())
                    ? m_symbols.substituteTypeParams(*opIdx->returnType, cls->templateParams, containerType->templateArgs)
                    : opIdx->returnType;
                return m_symbols.unwrapTypeAlias(rt);
            }
        }
        else if(containerType && m_symbols.findInterface(lhsType)){
            if(const MethodDecl* opIdx = m_symbols.findInterfaceMethod(*containerType, "[]", &oneArg))
                return m_symbols.unwrapTypeAlias(opIdx->returnType);
        }
        else if(const TemplateParamDecl* tp = findTemplateParam(ownerClass, method, lhsType)){
            for(const TypeRef* boundRef : tp->interfaceBounds){
                if(!boundRef)
                    continue;
                if(const MethodDecl* opIdx = m_symbols.findInterfaceMethod(*boundRef, "[]", &oneArg))
                    return m_symbols.unwrapTypeAlias(opIdx->returnType);
            }
        }
    }
    else if((expr.kind == ExprKind::Unary || expr.kind == ExprKind::Binary) && expr.lhs && expr.op != TokenType::Equal){
        const MStringView opSym = MapTokenTypeToOperatorSymbol(expr.op);
        if(!opSym.empty()){
            const TypeRef* lhsTypeRef = inferExprTypeRef(ownerClass, method, *expr.lhs, scope);
            const MStringView lhsType = lhsTypeRef
                ? m_symbols.resolveBaseTypeName(lhsTypeRef->name)
                : inferExprTypeName(ownerClass, method, *expr.lhs, scope);
            const usize opArity = (expr.kind == ExprKind::Unary) ? 0u : 1u;
            const b8 recvIsConst = method ? isReceiverConst(ownerClass, *method, *expr.lhs, scope) : false;
            if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
                if(const MethodDecl* opMethod = findClassOperator(*cls, opSym, &opArity, recvIsConst, ownerClass != cls)){
                    if(!opMethod->returnType)
                        return nullptr;
                    const TypeRef* rt = (lhsTypeRef && !cls->templateParams.empty() && !lhsTypeRef->templateArgs.empty())
                        ? m_symbols.substituteTypeParams(*opMethod->returnType, cls->templateParams, lhsTypeRef->templateArgs)
                        : opMethod->returnType;
                    return m_symbols.unwrapTypeAlias(rt);
                }
            }
            else if(lhsTypeRef && m_symbols.findInterface(lhsType)){
                if(const MethodDecl* opMethod = m_symbols.findInterfaceMethod(*lhsTypeRef, opSym, &opArity)){
                    if(opMethod->returnType)
                        return m_symbols.unwrapTypeAlias(opMethod->returnType);
                }
            }
            else if(const TemplateParamDecl* tp = findTemplateParam(ownerClass, method, lhsType)){
                for(const TypeRef* boundRef : tp->interfaceBounds){
                    if(!boundRef)
                        continue;
                    if(const MethodDecl* opMethod = m_symbols.findInterfaceMethod(*boundRef, opSym, &opArity)){
                        if(opMethod->returnType)
                            return m_symbols.unwrapTypeAlias(opMethod->returnType);
                    }
                }
            }
        }
    }
    else if(expr.kind == ExprKind::Call && expr.lhs){
        const usize callArgCount = expr.arguments.size();
        MVector<MStringView> argTypeHints(m_arena);
        MVector<b8> argIsMutHints(m_arena);
        buildCallArgHints(ownerClass, method, expr.arguments, scope, argTypeHints, argIsMutHints);
        if(expr.lhs->kind == ExprKind::Identifier){
            if(const MethodDecl* fn = m_symbols.findFreeFunction(expr.lhs->text, &callArgCount, &argTypeHints, &argIsMutHints)){
                if(!fn->returnType)
                    return nullptr;
                const TypeRef* rt = (!fn->templateParams.empty() && !expr.callTemplateArgs.empty())
                    ? m_symbols.substituteTypeParams(*fn->returnType, fn->templateParams, expr.callTemplateArgs)
                    : fn->returnType;
                return m_symbols.unwrapTypeAlias(rt);
            }
            if(const ScopedVarInfo* local = findScopedVar(scope, expr.lhs->text)){
                if(const TypeRef* ft = m_symbols.unwrapTypeAlias(local->type); ft && ft->isFuncType && ft->funcReturnType)
                    return m_symbols.unwrapTypeAlias(ft->funcReturnType);
                if(local->lambdaExpr && local->lambdaExpr->lambdaReturnType)
                    return m_symbols.unwrapTypeAlias(local->lambdaExpr->lambdaReturnType);
            }
            const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
            if(m_symbols.findClass(baseCallee) || m_symbols.findTypeAlias(expr.lhs->text)
                || SymbolTable::isSimdType(baseCallee) || SymbolTable::isArenaBackedContainer(baseCallee)
                || m_symbols.isSelfDefaultedType(baseCallee))
            {
                TypeRef* constructed = AllocateAstNode<TypeRef>(m_arena);
                constructed->name = expr.lhs->text;
                constructed->templateArgs = expr.callTemplateArgs;
                return m_symbols.unwrapTypeAlias(constructed);
            }
        }
        else if(expr.lhs->kind == ExprKind::ThisMember && ownerClass){
            const b8 isConstThis = method ? method->isConst : false;
            if(const MethodDecl* m = findClassMethod(*ownerClass, expr.lhs->text, &callArgCount, isConstThis, false, &argTypeHints, &argIsMutHints)){
                if(!m->returnType)
                    return nullptr;
                const TypeRef* rt = (!m->templateParams.empty() && !expr.callTemplateArgs.empty())
                    ? m_symbols.substituteTypeParams(*m->returnType, m->templateParams, expr.callTemplateArgs)
                    : m->returnType;
                return m_symbols.unwrapTypeAlias(rt);
            }
            if(const FieldDecl* field = findClassField(*ownerClass, expr.lhs->text)){
                if(const TypeRef* ft = m_symbols.unwrapTypeAlias(field->type); ft && ft->isFuncType && ft->funcReturnType)
                    return m_symbols.unwrapTypeAlias(ft->funcReturnType);
            }
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const TypeRef* recvTypeRef = inferExprTypeRef(ownerClass, method, *expr.lhs->lhs, scope);
            const MStringView lhsType = recvTypeRef
                ? m_symbols.resolveBaseTypeName(recvTypeRef->name)
                : inferExprTypeName(ownerClass, method, *expr.lhs->lhs, scope);
            const MStringView mName = expr.lhs->text;
            if(recvTypeRef && !recvTypeRef->templateArgs.empty() && recvTypeRef->templateArgs[0].type){
                u8 memberRetRule = 0u;
                constexpr u8 s_MemberRetNone = 0u;
                constexpr u8 s_MemberRetVoid = 0u;
                constexpr u8 s_MemberRetUsize = 0u;
                constexpr u8 s_MemberRetBool = 0u;
                constexpr u8 s_MemberRetStringView = 0u;
                constexpr u8 s_MemberRetOptionalOrExpectedValue = 1u;
                constexpr u8 s_MemberRetExpectedError = 2u;
                constexpr u8 s_MemberRetFirstTypeArg = 3u;
                constexpr u8 s_MemberRetAtomicValue = 4u;
#define NWB_ADHD_EXPORT_MEMBER_METHOD(methodName, isMutating, adaptsFirstArgToElement, returnRule) \
                if(mName == methodName) memberRetRule = s_MemberRet##returnRule;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_MEMBER_METHOD
                if(memberRetRule == s_MemberRetOptionalOrExpectedValue && (lhsType == "Optional" || lhsType == "Expected"))
                    return m_symbols.unwrapTypeAlias(recvTypeRef->templateArgs[0].type);
                if(memberRetRule == s_MemberRetExpectedError && lhsType == "Expected" && recvTypeRef->templateArgs.size() >= 2u && recvTypeRef->templateArgs[1].type)
                    return m_symbols.unwrapTypeAlias(recvTypeRef->templateArgs[1].type);
                if(memberRetRule == s_MemberRetFirstTypeArg && (IsSingleTypeParamBuiltinGeneric(lhsType) || IsTypeAndCountBuiltinGeneric(lhsType)))
                    return m_symbols.unwrapTypeAlias(recvTypeRef->templateArgs[0].type);
                if(memberRetRule == s_MemberRetAtomicValue && lhsType == "Atomic")
                    return m_symbols.unwrapTypeAlias(recvTypeRef->templateArgs[0].type);
            }
            if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
                const b8 recvIsConst = method ? isReceiverConst(ownerClass, *method, *expr.lhs->lhs, scope) : false;
                if(const MethodDecl* m = findClassMethod(*cls, mName, &callArgCount, recvIsConst, ownerClass != cls, &argTypeHints, &argIsMutHints)){
                    if(!m->returnType)
                        return nullptr;
                    const TypeRef* rt = m->returnType;
                    if(recvTypeRef && !cls->templateParams.empty() && !recvTypeRef->templateArgs.empty())
                        rt = m_symbols.substituteTypeParams(*rt, cls->templateParams, recvTypeRef->templateArgs);
                    if(!m->templateParams.empty() && !expr.callTemplateArgs.empty())
                        rt = m_symbols.substituteTypeParams(*rt, m->templateParams, expr.callTemplateArgs);
                    return m_symbols.unwrapTypeAlias(rt);
                }
                if(const FieldDecl* field = findClassField(*cls, mName)){
                    const TypeRef* ft = (recvTypeRef && !cls->templateParams.empty() && !recvTypeRef->templateArgs.empty() && field->type)
                        ? m_symbols.substituteTypeParams(*field->type, cls->templateParams, recvTypeRef->templateArgs)
                        : field->type;
                    if(const TypeRef* uft = m_symbols.unwrapTypeAlias(ft); uft && uft->isFuncType && uft->funcReturnType)
                        return m_symbols.unwrapTypeAlias(uft->funcReturnType);
                }
            }
            else if(recvTypeRef && m_symbols.findInterface(lhsType)){
                if(const MethodDecl* m = m_symbols.findInterfaceMethod(*recvTypeRef, mName, &callArgCount, 0u, &argTypeHints, &argIsMutHints); m && m->returnType){
                    const TypeRef* rt = (!m->templateParams.empty() && !expr.callTemplateArgs.empty())
                        ? m_symbols.substituteTypeParams(*m->returnType, m->templateParams, expr.callTemplateArgs)
                        : m->returnType;
                    return m_symbols.unwrapTypeAlias(rt);
                }
            }
            else if(const TemplateParamDecl* tp = findTemplateParam(ownerClass, method, lhsType)){
                for(const TypeRef* boundRef : tp->interfaceBounds){
                    if(!boundRef)
                        continue;
                    if(const MethodDecl* m = m_symbols.findInterfaceMethod(*boundRef, mName, &callArgCount, 0u, &argTypeHints, &argIsMutHints)){
                        if(!m->returnType)
                            return nullptr;
                        const TypeRef* rt = (!m->templateParams.empty() && !expr.callTemplateArgs.empty())
                            ? m_symbols.substituteTypeParams(*m->returnType, m->templateParams, expr.callTemplateArgs)
                            : m->returnType;
                        return m_symbols.unwrapTypeAlias(rt);
                    }
                }
            }
        }
    }
    else if((expr.kind == ExprKind::Cast || expr.kind == ExprKind::BitCast) && expr.targetType){
        return m_symbols.unwrapTypeAlias(expr.targetType);
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_checker_infer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 StmtReferencesIdentifier(const Stmt* stmt, MStringView varName)noexcept;

[[nodiscard]] b8 ExprReferencesIdentifier(const Expr* expr, const MStringView varName)noexcept{
    if(!expr)
        return false;
    if(expr->kind == ExprKind::Identifier && expr->text == varName)
        return true;
    if(expr->kind == ExprKind::Lambda){
        for(const LambdaCapture& cap : expr->lambdaCaptures){
            if(cap.name == varName)
                return true;
        }
        for(const Stmt* child : expr->lambdaBody){
            if(StmtReferencesIdentifier(child, varName))
                return true;
        }
    }
    if(ExprReferencesIdentifier(expr->lhs, varName) || ExprReferencesIdentifier(expr->rhs, varName))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprReferencesIdentifier(arg, varName))
            return true;
    }
    return false;
}

[[nodiscard]] b8 StmtReferencesIdentifier(const Stmt* stmt, const MStringView varName)noexcept{
    if(!stmt)
        return false;
    if(stmt->initStmt && StmtReferencesIdentifier(stmt->initStmt, varName))
        return true;
    if(ExprReferencesIdentifier(stmt->expr, varName)
        || ExprReferencesIdentifier(stmt->condition, varName)
        || ExprReferencesIdentifier(stmt->stepExpr, varName))
    {
        return true;
    }
    for(const Expr* arg : stmt->arguments){
        if(ExprReferencesIdentifier(arg, varName))
            return true;
    }
    for(const NativeBinding& nb : stmt->nativeBindings){
        if(ExprReferencesIdentifier(nb.sourceExpr, varName))
            return true;
    }
    for(const Stmt* child : stmt->statements){
        if(StmtReferencesIdentifier(child, varName))
            return true;
    }
    for(const Stmt* child : stmt->elseStatements){
        if(StmtReferencesIdentifier(child, varName))
            return true;
    }
    for(const SwitchCase& sc : stmt->switchCases){
        if(ExprReferencesIdentifier(sc.matchExpr, varName))
            return true;
        for(const Stmt* child : sc.statements){
            if(StmtReferencesIdentifier(child, varName))
                return true;
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TypeChecker::analyzeBlockMoveAtLastUse(
    const MVector<Stmt*>& statements,
    const MVector<ScopedVarInfo>& scope,
    const usize savedScopeSize
)const noexcept{
    for(usize i = savedScopeSize; i < scope.size(); ++i){
        const ScopedVarInfo& local = scope[i];
        if(local.isParam || local.isConst || (local.type && m_symbols.resolveRefMode(local.type) != ParamPassingMode::None))
            continue;

        usize lastUseIndex = statements.size();
        for(usize s = 0u; s < statements.size(); ++s){
            if(__hidden_adhd_type_checker_infer::StmtReferencesIdentifier(statements[s], local.name))
                lastUseIndex = s;
        }

        const MStringView resolvedLocalType = !local.inferredTypeName.empty()
            ? local.inferredTypeName
            : (local.type ? m_symbols.resolveBaseTypeName(local.type->name) : MStringView{});
        if(SymbolTable::isSimdType(resolvedLocalType))
            continue;

        b8 capturedByMutLambda = false;
        for(usize j = savedScopeSize; j < scope.size(); ++j){
            if(const Expr* lam = scope[j].lambdaExpr){
                for(const LambdaCapture& cap : lam->lambdaCaptures){
                    if(cap.mode == ParamPassingMode::Mut && cap.name == local.name){
                        capturedByMutLambda = true;
                        break;
                    }
                }
            }
            if(capturedByMutLambda)
                break;
        }
        if(capturedByMutLambda || lastUseIndex >= statements.size())
            continue;

        Stmt* lastStmt = statements[lastUseIndex];
        const b8 isDirectCallStmt = (lastStmt->kind == StmtKind::ExprStmt && lastStmt->expr && lastStmt->expr->kind == ExprKind::Call)
            || (lastStmt->kind == StmtKind::VarDecl && lastStmt->name != local.name && lastStmt->expr && lastStmt->expr->kind == ExprKind::Call);
        if(!isDirectCallStmt || __hidden_adhd_type_checker_infer::ExprReferencesIdentifier(lastStmt->expr->lhs, local.name))
            continue;

        usize argRefCount = 0u;
        for(const Expr* arg : lastStmt->expr->arguments){
            if(__hidden_adhd_type_checker_infer::ExprReferencesIdentifier(arg, local.name))
                ++argRefCount;
        }
        if(argRefCount == 1u){
            for(Expr* arg : lastStmt->expr->arguments){
                if(arg && !arg->isMutArg && arg->kind == ExprKind::Identifier && arg->text == local.name)
                    arg->moveAtLastUse = true;
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

