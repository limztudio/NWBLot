// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_substitution.h"
#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


TypeRef* TypeSubstitution::SubstituteAliasType(const TypeRef& origType, const Span<const TemplateParamDecl> templateParams, const Span<const TypeArg> templateArgs, MetaArena& arena, List<MString, MetaArena>& ownedTexts){
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
    cloned->funcReturnType = origType.funcReturnType ? SubstituteAliasType(*origType.funcReturnType, templateParams, templateArgs, arena, ownedTexts) : nullptr;
    cloned->inferredCaptureBytes = origType.inferredCaptureBytes;
    cloned->span = origType.span;
    cloned->isFuncType = origType.isFuncType;
    cloned->isAuto = origType.isAuto;
    for(const FuncParamType& fp : origType.funcParams){
        FuncParamType subFp = fp;
        if(fp.type)
            subFp.type = SubstituteAliasType(*fp.type, templateParams, templateArgs, arena, ownedTexts);
        cloned->funcParams.push_back(subFp);
    }
    for(const TypeArg& arg : origType.templateArgs){
        TypeArg subArg = arg;
        if(arg.type){
            b8 matchedConstParam = false;
            for(usize p = 0u; p < count; ++p){
                if(templateParams[p].constValueType && arg.type->name == templateParams[p].name){
                    subArg.type = nullptr;
                    subArg.constantText = templateArgs[p].constantOrTypeName();
                    matchedConstParam = true;
                    break;
                }
            }
            if(!matchedConstParam)
                subArg.type = SubstituteAliasType(*arg.type, templateParams, templateArgs, arena, ownedTexts);
        }
        else if(!arg.constantText.empty()){
            b8 exactMatch = false;
            for(usize p = 0u; p < count; ++p){
                if(templateParams[p].constValueType && arg.constantText == templateParams[p].name){
                    subArg.constantText = templateArgs[p].constantOrTypeName();
                    exactMatch = true;
                    break;
                }
            }
            if(!exactMatch){
                Optional<MString> rewritten;
                usize copiedUntil = 0u;
                usize idx = 0u;
                while(idx < arg.constantText.size()){
                    if(!IsAsciiIdentifierStart(arg.constantText[idx])){
                        ++idx;
                        continue;
                    }
                    const usize start = idx;
                    while(idx < arg.constantText.size() && IsAsciiIdentifierChar(arg.constantText[idx]))
                        ++idx;
                    const MStringView token = arg.constantText.substr(start, idx - start);
                    const b8 isScopedMember = start >= 2u && arg.constantText[start - 1u] == ':' && arg.constantText[start - 2u] == ':';
                    if(isScopedMember)
                        continue;
                    for(usize p = 0u; p < count; ++p){
                        if(templateParams[p].constValueType && token == templateParams[p].name){
                            if(!rewritten)
                                rewritten.emplace(arena);
                            rewritten->append(arg.constantText.data() + copiedUntil, start - copiedUntil);
                            const MStringView replacement = templateArgs[p].constantOrTypeName();
                            rewritten->append(replacement.data(), replacement.size());
                            copiedUntil = idx;
                            break;
                        }
                    }
                }
                if(rewritten){
                    rewritten->append(arg.constantText.data() + copiedUntil, arg.constantText.size() - copiedUntil);
                    ownedTexts.push_back(Move(*rewritten));
                    subArg.constantText = MStringView(ownedTexts.back().data(), ownedTexts.back().size());
                }
            }
        }
        cloned->templateArgs.push_back(subArg);
    }
    return cloned;
}

[[nodiscard]] MethodDecl* TypeSubstitution::SubstituteInterfaceMethod(const SymbolTable& symbols, MethodDecl* method, const b8 hasSubst, const Span<const TemplateParamDecl> templateParams, const Span<const TypeArg> templateArgs, MetaArena& arena){
    if(!method || !hasSubst)
        return method;
    MethodDecl* cloned = AllocateAstNode<MethodDecl>(arena);
    cloned->access = method->access;
    cloned->name = method->name;
    cloned->returnType = method->returnType ? symbols.substituteTypeParams(*method->returnType, templateParams, templateArgs) : nullptr;
    for(const ParamDecl& p : method->params){
        ParamDecl subP = p;
        if(p.type)
            subP.type = symbols.substituteTypeParams(*p.type, templateParams, templateArgs);
        cloned->params.push_back(subP);
    }
    cloned->span = method->span;
    cloned->isOperator = method->isOperator;
    cloned->isConst = method->isConst;
    return cloned;
}


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

