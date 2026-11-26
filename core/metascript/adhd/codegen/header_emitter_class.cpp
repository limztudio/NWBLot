// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_emitter.h"
#include "constructor_argument_emitter.h"
#include "asset_class_header_emitter.h"

#include "reflection_emitter.h"

#include <global/hash_utils.h>
#include "simd_constructor_emitter.h"

#include <core/metascript/adhd/ast/operator_semantics.h>
#include <core/metascript/adhd/sema/asset_type_analyzer.h>
#include <core/metascript/adhd/sema/type_layout_analyzer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_header_emitter_class{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void EmitRecordAlignment(MString& out, const ClassDecl& decl){
    if(decl.recordAlignment){
        out.append("alignas(");
        AppendUnsignedDecimal(out, decl.recordAlignment);
        out.append(") ");
    }
}

void EmitAccessSpecifier(MString& out, const AccessSpecifier::Enum access, const b8 maybeUnusedPrivate){
#define NWB_ADHD_EXPORT_ACCESS_SPECIFIER(adhdKeyword, enumName, cppLabelPrefix, maybeUnusedOnField) \
    if(access == AccessSpecifier::enumName){ \
        out.append((maybeUnusedOnField && maybeUnusedPrivate) ? (cppLabelPrefix "[[maybe_unused]] ") : cppLabelPrefix); \
        return; \
    }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_ACCESS_SPECIFIER
    out.append(maybeUnusedPrivate ? "private:\r\n    [[maybe_unused]] " : "private:\r\n    ");
}

void EmitTypedAssetBaseType(MString& out, const ClassDecl& decl, const SymbolTable& symbols, const CppTypeEmitter& typeEmitter){
    const ClassRoleContractInfo* assetContract = ContractSemantics::FindClassRoleContract(ClassRoleKind::Asset);
    if(assetContract){
        out.append(assetContract->cppBaseTypeOrTemplate.data(), assetContract->cppBaseTypeOrTemplate.size());
        out.push_back('<');
    }
    out.append(decl.name.data(), decl.name.size());
    if(const TypeRef* assetIface = AssetTypeAnalyzer(symbols).findImplementedAssetInterface(decl)){
        out.append(", ");
        typeEmitter.emitTypeCpp(out, *assetIface);
    }
    out.push_back('>');
}

