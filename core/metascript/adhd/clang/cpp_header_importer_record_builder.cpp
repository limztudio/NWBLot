// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpp_header_importer_internal.h"

#include "../ast/keyword_semantics.h"
#include "../ast/operator_semantics.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CppHeaderImportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpp_header_importer_record_builder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ParentChain{
    MVector<MStringView> namespaces;
    MVector<MStringView> classes;
    b8 isInternal = false;

    explicit ParentChain(MetaArena& arena)noexcept
        : namespaces(arena)
        , classes(arena)
    {}
};

[[nodiscard]] ParentChain CollectSemanticParents(MetaArena& arena, CXCursor cursor){
    ParentChain chain(arena);
    MVector<MStringView> nsRev(arena);
    MVector<MStringView> clsRev(arena);

    CXCursor parent = ::clang_getCursorSemanticParent(cursor);
    for(usize guard = 0u; guard < 32u && !::clang_Cursor_isNull(parent); ++guard){
        const CXCursorKind pk = ::clang_getCursorKind(parent);
        if(pk == CXCursor_TranslationUnit || pk == CXCursor_InvalidFile)
            break;

        if(pk == CXCursor_Namespace){
            const MString nsStr = ConsumeClangString(arena, ::clang_getCursorSpelling(parent));
            const MStringView nsView(nsStr.data(), nsStr.size());
            if(CppImporterSemantics::IsInternalIdentifierName(nsView)){
                chain.isInternal = true;
                return chain;
            }
            if(!nsView.empty())
                nsRev.push_back(InternArenaString(arena, nsView));
        }
        else if(pk == CXCursor_StructDecl || pk == CXCursor_ClassDecl || pk == CXCursor_ClassTemplate || pk == CXCursor_ClassTemplatePartialSpecialization){
            const MString clsStr = ConsumeClangString(arena, ::clang_getCursorSpelling(parent));
            const MStringView clsView(clsStr.data(), clsStr.size());
            if(CppImporterSemantics::IsInternalIdentifierName(clsView)){
                chain.isInternal = true;
                return chain;
            }
            clsRev.push_back(InternArenaString(arena, clsView));
        }
        else if(pk != CXCursor_LinkageSpec && pk != CXCursor_UnexposedDecl){
            break;
        }

        parent = ::clang_getCursorSemanticParent(parent);
    }

    for(usize i = nsRev.size(); i > 0u; --i)
        chain.namespaces.push_back(nsRev[i - 1u]);
    for(usize i = clsRev.size(); i > 0u; --i)
        chain.classes.push_back(clsRev[i - 1u]);
    return chain;
}

[[nodiscard]] MStringView BuildNamespaceString(MetaArena& arena, const Span<const MStringView> namespaces){
    if(namespaces.empty())
        return {};
    MString out(arena);
    for(usize i = 0u; i < namespaces.size(); ++i){
        if(i > 0u)
            out.append(ContractSemantics::s_CppScopeSeparator.data(), ContractSemantics::s_CppScopeSeparator.size());
        out.append(namespaces[i].data(), namespaces[i].size());
    }
    return InternArenaString(arena, out);
}

[[nodiscard]] MStringView BuildCppExportName(MetaArena& arena, const Span<const MStringView> namespaces, const Span<const MStringView> classes, const MStringView leaf){
    MString out(arena);
    for(const MStringView ns : namespaces){
        out.append(ContractSemantics::s_CppScopeSeparator.data(), ContractSemantics::s_CppScopeSeparator.size());
        out.append(ns.data(), ns.size());
    }
    for(const MStringView cls : classes){
        out.append(ContractSemantics::s_CppScopeSeparator.data(), ContractSemantics::s_CppScopeSeparator.size());
        out.append(cls.data(), cls.size());
    }
    out.append(ContractSemantics::s_CppScopeSeparator.data(), ContractSemantics::s_CppScopeSeparator.size());
    out.append(leaf.data(), leaf.size());
    return InternArenaString(arena, out);
}

