// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "constructor_argument_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ConstructorArgumentEmitter::emit(MString& out, const MStringView baseTypeName, const Span<const Expr* const> arguments, const MStringView injectedArenaName, const b8 useBracesForList, const b8 emptyUsesBraces)const{
    if(!baseTypeName.empty() && SymbolTable::IsSimdMatrixType(baseTypeName)){
        b8 allZeroArgs = true;
        for(const Expr* arg : arguments){
            if(!arg || !SimdExpressionAnalysis::IsZeroScalarLiteral(*arg, &m_emitter.m_symbols)){
                allZeroArgs = false;
                break;
            }
        }
        if(arguments.empty() || allZeroArgs){
            out.append("{}");
        }
        else if(!useBracesForList && arguments.size() == 1u && arguments[0] && SymbolTable::IsSimdMatrixType(arguments[0]->resolvedSimdType)){
            out.push_back('(');
            m_emitter.emitExpr(out, *arguments[0]);
            out.push_back(')');
        }
        else if(arguments.size() == 1u && arguments[0] && arguments[0]->resolvedSimdType.empty()){
            const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(baseTypeName);
            const u32 elemCount = matInfo ? matInfo->elemCount : 16u;
            out.append("{ .raw = { ");
            for(u32 i = 0u; i < elemCount; ++i){
                if(i > 0u)
                    out.append(", ");
                m_emitter.emitExpr(out, *arguments[0]);
            }
            out.append(" } }");
        }
        else{
            out.append("{ .raw = { ");
            for(usize i = 0u; i < arguments.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                if(arguments[i])
                    m_emitter.emitExpr(out, *arguments[i]);
            }
            out.append(" } }");
        }
        return;
    }

    if(arguments.empty() && injectedArenaName.empty() && emptyUsesBraces){
        out.append("{}");
        return;
    }

    u32 ctorVecLanes = 0u;
    const b8 isStorageOrUnalignedVecCtor = !baseTypeName.empty()
        && (SymbolTable::IsSimdStorageVectorType(baseTypeName, ctorVecLanes) || SymbolTable::IsUnalignedVectorType(baseTypeName, ctorVecLanes));
    if(isStorageOrUnalignedVecCtor){
        b8 allZeroVecArgs = true;
        for(const Expr* arg : arguments){
            if(!arg || !SimdExpressionAnalysis::IsZeroScalarLiteral(*arg, &m_emitter.m_symbols)){
                allZeroVecArgs = false;
                break;
            }
        }
        if(arguments.empty() || allZeroVecArgs){
            out.append("{}");
            return;
        }
    }

    out.push_back(useBracesForList ? '{' : '(');
    const b8 isScalarSplatCtor = !baseTypeName.empty()
        && (SymbolTable::IsSimdVectorType(baseTypeName, ctorVecLanes) || SymbolTable::IsUnalignedVectorType(baseTypeName, ctorVecLanes))
        && arguments.size() == 1u && arguments[0]
        && arguments[0]->resolvedSimdType.empty();
    if(isScalarSplatCtor){
        for(u32 lane = 0u; lane < ctorVecLanes; ++lane){
            if(lane > 0u)
                out.append(", ");
            m_emitter.emitExpr(out, *arguments[0]);
        }
    }
    else if(!useBracesForList && injectedArenaName.empty() && arguments.size() == 1u && arguments[0] && arguments[0]->kind == ExprKind::Call){
        out.push_back('(');
        m_emitter.emitExpr(out, *arguments[0]);
        out.push_back(')');
    }
    else{
        const b8 omitsArena = !baseTypeName.empty() && !m_emitter.m_symbols.hasTypeSymbol(baseTypeName) && OmitsArenaInCtorType(baseTypeName);
        const MStringView effArenaName = omitsArena ? MStringView{} : injectedArenaName;
        const b8 arenaLast = !baseTypeName.empty() && !m_emitter.m_symbols.hasTypeSymbol(baseTypeName) && SymbolTable::IsArenaBackedContainer(baseTypeName) && !IsArenaFirstCtorType(baseTypeName);
        const b8 arenaFirst = !effArenaName.empty() && !arenaLast;
        if(arenaFirst){
            out.append(effArenaName.data(), effArenaName.size());
            if(!arguments.empty())
                out.append(", ");
        }
        for(usize i = 0u; i < arguments.size(); ++i){
            if(i > 0u)
                out.append(", ");
            if(arguments[i])
                m_emitter.emitExpr(out, *arguments[i]);
        }
        if(!effArenaName.empty() && !arenaFirst){
            if(!arguments.empty())
                out.append(", ");
            out.append(effArenaName.data(), effArenaName.size());
        }
    }
    out.push_back(useBracesForList ? '}' : ')');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