void EmitClassRoleDefineMacro(MString& out, const ClassRoleKind::Enum role, const MStringView typeName, const b8 trailingBlankLine){
    if(const ClassRoleContractInfo* contract = ContractSemantics::FindClassRoleContract(role); contract && !contract->defineMacroName.empty()){
        out.append("    ");
        out.append(contract->defineMacroName.data(), contract->defineMacroName.size());
        out.append("(\"");
        out.append(typeName.data(), typeName.size());
        out.append(trailingBlankLine ? "\")\r\n\r\n" : "\")\r\n");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void HeaderEmitter::emitClassDecl(MString& out, const ClassDecl& decl)const{
    if(!decl.subsystemTypeName.empty()){
        AssetClassHeaderEmitter(m_symbols, m_typeEmitter).emitSubsystem(out, decl);
        return;
    }

    ReflectionEmitter reflectionEmitter(m_symbols, m_typeEmitter);
    const b8 packClass = decl.isSerialize && !decl.recordAlignment && TypeLayoutAnalyzer(m_symbols).shouldPackSerializeClass(decl.orderedFields);

    if(packClass)
        out.append("#pragma pack(push, 1)\r\n");

    m_typeEmitter.emitTemplateHeaderCpp(out, decl.templateParams);
    out.append("class ");
    __hidden_adhd_header_emitter_class::EmitRecordAlignment(out, decl);
    out.append(decl.name.data(), decl.name.size());
    if(!decl.isExtensible)
        out.append(" final");

    const b8 isAssetClass = !decl.assetTypeName.empty();
    const b8 isSystemClass = !decl.systemTypeName.empty();
    const b8 isComponentClass = !decl.componentTypeName.empty();
    const ClassRoleContractInfo* sysContract = ContractSemantics::FindClassRoleContract(ClassRoleKind::System);
    const TypeRef* const assetInterfaceBase = AssetTypeAnalyzer(m_symbols).findImplementedAssetInterface(decl);
    if(isAssetClass || isSystemClass || !decl.implementedInterfaces.empty()){
        out.append(" : ");
        b8 firstBase = true;
        if(isAssetClass){
            out.append("public ");
            __hidden_adhd_header_emitter_class::EmitTypedAssetBaseType(out, decl, m_symbols, m_typeEmitter);
            firstBase = false;
        }
        if(isSystemClass && sysContract){
            if(!firstBase)
                out.append(", ");
            out.append("public ");
            out.append(sysContract->cppBaseTypeOrTemplate.data(), sysContract->cppBaseTypeOrTemplate.size());
            firstBase = false;
        }
        for(usize i = 0u; i < decl.implementedInterfaces.size(); ++i){
            if(decl.implementedInterfaces[i] == assetInterfaceBase)
                continue;
            if(isSystemClass && sysContract && decl.implementedInterfaces[i] && m_symbols.resolveBaseTypeName(decl.implementedInterfaces[i]->name) == sysContract->implicitInterfaceAdhdName)
                continue;
            if(!firstBase)
                out.append(", ");
            firstBase = false;
            out.append("public ");
            if(decl.implementedInterfaces[i])
                m_typeEmitter.emitTypeCpp(out, *decl.implementedInterfaces[i]);
        }
    }
    out.append("{\r\n");
    emitClassCppTypeAliases(out, decl);
    for(const MStringView peer : decl.friendClasses){
        out.append("    friend class ").append(peer).append(";\r\n");
    }
    if(isAssetClass){
        out.append("public:\r\n");
        MVector<MStringView> emittedAliases(m_arena);
        const auto emitAlias = [&](const TypeRef& aliasRef){
            for(const MStringView existing : emittedAliases){
                if(existing == aliasRef.name)
                    return;
            }
            emittedAliases.push_back(aliasRef.name);
            out.append("    using ");
            out.append(aliasRef.name.data(), aliasRef.name.size());
            out.append(" = ");
            m_typeEmitter.emitTypeCpp(out, aliasRef);
            out.append(";\r\n");
        };
        for(usize i = 0u; i < decl.fields.size(); ++i){
            const FieldDecl* field = decl.fields[i];
            if(!field || !field->type)
                continue;
            const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(field->type->name);
            if(!aliasDecl)
                continue;
            if(aliasDecl->aliasedType){
                for(const TypeArg& targ : aliasDecl->aliasedType->templateArgs){
                    if(targ.type && m_symbols.findTypeAlias(targ.type->name))
                        emitAlias(*targ.type);
                }
            }
            emitAlias(*field->type);
        }
        __hidden_adhd_header_emitter_class::EmitClassRoleDefineMacro(out, ClassRoleKind::Asset, decl.assetTypeName, true);
    }
    if(isSystemClass){
        out.append("public:\r\n");
        if(sysContract && !sysContract->defineMacroName.empty()){
            __hidden_adhd_header_emitter_class::EmitClassRoleDefineMacro(out, ClassRoleKind::System, decl.systemTypeName, false);
            out.append("    [[nodiscard]] virtual const ");
            out.append(CodegenSymbols::s_NameCppType.data(), CodegenSymbols::s_NameCppType.size());
            out.append("& systemType()const noexcept override{ return ");
            out.append(sysContract->typeNameStaticField.data(), sysContract->typeNameStaticField.size());
            out.append("; }\r\n\r\n");
        }
    }
    if(isComponentClass){
        out.append("public:\r\n");
        __hidden_adhd_header_emitter_class::EmitClassRoleDefineMacro(out, ClassRoleKind::Component, decl.componentTypeName, true);
    }

    const ClassDecl* prevOwnerClass = m_sourceEmitter.activeOwnerClass();
    const b8 prevOwnerNeedsArena = m_sourceEmitter.activeOwnerNeedsArena();
    m_sourceEmitter.setActiveOwnerClassContext(&decl, decl.needsClassArena);

    emitClassMethodsCpp(out, decl, decl.orderedFields, !decl.templateParams.empty(), decl.needsClassArena);

    if(!decl.orderedFields.empty())
        out.append("\r\n");

    emitClassFieldsCpp(out, decl, decl.orderedFields);

    if(decl.isSerialize || decl.isReflect || !decl.assetTypeName.empty()){
        out.append("\r\n");
        if(decl.templateParams.empty() && decl.assetTypeName.empty())
            reflectionEmitter.emitClassReflectionDeclarations(out, decl);
        else
            reflectionEmitter.emitInlineClassReflection(out, decl, decl.orderedFields, decl.needsClassArena);
    }

    m_sourceEmitter.setActiveOwnerClassContext(prevOwnerClass, prevOwnerNeedsArena);

    out.append("};\r\n");

    if(packClass)
        out.append("#pragma pack(pop)\r\n");

    if(isAssetClass && !assetInterfaceBase){
        if(AssetTypeAnalyzer(m_symbols).findAssetLoadBinaryMethod(decl, false) != nullptr){
            const AssetCodecContractInfo& codec = ContractSemantics::s_AssetCodec;
            const MStringView codecSuffix = ContractSemantics::ResolveAssetCodecSuffix(decl.name);
            out.append("\r\n");
            out.append(codec.defineCodecMacro.data(), codec.defineCodecMacro.size());
            out.push_back('(');
            out.append(decl.name.data(), decl.name.size());
            out.append(codecSuffix.data(), codecSuffix.size());
            out.append(", ");
            out.append(decl.name.data(), decl.name.size());
            out.append(");\r\n");
        }
    }

    out.append("\r\n");
}

void HeaderEmitter::emitClassTemplateSpecialization(MString& out, const ClassTemplateInstantiation& inst, const MStringView localNamespace)const{
    if(!inst.templateDecl || inst.templateDecl->isExtern)
        return;

    const ClassDecl& decl = *inst.templateDecl;
    const MStringView ownerNamespace = decl.cppNamespace.empty() ? localNamespace : decl.cppNamespace;
    CppTypeEmitter canonical(m_symbols);
    canonical.setLocalNamespace(localNamespace);
    canonical.useCanonicalTemplateArguments();
    MString identity(m_arena);
    canonical.appendQualifiedName(identity, ownerNamespace, decl.name);
    canonical.emitTypeArgListCpp(identity, inst.templateArgs);
    MString guard(m_arena);
    guard.append("NWB_ADHD_CLASS_SPECIALIZATION_").append(decl.name).append("_");
    AppendUnsignedDecimal(guard, ComputeFnv64Text(identity));
    out.append("#ifndef ").append(guard).append("\r\n#define ").append(guard).append("\r\n");

    if(ownerNamespace != localNamespace){
        if(!localNamespace.empty())
            out.append("}\r\n");
        if(!ownerNamespace.empty())
            out.append("namespace ").append(ownerNamespace).append("{\r\n");
        m_typeEmitter.setLocalNamespace(localNamespace);
        m_sourceEmitter.setLocalNamespace(localNamespace);
        m_typeEmitter.setCurrentNamespace(ownerNamespace);
        m_sourceEmitter.setCurrentNamespace(ownerNamespace);
    }

    ReflectionEmitter reflectionEmitter(m_symbols, m_typeEmitter);
    const b8 packClass = decl.isSerialize && !decl.recordAlignment && TypeLayoutAnalyzer(m_symbols).shouldPackSerializeClass(inst.orderedFields);

    if(packClass)
        out.append("#pragma pack(push, 1)\r\n");

    out.append("template<>\r\nclass ");
    __hidden_adhd_header_emitter_class::EmitRecordAlignment(out, decl);
    out.append(decl.name.data(), decl.name.size());
    m_typeEmitter.emitTypeArgListCpp(out, inst.templateArgs);
    if(!decl.isExtensible)
        out.append(" final");

    if(!decl.implementedInterfaces.empty()){
        out.append(" : ");
        for(usize i = 0u; i < decl.implementedInterfaces.size(); ++i){
            if(i > 0u)
                out.append(", ");
            out.append("public ");
            if(decl.implementedInterfaces[i]){
                const TypeRef* resolvedIface = m_symbols.substituteTypeParams(*decl.implementedInterfaces[i], decl.templateParams, inst.templateArgs);
                m_typeEmitter.emitTypeCpp(out, *resolvedIface);
            }
        }
    }
    out.append("{\r\n");
    emitClassCppTypeAliases(out, decl);
    for(const MStringView peer : decl.friendClasses){
        out.append("    friend class ").append(peer).append(";\r\n");
    }
    if(!decl.componentTypeName.empty()){
        out.append("public:\r\n");
        __hidden_adhd_header_emitter_class::EmitClassRoleDefineMacro(out, ClassRoleKind::Component, decl.componentTypeName, true);
    }

    const usize aliasCount = decl.templateParams.size() < inst.templateArgs.size() ? decl.templateParams.size() : inst.templateArgs.size();
    if(aliasCount > 0u){
        out.append("private:\r\n");
        for(usize i = 0u; i < aliasCount; ++i){
            const TemplateParamDecl& param = decl.templateParams[i];
            const TypeArg& arg = inst.templateArgs[i];
            if(param.constValueType){
                const MStringView valText = arg.constantOrTypeName();
                out.append("    static constexpr ");
                m_typeEmitter.emitTypeCpp(out, *param.constValueType);
                out.push_back(' ');
                out.append(param.name.data(), param.name.size());
                out.append(" = ");
                m_typeEmitter.emitConstantTextCpp(out, valText);
                out.append(";\r\n");
            }
            else if(arg.type){
                out.append("    using ");
                out.append(param.name.data(), param.name.size());
                out.append(" = ");
                m_typeEmitter.emitTypeCpp(out, *arg.type);
                out.append(";\r\n");
            }
        }
        out.append("\r\n");
    }

    const ClassDecl* prevOwnerClass = m_sourceEmitter.activeOwnerClass();
    const b8 prevOwnerNeedsArena = m_sourceEmitter.activeOwnerNeedsArena();
    const Span<const TemplateParamDecl> prevTemplateParams = m_sourceEmitter.activeTemplateParams();
    const Span<const TypeArg> prevTemplateArgs = m_sourceEmitter.activeTemplateArgs();
    const Span<const FieldDecl* const> prevOrderedFields = m_sourceEmitter.activeOrderedFields();
    m_sourceEmitter.setActiveOwnerClassContext(&decl, inst.needsClassArena);
    m_sourceEmitter.setActiveTemplateContext(decl.templateParams, inst.templateArgs, inst.orderedFields);

    emitClassMethodsCpp(out, decl, inst.orderedFields, true, inst.needsClassArena);

    if(!inst.orderedFields.empty() && !decl.methods.empty())
        out.append("\r\n");

    emitClassFieldsCpp(out, decl, inst.orderedFields);

    if(decl.isSerialize || decl.isReflect || !decl.assetTypeName.empty()){
        out.append("\r\n");
        reflectionEmitter.emitInlineClassReflection(out, decl, inst.orderedFields, inst.needsClassArena);
    }

    m_sourceEmitter.setActiveTemplateContext(prevTemplateParams, prevTemplateArgs, prevOrderedFields);
    m_sourceEmitter.setActiveOwnerClassContext(prevOwnerClass, prevOwnerNeedsArena);

    out.append("};\r\n");
    if(packClass)
        out.append("#pragma pack(pop)\r\n");
    if(ownerNamespace != localNamespace){
        if(!ownerNamespace.empty())
            out.append("}\r\n");
        if(!localNamespace.empty())
            out.append("namespace ").append(localNamespace).append("{\r\n");
        m_typeEmitter.setLocalNamespace({});
        m_sourceEmitter.setLocalNamespace({});
        m_typeEmitter.setCurrentNamespace(localNamespace);
        m_sourceEmitter.setCurrentNamespace(localNamespace);
    }
    out.append("#endif\r\n\r\n");
}

void HeaderEmitter::emitClassMethodsCpp(MString& out, const ClassDecl& decl, const Span<const FieldDecl* const> orderedFields, const b8 forceInlineBody, const b8 classNeedsArena)const{
    const TypeLayoutAnalyzer layoutAnalyzer(m_symbols);
    b8 hasNoCopyBase = decl.isExtensible || decl.concreteBase;
    for(const TypeRef* implIface : decl.implementedInterfaces){
        if(implIface && layoutAnalyzer.interfaceInheritsNoCopy(*implIface)){
            hasNoCopyBase = true;
            break;
        }
    }
    const b8 hasMoveOnlyBase = layoutAnalyzer.classInheritsMoveOnly(decl);

    b8 hasUserConstructor = false;
    b8 hasUserDestructor = false;
    b8 hasUserArenaCopyCtor = false;
    b8 hasUserAssignOp = false;
    for(const MethodDecl* m : decl.methods){
        if(!m)
            continue;
        hasUserDestructor |= m->isDestructor;
        if(m->isConstructor){
            hasUserConstructor = true;
            if(m->params.size() == 1u && m->params[0].type && m->params[0].mode == ParamPassingMode::In && m_symbols.resolveBaseTypeName(m->params[0].type->name) == decl.name)
                hasUserArenaCopyCtor = true;
        }
        if(m->isOperator && OperatorSemantics::MapOperatorSymbolToTokenType(m->name) == TokenType::Equal)
            hasUserAssignOp = true;
    }

    if(decl.isExtensible && !hasUserDestructor)
        out.append("public:\r\n    virtual ~").append(decl.name).append("() = default;\r\n");

    const auto emitDeletedSpecialMembers = [&](const b8 emitCopy, const b8 emitMove, const b8 isAssign){
        const MStringView opPrefix = isAssign ? "& operator=(" : "(";
        if(emitCopy){
            out.append("    ");
            out.append(decl.name.data(), decl.name.size());
            out.append(opPrefix.data(), opPrefix.size());
            out.append("const ");
            out.append(decl.name.data(), decl.name.size());
            out.append("&) = delete;\r\n");
        }
        if(emitMove){
            out.append("    ");
            out.append(decl.name.data(), decl.name.size());
            out.append(opPrefix.data(), opPrefix.size());
            out.append(decl.name.data(), decl.name.size());
            out.append("&&) = delete;\r\n");
        }
    };

    const b8 hasMotherArenaMember = decl.canBorrowMotherArena() && classNeedsArena;
    if(decl.usesStaticClassArena()){
        out.append("private:\r\n");
        out.append("    inline static ::NWB::Core::Alloc::GlobalArena s_ClassArena{::Name(\"");
        out.append(decl.name.data(), decl.name.size());
        out.append("\")};\r\n\r\n");
    }

    if(hasMotherArenaMember){
        out.append("public:\r\n");
        out.append("    using uses_leading_arena_t = ::ArenaBase;\r\n\r\n");
        out.append("private:\r\n");
        out.append("    ::NotNull<::ArenaBase*> __adhd_arena;\r\n\r\n");

        const b8 canCopy = layoutAnalyzer.isClassCopyConstructible(decl, orderedFields);
        const b8 canMove = layoutAnalyzer.isClassMoveConstructible(decl, orderedFields);
        const b8 canCopyAssign = canCopy && layoutAnalyzer.isClassCopyAssignable(decl, orderedFields);
        const b8 canMoveAssign = canMove && layoutAnalyzer.isClassMoveAssignable(decl, orderedFields);

        const b8 nothrowDefaultCtor = layoutAnalyzer.isClassNothrowDefaultConstructible(decl, orderedFields);

        if(!hasUserConstructor || canCopy || canMove || hasNoCopyBase || hasMoveOnlyBase || ((canCopyAssign || canMoveAssign) && !hasUserAssignOp)){
            out.append("public:\r\n");
            if(!hasUserConstructor){
                out.append("    explicit ");
                out.append(decl.name.data(), decl.name.size());
                out.append("(::ArenaBase& ");
                out.append(CodegenSymbols::s_MotherArenaVar.data(), CodegenSymbols::s_MotherArenaVar.size());
                out.append(nothrowDefaultCtor ? ")noexcept : " : ") : ");
                if(decl.concreteBase && decl.concreteBase->canBorrowMotherArena() && decl.concreteBase->needsClassArena)
                    out.append(decl.concreteBase->name).append("(").append(CodegenSymbols::s_MotherArenaVar).append("), ");
                if(!decl.systemTypeName.empty()){
                    if(const ClassRoleContractInfo* sysContract = ContractSemantics::FindClassRoleContract(ClassRoleKind::System)){
                        out.append(sysContract->cppBaseTypeOrTemplate.data(), sysContract->cppBaseTypeOrTemplate.size());
                        out.push_back('(');
                        out.append(CodegenSymbols::s_MotherArenaVar.data(), CodegenSymbols::s_MotherArenaVar.size());
                        out.append("), ");
                    }
                }
                out.append(CodegenSymbols::s_MemberArenaVar.data(), CodegenSymbols::s_MemberArenaVar.size());
                out.push_back('(');
                out.append(CodegenSymbols::s_MakeNotNullFunc.data(), CodegenSymbols::s_MakeNotNullFunc.size());
                out.append("(&");
                out.append(CodegenSymbols::s_MotherArenaVar.data(), CodegenSymbols::s_MotherArenaVar.size());
                out.append(")){}\r\n");
                if(!decl.assetTypeName.empty()){
                    out.append("    ");
                    out.append(decl.name.data(), decl.name.size());
                    out.append("(::ArenaBase& __adhd_mother_arena, const ::Name& __adhd_virtual_path)noexcept : ");
                    __hidden_adhd_header_emitter_class::EmitTypedAssetBaseType(out, decl, m_symbols, m_typeEmitter);
                    out.append("(__adhd_virtual_path), __adhd_arena(::MakeNotNull(&__adhd_mother_arena)){}\r\n");
                }
            }
            if(canCopy || canMove){
                out.append("    ");
                out.append(decl.name.data(), decl.name.size());
                out.append("(const ");
                out.append(decl.name.data(), decl.name.size());
                out.append(canCopy ? "&) = default;\r\n" : "&) = delete;\r\n");

                if(canMove){
                    out.append("    ");
                    out.append(decl.name.data(), decl.name.size());
                    out.append("(");
                    out.append(decl.name.data(), decl.name.size());
                    out.append("&&)noexcept = default;\r\n");
                }
                else{
                    emitDeletedSpecialMembers(false, true, false);
                }

                const auto emitRebindCtor = [&](const b8 isMove){
                    out.append("    ");
                    out.append(decl.name.data(), decl.name.size());
                    out.append("(::ArenaBase& __adhd_mother_arena, ");
                    if(orderedFields.empty() && decl.assetTypeName.empty())
                        out.append("[[maybe_unused]] ");
                    if(!isMove)
                        out.append("const ");
                    out.append(decl.name.data(), decl.name.size());
                    out.append(isMove ? "&& __adhd_rhs) : " : "& __adhd_rhs) : ");
                    if(!decl.assetTypeName.empty()){
                        __hidden_adhd_header_emitter_class::EmitTypedAssetBaseType(out, decl, m_symbols, m_typeEmitter);
                        out.append("(__adhd_rhs.virtualPath()), ");
                    }
                    out.append("__adhd_arena(::MakeNotNull(&__adhd_mother_arena))");
                    for(const FieldDecl* field : orderedFields){
                        if(!field || !field->type)
                            continue;
                        out.append(", ");
                        out.append(field->name.data(), field->name.size());
                        if(m_symbols.resolveRefMode(field->type) != ParamPassingMode::None){
                            out.append("(__adhd_rhs.");
                            out.append(field->name.data(), field->name.size());
                            out.push_back(')');
                        }
                        else{
                            out.append("(::AdhdRebindConstruct<");
                            m_typeEmitter.emitTypeCpp(out, *field->type);
                            out.append(">(__adhd_mother_arena, ");
                            if(isMove)
                                out.append("::Move(");
                            out.append("__adhd_rhs.");
                            out.append(field->name.data(), field->name.size());
                            if(isMove)
                                out.push_back(')');
                            out.append("))");
                        }
                    }
                    out.append("{}\r\n");
                };

                if(canCopy && !hasUserArenaCopyCtor)
                    emitRebindCtor(false);
                if(canMove)
                    emitRebindCtor(true);
            }
            else if(hasNoCopyBase || hasMoveOnlyBase){
                emitDeletedSpecialMembers(true, true, false);
            }
            if((canCopyAssign || canMoveAssign) && !hasUserAssignOp){
                const auto emitAssignOp = [&](const b8 isMove){
                    out.append("    ");
                    out.append(decl.name.data(), decl.name.size());
                    out.append("& operator=(");
                    if(!isMove)
                        out.append("const ");
                    out.append(decl.name.data(), decl.name.size());
                    out.append(isMove ? "&& __adhd_rhs){\r\n" : "& __adhd_rhs){\r\n");
                    if(!orderedFields.empty() || !decl.assetTypeName.empty()){
                        out.append("        if(this != &__adhd_rhs){\r\n");
                        if(!decl.assetTypeName.empty()){
                            const ClassRoleContractInfo* assetContract = ContractSemantics::FindClassRoleContract(ClassRoleKind::Asset);
                            const MStringView assetBaseCpp = assetContract ? CppTypeEmitter::MapBuiltinCppType(assetContract->implicitInterfaceAdhdName) : MStringView{};
                            out.append("            static_cast<");
                            out.append(assetBaseCpp.data(), assetBaseCpp.size());
                            out.append("&>(*this) = __adhd_rhs;\r\n");
                        }
                        for(const FieldDecl* field : orderedFields){
                            if(!field || !field->type)
                                continue;
                            if(m_symbols.resolveRefMode(field->type) != ParamPassingMode::None){
                                out.append("            this->");
                                out.append(field->name.data(), field->name.size());
                                out.append(" = __adhd_rhs.");
                                out.append(field->name.data(), field->name.size());
                                out.append(";\r\n");
                                continue;
                            }
                            if(layoutAnalyzer.isArenaBackedValueWrapper(*field->type))
                                out.append("            ::AdhdAssignWrapped((*this->__adhd_arena), this->");
                            else
                                out.append("            ::AdhdAssign(this->");
                            out.append(field->name.data(), field->name.size());
                            out.append(", ");
                            if(isMove)
                                out.append("::Move(");
                            out.append("__adhd_rhs.");
                            out.append(field->name.data(), field->name.size());
                            if(isMove)
                                out.push_back(')');
                            out.append(");\r\n");
                        }
                        out.append("        }\r\n");
                    }
                    else{
                        out.append("        (void)__adhd_rhs;\r\n");
                    }
                    out.append("        return *this;\r\n    }\r\n");
                };
                if(canCopyAssign)
                    emitAssignOp(false);
                else
                    emitDeletedSpecialMembers(true, false, true);
                if(canMoveAssign)
                    emitAssignOp(true);
                else
                    emitDeletedSpecialMembers(false, true, true);
            }
            else if((hasNoCopyBase || hasMoveOnlyBase) && !hasUserAssignOp){
                emitDeletedSpecialMembers(true, true, true);
            }
            if(!decl.methods.empty())
                out.append("\r\n");
        }
    }
    else if(!decl.assetTypeName.empty()){
        if(!hasUserConstructor){
            out.append("public:\r\n");
            out.append("    ");
            out.append(decl.name.data(), decl.name.size());
            out.append("()noexcept = default;\r\n");
            out.append("    explicit ");
            out.append(decl.name.data(), decl.name.size());
            out.append("(const ::Name& __adhd_virtual_path)noexcept : ");
            __hidden_adhd_header_emitter_class::EmitTypedAssetBaseType(out, decl, m_symbols, m_typeEmitter);
            out.append("(__adhd_virtual_path){}\r\n");
            out.append("    explicit ");
            out.append(decl.name.data(), decl.name.size());
            out.append("(::ArenaBase& __adhd_mother_arena)noexcept{ (void)__adhd_mother_arena; }\r\n");
            out.append("    ");
            out.append(decl.name.data(), decl.name.size());
            out.append("(::ArenaBase& __adhd_mother_arena, const ::Name& __adhd_virtual_path)noexcept : ");
            __hidden_adhd_header_emitter_class::EmitTypedAssetBaseType(out, decl, m_symbols, m_typeEmitter);
            out.append("(__adhd_virtual_path){ (void)__adhd_mother_arena; }\r\n");
            if(!decl.methods.empty())
                out.append("\r\n");
        }
    }
    else if(hasNoCopyBase || hasMoveOnlyBase){
        out.append("public:\r\n");
        if(!hasUserConstructor && decl.systemTypeName.empty()){
            out.append("    ");
            out.append(decl.name.data(), decl.name.size());
            out.append("() = default;\r\n");
        }
        const b8 canMove = hasMoveOnlyBase && layoutAnalyzer.isClassMoveConstructible(decl, orderedFields);
        emitDeletedSpecialMembers(true, !canMove, false);
        if(canMove)
            out.append("    ").append(decl.name).append("(").append(decl.name).append("&&)noexcept = default;\r\n");
        if(!hasUserAssignOp){
            const b8 canMoveAssign = canMove && layoutAnalyzer.isClassMoveAssignable(decl, orderedFields);
            emitDeletedSpecialMembers(true, !canMoveAssign, true);
            if(canMoveAssign)
                out.append("    ").append(decl.name).append("& operator=(").append(decl.name).append("&&)noexcept = default;\r\n");
        }
        if(!decl.methods.empty())
            out.append("\r\n");
    }

    for(const MethodDecl* method : decl.methods){
        if(!method)
            continue;
        __hidden_adhd_header_emitter_class::EmitAccessSpecifier(out, method->access, false);

        if(!method->templateParams.empty() || method->hasSynthesizedArenaParams){
            m_typeEmitter.emitCallableTemplateHeaderCpp(out, *method, true);
            out.append("    ");
        }
        const b8 deferSynthesizedToOutOfClass = decl.templateParams.empty() && !forceInlineBody && method->hasSynthesizedArenaParams;
        const b8 emitInlineBody = !deferSynthesizedToOutOfClass && (forceInlineBody || method->isInline || !method->templateParams.empty());
        m_typeEmitter.emitMethodSignature(out, &decl, *method, true, classNeedsArena);
        if(emitInlineBody && method->hasBody){
            if(method->isConstructor)
                m_sourceEmitter.emitConstructorNotNullInitializers(out, orderedFields, *method);
            out.append("{\r\n");
            m_sourceEmitter.emitMethodBodyBlock(out, *method, 2u);
            out.append("    }\r\n");
        }
        else{
            out.append(";\r\n");
        }
        if(method->exportsRvalueOverload){
            out.append("    ");
            m_typeEmitter.emitTypeCpp(out, *method->returnType);
            out.append(" ").append(method->name).append("(");
            m_typeEmitter.emitTypeCpp(out, *method->params.front().type);
            out.append("&& value)");
            if(method->isConst)
                out.append("const ");
            out.append("noexcept(noexcept(this->").append(method->name).append("(value))){ return this->").append(method->name).append("(value); }\r\n");
        }
        if(method->exportsExplicitBool){
            out.append("    explicit operator b8()const noexcept(noexcept(this->").append(method->name).append("())){ return this->")
                .append(method->name).append("(); }\r\n");
        }
        if(method->exportsTakingAssignment){
            MString valueType(out.get_allocator().arena());
            m_typeEmitter.emitTypeCpp(valueType, *method->params.front().type);
            out.append("    template<typename __AdhdIncoming>\r\n")
                .append("        requires (!::IsSame_V<::Decay_T<__AdhdIncoming>, ").append(decl.name)
                .append("> && ::IsConstructible_V<").append(valueType).append(", __AdhdIncoming&&>)\r\n")
                .append("    ").append(decl.name).append("& operator=(__AdhdIncoming&& incoming)noexcept(noexcept(").append(valueType)
                .append("(::Forward<__AdhdIncoming>(incoming))) && noexcept(this->").append(method->name).append("(::DeclVal<")
                .append(valueType).append("&>()))){\r\n")
                .append("        ").append(valueType).append(" value(::Forward<__AdhdIncoming>(incoming));\r\n")
                .append("        this->").append(method->name).append("(value);\r\n        return *this;\r\n    }\r\n");
        }
    }
}

void HeaderEmitter::emitClassCppTypeAliases(MString& out, const ClassDecl& decl)const{
    if(decl.cppTypeAliases.empty())
        return;
    out.append("public:\r\n");
    for(const TypeAliasDecl* alias : decl.cppTypeAliases){
        out.append("    using ").append(alias->name).append(" = ");
        m_typeEmitter.emitTypeCpp(out, *alias->aliasedType);
        out.append(";\r\n");
    }
    out.append("private:\r\n");
}

void HeaderEmitter::emitClassFieldsCpp(MString& out, const ClassDecl& decl, const Span<const FieldDecl* const> orderedFields)const{
    const b8 prevInFieldInit = m_sourceEmitter.inFieldInitializer();
    m_sourceEmitter.setInFieldInitializer(true);
    const MStringView arenaExpr = decl.usesStaticClassArena()
        ? MStringView("static_cast<::ArenaBase&>(s_ClassArena)")
        : MStringView("(*this->__adhd_arena)");
    const TypeLayoutAnalyzer layoutAnalyzer(m_symbols);
    for(const FieldDecl* field : orderedFields){
        if(!field || !field->type)
            continue;
        __hidden_adhd_header_emitter_class::EmitAccessSpecifier(out, field->access, true);

        m_typeEmitter.emitTypeCpp(out, *field->type);
        out.push_back(' ');
        out.append(field->name.data(), field->name.size());
        const ParamPassingMode::Enum fieldRefMode = m_symbols.resolveRefMode(field->type);
        if(field->defaultExpr){
            if(fieldRefMode != ParamPassingMode::None){
                out.append(" = ::MakeNotNull(::AddressOf(");
                m_sourceEmitter.emitExpr(out, *field->defaultExpr);
                out.append("))");
            }
            else if(field->usesClassArena && field->defaultExpr->kind == ExprKind::StringLiteral){
                if(field->arenaFirstDefaultInit){
                    out.push_back('{');
                    out.append(arenaExpr.data(), arenaExpr.size());
                    out.append(", ");
                    m_sourceEmitter.emitExpr(out, *field->defaultExpr);
                    out.push_back('}');
                }
                else{
                    out.push_back('{');
                    m_sourceEmitter.emitExpr(out, *field->defaultExpr);
                    out.append(", ");
                    out.append(arenaExpr.data(), arenaExpr.size());
                    out.push_back('}');
                }
            }
            else{
                out.append(" = ");
                m_sourceEmitter.emitExpr(out, *field->defaultExpr);
            }
        }
        else if(field->hasCtorInit){
            const MStringView baseFieldType = m_symbols.resolveBaseTypeName(field->type->name);
            if(SymbolTable::IsSimdRegisterType(baseFieldType)){
                out.append(" = ");
                SimdConstructorEmitter(m_sourceEmitter, out, baseFieldType, field->ctorArgs).emit();
            }
            else{
                ConstructorArgumentEmitter(m_sourceEmitter).emit(out, baseFieldType, field->ctorArgs, field->usesClassArena ? arenaExpr : MStringView{}, true, true);
            }
        }
        else if(field->usesClassArena && m_symbols.isSelfDefaultedType(field->type->name, &decl)){
            if(layoutAnalyzer.isFixedArrayNeedingArenaDefaultInit(*field->type)){
                out.append(" = ::AdhdDefaultConstructInArena<");
                m_typeEmitter.emitTypeCpp(out, *field->type);
                out.append(">(");
                out.append(arenaExpr.data(), arenaExpr.size());
                out.push_back(')');
            }
            else{
                out.push_back('{');
                out.append(arenaExpr.data(), arenaExpr.size());
                out.push_back('}');
            }
        }
        else if(fieldRefMode == ParamPassingMode::None){
            const MStringView baseFieldType = m_symbols.resolveBaseTypeName(field->type->name);
            if(
                NeedsBraceDefaultFieldInit(baseFieldType)
                || SymbolTable::IsSimdType(baseFieldType)
                || SymbolTable::IsUnalignedVectorType(baseFieldType)
                || m_symbols.isEnumType(baseFieldType)
                || (m_symbols.findClass(baseFieldType) != nullptr && m_symbols.isSelfDefaultedType(baseFieldType))
            )
                out.append("{}");
        }
        out.append(";\r\n");
    }
    m_sourceEmitter.setInFieldInitializer(prevInFieldInit);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