[[nodiscard]] MStringView BuildFlattenedClassName(MetaArena& arena, const Span<const MStringView> classes, const MStringView leaf){
    if(classes.empty())
        return InternArenaString(arena, leaf);
    MString out(arena);
    for(const MStringView cls : classes)
        out.append(cls.data(), cls.size());
    out.append(leaf.data(), leaf.size());
    return InternArenaString(arena, out);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FormatNormalizedAdhdDeclName(MetaArena& arena, CXCursor declCursor, MStringView& outName, MStringView& outNamespace, MStringView& outCppExportName, b8& outIsDirectEnum, b8& outIsFlag){
    const CXCursorKind kind = ::clang_getCursorKind(declCursor);
    const MString leafStr = ConsumeClangString(arena, ::clang_getCursorSpelling(declCursor));
    if(CppImporterSemantics::IsInternalIdentifierName(leafStr) || ::clang_Cursor_isAnonymous(declCursor))
        return false;
    const MStringView leaf = InternArenaString(arena, leafStr);

    const __hidden_cpp_header_importer_record_builder::ParentChain parents = __hidden_cpp_header_importer_record_builder::CollectSemanticParents(arena, declCursor);
    if(parents.isInternal)
        return false;

    outIsDirectEnum = false;
    outIsFlag = false;

    if(kind == CXCursor_EnumDecl){
        const b8 isScoped = ::clang_EnumDecl_isScoped(declCursor) != 0u;
        const b8 isNwbEnumWrapper = !isScoped && parents.classes.empty() && !parents.namespaces.empty() && (leaf == ContractSemantics::s_EnumInnerName || leaf == ContractSemantics::s_FlagInnerName);
        outIsFlag = (leaf == ContractSemantics::s_FlagInnerName);
        if(isNwbEnumWrapper){
            outName = parents.namespaces.back();
            outNamespace = __hidden_cpp_header_importer_record_builder::BuildNamespaceString(arena, Span<const MStringView>(parents.namespaces.data(), parents.namespaces.size() - 1u));
            outCppExportName = __hidden_cpp_header_importer_record_builder::BuildCppExportName(arena, Span<const MStringView>(parents.namespaces.data(), parents.namespaces.size()), {}, leaf);
            outIsDirectEnum = false;
            return true;
        }
        outIsDirectEnum = true;
    }

    outName = __hidden_cpp_header_importer_record_builder::BuildFlattenedClassName(arena, Span<const MStringView>(parents.classes.data(), parents.classes.size()), leaf);
    if(!KeywordSemantics::FindProhibitedTypeKeywordError(outName).empty())
        return false;
    outNamespace = __hidden_cpp_header_importer_record_builder::BuildNamespaceString(arena, Span<const MStringView>(parents.namespaces.data(), parents.namespaces.size()));
    outCppExportName = __hidden_cpp_header_importer_record_builder::BuildCppExportName(arena, Span<const MStringView>(parents.namespaces.data(), parents.namespaces.size()), Span<const MStringView>(parents.classes.data(), parents.classes.size()), leaf);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MethodDecl* ImporterContext::buildMethodDecl(CXCursor cursor, const MStringView ownerClassName, const b8 isTemplate, const ClassDecl* ownerClass){
    const CXCursorKind kind = ::clang_getCursorKind(cursor);
    if(kind == CXCursor_Destructor || kind == CXCursor_ConversionFunction)
        return nullptr;

    const MString rawSpelling = ConsumeClangString(m_arena, ::clang_getCursorSpelling(cursor));
    const MStringView spellingView(rawSpelling.data(), rawSpelling.size());
    if(spellingView.empty() || CppImporterSemantics::IsInternalIdentifierName(spellingView))
        return nullptr;

    const b8 isCtor = (kind == CXCursor_Constructor);
    b8 isOperator = false;
    MStringView methodName = InternArenaString(m_arena, spellingView);
    if(!isCtor && spellingView.starts_with(KeywordSemantics::s_Operator)){
        methodName = mapOperatorToken(spellingView);
        if(methodName.empty() || ownerClassName.empty())
            return nullptr;
        isOperator = true;
    }

    if(isCtor && ownerClass && !ownerClass->isCopyConstructible && ::clang_CXXConstructor_isCopyConstructor(cursor))
        return nullptr;
    if(isCtor && ownerClass && !ownerClass->isMoveConstructible && ::clang_CXXConstructor_isMoveConstructor(cursor))
        return nullptr;
    if(!isCtor && (::clang_CXXConstructor_isCopyConstructor(cursor) || ::clang_CXXConstructor_isMoveConstructor(cursor) || ::clang_CXXMethod_isCopyAssignmentOperator(cursor) || ::clang_CXXMethod_isMoveAssignmentOperator(cursor)))
        return nullptr;

    MethodDecl* decl = AllocateAstNode<MethodDecl>(m_arena);
    decl->access = AccessSpecifier::Public;
    decl->ownerClassName = ownerClassName;
    decl->name = isCtor ? ownerClassName : methodName;
    decl->isConstructor = isCtor;
    decl->isOperator = isOperator;
    decl->isConst = ::clang_CXXMethod_isConst(cursor) != 0u;
    decl->isStatic = ::clang_CXXMethod_isStatic(cursor) != 0u;
    decl->isImported = true;
    decl->isExtern = true;
    decl->requiredHeader = resolveRequiredHeader(cursor);

    if(!isCtor){
        const CXType resultType = ::clang_getCursorResultType(cursor);
        decl->returnType = mapType(resultType, false);
        if(!decl->returnType)
            return nullptr;
        decl->returnsReference = (decl->returnType->refMode != ParamPassingMode::None);
    }

    MVector<CXCursor> paramCursors(m_arena);
    MVector<Pair<MStringView, MStringView>> tparamRenames(m_arena);
    if(isTemplate){
        MVector<CXCursor> children(m_arena);
        visitChildrenList(cursor, children);
        for(const CXCursor child : children){
            const CXCursorKind ck = ::clang_getCursorKind(child);
            if(ck == CXCursor_TemplateTypeParameter || ck == CXCursor_NonTypeTemplateParameter){
                const MString tNameStr = ConsumeClangString(m_arena, ::clang_getCursorSpelling(child));
                TemplateParamDecl tparam(m_arena);
                MStringView paramName = tNameStr.empty() ? CppImporterSemantics::s_TemplateParamFallback : InternArenaString(m_arena, tNameStr);
                if(BuiltinTypeSemantics::IsFuncTypeName(paramName) || !KeywordSemantics::FindProhibitedTypeKeywordError(paramName).empty()){
                    const MStringView safeName = BuiltinTypeSemantics::IsFuncTypeName(paramName) ? CppImporterSemantics::s_ConflictTemplateParamFunc : CppImporterSemantics::s_ConflictTemplateParamOther;
                    tparamRenames.emplace_back(paramName, safeName);
                    paramName = safeName;
                }
                tparam.name = paramName;
                decl->templateParams.push_back(Move(tparam));
            }
            else if(ck == CXCursor_ParmDecl){
                paramCursors.push_back(child);
            }
        }
    }
    else{
        const int numArgs = ::clang_Cursor_getNumArguments(cursor);
        if(numArgs >= 0){
            paramCursors.reserve(static_cast<usize>(numArgs));
            for(int i = 0; i < numArgs; ++i)
                paramCursors.push_back(::clang_Cursor_getArgument(cursor, static_cast<unsigned>(i)));
        }
        else{
            MVector<CXCursor> children(m_arena);
            visitChildrenList(cursor, children);
            for(const CXCursor child : children){
                if(::clang_getCursorKind(child) == CXCursor_ParmDecl)
                    paramCursors.push_back(child);
            }
        }
    }

    if(isOperator){
        usize minOps = 0u;
        usize maxOps = 0u;
        b8 prohibitsUnary = false;
        if(!OperatorSemantics::TryGetMemberOperatorArityRules(methodName, minOps, maxOps, prohibitsUnary))
            return nullptr;
        const usize arity = paramCursors.size();
        if(arity < minOps || arity > maxOps || (prohibitsUnary && arity == 0u))
            return nullptr;
    }

    usize requiredPrefixCount = 0u;
    b8 seenDefault = false;
    for(usize idx = 0u; idx < paramCursors.size(); ++idx){
        const CXCursor parm = paramCursors[idx];
        const CXType parmType = ::clang_getCursorType(parm);
        const TypeCacheKey parmCacheKey{ static_cast<i32>(parmType.kind), 1u, parmType.data[0], parmType.data[1] };
        const auto cachedParmIt = m_typeCache.find(parmCacheKey);
        const b8 knownNonPack = !MapRawBuiltinScalarKind(parmType.kind).empty()
            || parmType.kind == CXType_Enum
            || (cachedParmIt != m_typeCache.end() && cachedParmIt->second != nullptr);
        if(!knownNonPack){
            const MString parmTypeSpelling = ConsumeClangString(m_arena, ::clang_getTypeSpelling(parmType));
            if(MStringView(parmTypeSpelling.data(), parmTypeSpelling.size()).find(CppImporterSemantics::s_VariadicPackToken) != MStringView::npos)
                return nullptr;
        }

        const b8 hasDefault = hasDefaultInitializerToken(parm);
        TypeRef* mappedParamType = mapType(parmType, true);
        if(!mappedParamType){
            if(hasDefault)
                break;
            return nullptr;
        }

        if(!hasDefault && !seenDefault)
            requiredPrefixCount = idx + 1u;
        else if(hasDefault)
            seenDefault = true;

        ParamDecl param;
        const MString pNameStr = ConsumeClangString(m_arena, ::clang_getCursorSpelling(parm));
        const MStringView pNameView(pNameStr.data(), pNameStr.size());
        param.name = !IsAsciiIdentifier(pNameView)
            ? CppImporterSemantics::s_ParamNameFallback
            : InternArenaString(m_arena, pNameStr);
        param.type = mappedParamType;
        param.mode = mappedParamType->refMode;
        decl->params.push_back(param);
    }

    if(seenDefault){
        decl->hasDefaultArgs = true;
        decl->minArgCount = requiredPrefixCount;
    }
    else{
        decl->minArgCount = decl->params.size();
    }

    for(const auto& rename : tparamRenames){
        decl->returnType = RewriteTemplateTypeRefName(m_arena, decl->returnType, rename.first(), rename.second());
        for(ParamDecl& p : decl->params)
            p.type = RewriteTemplateTypeRefName(m_arena, p.type, rename.first(), rename.second());
    }

    return decl;
}

void ImporterContext::inspectBaseAndSpecialMembers(CXCursor classCursor, ClassTraits& traits, HashSet<CursorKey, MetaArena, CursorKeyHash>& visitedTraitBases, const b8 isBasePass){
    if(isBasePass){
        if(!visitedTraitBases.insert(MakeCursorKey(classCursor)).second)
            return;
    }

    MVector<CXCursor> children(m_arena);
    visitChildrenList(classCursor, children);

    for(const CXCursor child : children){
        const CXCursorKind ck = ::clang_getCursorKind(child);
        const CX_CXXAccessSpecifier acc = ::clang_getCXXAccessSpecifier(child);

        if(ck == CXCursor_CXXBaseSpecifier){
            const CXType baseType = ::clang_getCursorType(child);
            const CXCursor baseDecl = ::clang_getTypeDeclaration(baseType);
            const MString baseNameStr = ConsumeClangString(m_arena, ::clang_getCursorSpelling(baseDecl));
            const MStringView baseName(baseNameStr.data(), baseNameStr.size());

            if(baseName == CppImporterSemantics::s_NoCopyBaseClass){
                traits.isCopyConstructible = false;
                traits.isCopyAssignable = false;
                continue;
            }
            if(baseName == CppImporterSemantics::s_NoMoveBaseClass){
                traits.isMoveConstructible = false;
                traits.isMoveAssignable = false;
                continue;
            }

            if(acc == CX_CXXPublic && !::clang_Cursor_isNull(baseDecl) && ::clang_getCursorKind(baseDecl) != CXCursor_NoDeclFound){
                inspectBaseAndSpecialMembers(baseDecl, traits, visitedTraitBases, true);
                traits.publicBaseRecords.push_back(baseDecl);
            }
            continue;
        }

        if(ck == CXCursor_Constructor){
            const b8 accessible = (acc == CX_CXXPublic) && (::clang_CXXMethod_isDeleted(child) == 0u);
            if(::clang_CXXConstructor_isCopyConstructor(child)){
                traits.hasUserDeclaredCopyCtor = true;
                if(!accessible)
                    traits.isCopyConstructible = false;
            }
            if(::clang_CXXConstructor_isMoveConstructor(child)){
                traits.hasUserDeclaredMoveCtor = true;
                if(!accessible)
                    traits.isMoveConstructible = false;
            }
            continue;
        }

        if(ck == CXCursor_CXXMethod){
            const b8 accessible = (acc == CX_CXXPublic) && (::clang_CXXMethod_isDeleted(child) == 0u);
            if(::clang_CXXMethod_isCopyAssignmentOperator(child)){
                traits.hasUserDeclaredCopyAssign = true;
                if(!accessible)
                    traits.isCopyAssignable = false;
            }
            if(::clang_CXXMethod_isMoveAssignmentOperator(child)){
                traits.hasUserDeclaredMoveAssign = true;
                if(!accessible)
                    traits.isMoveAssignable = false;
            }
            continue;
        }

        if(ck == CXCursor_Destructor){
            if(::clang_CXXMethod_isDeleted(child) == 0u)
                traits.hasExplicitDestructor = true;
        }
    }

    if(traits.hasUserDeclaredMoveCtor || traits.hasUserDeclaredMoveAssign){
        if(!traits.hasUserDeclaredCopyCtor)
            traits.isCopyConstructible = false;
        if(!traits.hasUserDeclaredCopyAssign)
            traits.isCopyAssignable = false;
    }
    if(traits.hasUserDeclaredCopyCtor || traits.hasUserDeclaredCopyAssign){
        if(!traits.hasUserDeclaredMoveCtor)
            traits.isMoveConstructible = traits.isCopyConstructible;
        if(!traits.hasUserDeclaredMoveAssign)
            traits.isMoveAssignable = traits.isCopyAssignable;
    }
}

b8 ImporterContext::MethodSignaturesEqual(const MethodDecl& a, const MethodDecl& b)noexcept{
    if(a.isConstructor != b.isConstructor || a.isOperator != b.isOperator || a.isConst != b.isConst || a.isStatic != b.isStatic)
        return false;
    if(a.name != b.name || a.params.size() != b.params.size() || a.templateParams.size() != b.templateParams.size())
        return false;
    for(usize i = 0u; i < a.params.size(); ++i){
        const TypeRef* ta = a.params[i].type;
        const TypeRef* tb = b.params[i].type;
        if((ta == nullptr) != (tb == nullptr))
            return false;
        if(ta && (ta->name != tb->name || a.params[i].mode != b.params[i].mode))
            return false;
    }
    return true;
}

void ImporterContext::addUniqueMethod(ClassDecl& target, MethodDecl* method, ClassImportDedupe& dedupe){
    if(!method)
        return;
    auto [it, inserted] = dedupe.methodsByName.emplace(method->name, MVector<const MethodDecl*>(m_arena));
    for(const MethodDecl* existing : it.value()){
        if(existing && MethodSignaturesEqual(*existing, *method))
            return;
    }
    it.value().push_back(method);
    target.methods.push_back(method);
}

void ImporterContext::tryAddClassField(ClassDecl& targetClass, CXCursor child, CXType recordType, const b8 isStatic, ClassImportDedupe& dedupe){
    const MString fieldNameStr = ConsumeClangString(m_arena, ::clang_getCursorSpelling(child));
    const MStringView fieldNameView(fieldNameStr.data(), fieldNameStr.size());
    if(!IsAsciiIdentifier(fieldNameView) || CppImporterSemantics::IsInternalIdentifierName(fieldNameView))
        return;

    const MStringView fieldName = InternArenaString(m_arena, fieldNameStr);
    if(!dedupe.seenFields.insert(fieldName).second)
        return;

    const CXType ftype = ::clang_getCursorType(child);
    TypeRef* mappedType = mapType(ftype, false);
    if(!mappedType)
        return;
    if(isStatic && (mappedType->refMode != ParamPassingMode::None || BuiltinTypeSemantics::IsVoidTypeName(mappedType->name)))
        return;

    FieldDecl* field = AllocateAstNode<FieldDecl>(m_arena);
    field->access = AccessSpecifier::Public;
    field->type = mappedType;
    field->name = fieldName;
    field->originalIndex = targetClass.fields.size();
    field->isStatic = isStatic;

    if(!isStatic){
        const long long fsize = ::clang_Type_getSizeOf(ftype);
        const long long falign = ::clang_Type_getAlignOf(ftype);
        const long long bitOffset = ::clang_Type_getOffsetOf(recordType, fieldNameStr.c_str());
        if(fsize > 0) field->resolvedSize = static_cast<usize>(fsize);
        if(falign > 0) field->resolvedAlign = static_cast<usize>(falign);
        if(bitOffset >= 0) field->resolvedOffset = static_cast<usize>(bitOffset) / 8u;
    }

    targetClass.fields.push_back(field);
    targetClass.orderedFields.push_back(field);
}

void ImporterContext::importClassMethodsFromCursor(CXCursor classCursor, ClassDecl& targetClass, const b8 isBasePass, ClassImportDedupe& dedupe){
    if(isBasePass){
        if(!dedupe.visitedBaseRecords.insert(MakeCursorKey(classCursor)).second)
            return;
    }

    MVector<CXCursor> children(m_arena);
    visitChildrenList(classCursor, children);

    const CXType recordType = ::clang_getCursorType(classCursor);

    for(const CXCursor child : children){
        const CXCursorKind ck = ::clang_getCursorKind(child);
        const CX_CXXAccessSpecifier acc = ::clang_getCXXAccessSpecifier(child);

        if((ck == CXCursor_StructDecl || ck == CXCursor_ClassDecl) && !isBasePass){
            if(acc == CX_CXXPublic && ::clang_isCursorDefinition(child))
                extractClass(child);
            continue;
        }
        if(ck == CXCursor_EnumDecl && !isBasePass){
            if(acc == CX_CXXPublic && ::clang_isCursorDefinition(child))
                extractEnum(child);
            continue;
        }
        if((ck == CXCursor_TypeAliasDecl || ck == CXCursor_TypedefDecl) && !isBasePass){
            if(acc == CX_CXXPublic)
                extractTypeAlias(child);
            continue;
        }
        if(ck == CXCursor_CXXBaseSpecifier && acc == CX_CXXPublic){
            CXCursor baseDecl = ::clang_getCursorDefinition(::clang_getCursorReferenced(child));
            if(::clang_Cursor_isNull(baseDecl))
                baseDecl = ::clang_getCursorReferenced(child);
            if(!::clang_Cursor_isNull(baseDecl))
                importClassMethodsFromCursor(baseDecl, targetClass, true, dedupe);
            continue;
        }

        if(acc != CX_CXXPublic)
            continue;

        if(ck == CXCursor_FieldDecl && !isBasePass){
            tryAddClassField(targetClass, child, recordType, false, dedupe);
            continue;
        }

        if(ck == CXCursor_VarDecl && !isBasePass){
            tryAddClassField(targetClass, child, recordType, true, dedupe);
            continue;
        }

        if((ck == CXCursor_Constructor && !isBasePass) || ck == CXCursor_CXXMethod || ck == CXCursor_FunctionTemplate){
            if(::clang_CXXMethod_isDeleted(child))
                continue;
            const b8 isTpl = (ck == CXCursor_FunctionTemplate);
            if(isTpl){
                const CXCursorKind innerKind = ::clang_getTemplateCursorKind(child);
                if(innerKind != CXCursor_CXXMethod && innerKind != CXCursor_Constructor)
                    continue;
            }
            MethodDecl* method = buildMethodDecl(child, targetClass.name, isTpl, &targetClass);
            if(method)
                addUniqueMethod(targetClass, method, dedupe);
        }
    }
}

void ImporterContext::extractClass(CXCursor classCursor){
    if(!::clang_isCursorDefinition(classCursor))
        return;

    MStringView adhdName;
    MStringView cppNs;
    MStringView cppExportName;
    b8 dummyDirect = false;
    b8 dummyFlag = false;
    if(!formatDeclName(classCursor, adhdName, cppNs, cppExportName, dummyDirect, dummyFlag))
        return;

    const CXType recordType = ::clang_getCursorType(classCursor);
    const long long sz = ::clang_Type_getSizeOf(recordType);
    const long long al = ::clang_Type_getAlignOf(recordType);

    ClassDecl* cls = FindExistingDeclForMerge(m_classByExportName, m_classByName, adhdName, cppExportName);
    const b8 isNew = (cls == nullptr);
    if(isNew){
        cls = AllocateAstNode<ClassDecl>(m_arena);
        cls->name = adhdName;
    }

    cls->cppNamespace = cppNs;
    cls->cppExportName = cppExportName;
    cls->requiredHeader = resolveRequiredHeader(classCursor);
    cls->isImported = true;
    cls->isExtern = true;
    cls->isAbstract = ::clang_CXXRecord_isAbstract(classCursor) != 0u;
    if(sz > 0){
        cls->totalSize = static_cast<usize>(sz);
        cls->hasExplicitLayout = true;
    }
    if(al > 0)
        cls->totalAlign = static_cast<usize>(al);

    ClassTraits traits(m_arena);
    HashSet<CursorKey, MetaArena, CursorKeyHash> visitedTraitBases(m_arena);
    inspectBaseAndSpecialMembers(classCursor, traits, visitedTraitBases, false);
    cls->isCopyConstructible = traits.isCopyConstructible;
    cls->isMoveConstructible = traits.isMoveConstructible;
    cls->isCopyAssignable = traits.isCopyAssignable;
    cls->isMoveAssignable = traits.isMoveAssignable;
    cls->hasExplicitDestructor = traits.hasExplicitDestructor;

    if(isNew){
        m_unit.classes.push_back(cls);
        m_classByName.emplace(MString(cls->name.data(), cls->name.size(), m_arena), cls);
        m_classByExportName.emplace(MString(cls->cppExportName.data(), cls->cppExportName.size(), m_arena), cls);
    }

    ClassImportDedupe dedupe(m_arena, *cls);
    importClassMethodsFromCursor(classCursor, *cls, false, dedupe);
    for(const CXCursor baseRec : traits.publicBaseRecords){
        const CXCursor def = ::clang_getCursorDefinition(baseRec);
        const CXCursor resolvedBase = ::clang_Cursor_isNull(def) ? baseRec : def;
        importClassMethodsFromCursor(resolvedBase, *cls, true, dedupe);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

