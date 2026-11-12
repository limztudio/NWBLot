// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_emitter.h"
#include "constructor_argument_emitter.h"
#include "asset_class_header_emitter.h"

#include "reflection_emitter.h"
#include "simd_constructor_emitter.h"

#include <core/metascript/adhd/ast/operator_semantics.h>
#include <core/metascript/adhd/sema/type_layout_analyzer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_header_emitter_class{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void HeaderEmitter::emitClassDecl(MString& out, const ClassDecl& decl)const{
    if(!decl.subsystemTypeName.empty()){
        AssetClassHeaderEmitter(m_symbols, m_typeEmitter).emitSubsystem(out, decl);
        return;
    }
    if(!decl.componentAssetTypeName.empty()){
        AssetClassHeaderEmitter(m_symbols, m_typeEmitter).emitComponent(out, decl);
        return;
    }

    ReflectionEmitter reflectionEmitter(m_symbols);
    const b8 packClass = decl.isSerialize && TypeLayoutAnalyzer(m_symbols).shouldPackSerializeClass(decl.orderedFields);

    if(packClass)
        out.append("#pragma pack(push, 1)\r\n");

    m_typeEmitter.emitTemplateHeaderCpp(out, decl.templateParams);
    out.append("class ");
    out.append(decl.name.data(), decl.name.size());
    out.append(" final");

    const b8 isAssetClass = !decl.assetTypeName.empty();
    if(isAssetClass || !decl.implementedInterfaces.empty()){
        out.append(" : ");
        b8 firstBase = true;
        if(isAssetClass){
            out.append("public ::NWB::Core::Assets::TypedAsset<");
            out.append(decl.name.data(), decl.name.size());
            out.push_back('>');
            firstBase = false;
        }
        for(usize i = 0u; i < decl.implementedInterfaces.size(); ++i){
            if(!firstBase)
                out.append(", ");
            firstBase = false;
            out.append("public ");
            if(decl.implementedInterfaces[i])
                m_typeEmitter.emitTypeCpp(out, *decl.implementedInterfaces[i]);
        }
    }
    out.append("{\r\n");
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
        out.append("    NWB_DEFINE_ASSET_TYPE(\"");
        out.append(decl.assetTypeName.data(), decl.assetTypeName.size());
        out.append("\")\r\n\r\n");
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

    if(isAssetClass){
        b8 hasLoadBinary = false;
        for(const MethodDecl* method : decl.methods){
            if(method && !method->isConstructor && !method->isDestructor && !method->isOperator && method->name == "loadBinary" && method->params.size() == 1u){
                hasLoadBinary = true;
                break;
            }
        }
        if(hasLoadBinary){
            out.append("\r\nNWB_DEFINE_ASSET_CODEC(");
            out.append(decl.name.data(), decl.name.size());
            out.append(decl.name.ends_with("Asset") ? "Codec, " : "AssetCodec, ");
            out.append(decl.name.data(), decl.name.size());
            out.append(");\r\n");
        }
    }

    out.append("\r\n");
}

void HeaderEmitter::emitClassTemplateSpecialization(MString& out, const ClassTemplateInstantiation& inst)const{
    if(!inst.templateDecl)
        return;

    const ClassDecl& decl = *inst.templateDecl;
    ReflectionEmitter reflectionEmitter(m_symbols);
    const b8 packClass = decl.isSerialize && TypeLayoutAnalyzer(m_symbols).shouldPackSerializeClass(inst.orderedFields);

    if(packClass)
        out.append("#pragma pack(push, 1)\r\n");

    out.append("template<>\r\nclass ");
    out.append(decl.name.data(), decl.name.size());
    m_typeEmitter.emitTypeArgListCpp(out, inst.templateArgs);
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
                out.append(valText.data(), valText.size());
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
    out.append("\r\n");
}

void HeaderEmitter::emitClassMethodsCpp(MString& out, const ClassDecl& decl, const Span<const FieldDecl* const> orderedFields, const b8 forceInlineBody, const b8 classNeedsArena)const{
    const b8 hasMotherArenaMember = !decl.isArenaOwner && !decl.isSerialize && decl.subsystemTypeName.empty() && decl.componentAssetTypeName.empty() && classNeedsArena;
    if(decl.isArenaOwner){
        out.append("private:\r\n");
        out.append("    inline static ::NWB::Core::Alloc::GlobalArena s_ClassArena{::Name(\"");
        out.append(decl.name.data(), decl.name.size());
        out.append("\")};\r\n\r\n");
    }
    else if(hasMotherArenaMember){
        out.append("public:\r\n");
        out.append("    using uses_leading_arena_t = ::ArenaBase;\r\n\r\n");
        out.append("private:\r\n");
        out.append("    ::NotNull<::ArenaBase*> __adhd_arena;\r\n\r\n");

        b8 hasUserConstructor = false;
        b8 hasUserArenaCopyCtor = false;
        b8 hasUserAssignOp = false;
        for(const MethodDecl* m : decl.methods){
            if(!m)
                continue;
            if(m->isConstructor){
                hasUserConstructor = true;
                if(m->params.size() == 1u && m->params[0].type && m->params[0].mode == ParamPassingMode::In && m_symbols.resolveBaseTypeName(m->params[0].type->name) == decl.name)
                    hasUserArenaCopyCtor = true;
            }
            if(m->isOperator && OperatorSemantics::MapOperatorSymbolToTokenType(m->name) == TokenType::Equal)
                hasUserAssignOp = true;
        }

        const TypeLayoutAnalyzer layoutAnalyzer(m_symbols);
        const b8 canCopy = layoutAnalyzer.isClassCopyConstructible(decl, orderedFields);
        const b8 canMove = layoutAnalyzer.isClassMoveConstructible(decl, orderedFields);
        const b8 canCopyAssign = canCopy && layoutAnalyzer.isClassCopyAssignable(decl, orderedFields);
        const b8 canMoveAssign = canMove && layoutAnalyzer.isClassMoveAssignable(decl, orderedFields);

        if(!hasUserConstructor || canCopy || canMove || ((canCopyAssign || canMoveAssign) && !hasUserAssignOp)){
            out.append("public:\r\n");
            if(!hasUserConstructor){
                out.append("    explicit ");
                out.append(decl.name.data(), decl.name.size());
                out.append("(::ArenaBase& __adhd_mother_arena)noexcept : __adhd_arena(::MakeNotNull(&__adhd_mother_arena)){}\r\n");
                if(!decl.assetTypeName.empty()){
                    out.append("    ");
                    out.append(decl.name.data(), decl.name.size());
                    out.append("(::ArenaBase& __adhd_mother_arena, const ::Name& __adhd_virtual_path)noexcept : ::NWB::Core::Assets::TypedAsset<");
                    out.append(decl.name.data(), decl.name.size());
                    out.append(">(__adhd_virtual_path), __adhd_arena(::MakeNotNull(&__adhd_mother_arena)){}\r\n");
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

                const auto emitRebindCtor = [&](const b8 isMove){
                    out.append("    ");
                    out.append(decl.name.data(), decl.name.size());
                    out.append("(::ArenaBase& __adhd_mother_arena, ");
                    if(!isMove)
                        out.append("const ");
                    out.append(decl.name.data(), decl.name.size());
                    out.append(isMove ? "&& __adhd_rhs)noexcept : " : "& __adhd_rhs)noexcept : ");
                    if(!decl.assetTypeName.empty()){
                        out.append("::NWB::Core::Assets::TypedAsset<");
                        out.append(decl.name.data(), decl.name.size());
                        out.append(">(__adhd_rhs.virtualPath()), ");
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
            if((canCopyAssign || canMoveAssign) && !hasUserAssignOp){
                const auto emitAssignOp = [&](const b8 isMove){
                    out.append("    ");
                    out.append(decl.name.data(), decl.name.size());
                    out.append("& operator=(");
                    if(!isMove)
                        out.append("const ");
                    out.append(decl.name.data(), decl.name.size());
                    out.append(isMove ? "&& __adhd_rhs)noexcept{\r\n" : "& __adhd_rhs)noexcept{\r\n");
                    if(!orderedFields.empty() || !decl.assetTypeName.empty()){
                        out.append("        if(this != &__adhd_rhs){\r\n");
                        if(!decl.assetTypeName.empty())
                            out.append("            static_cast<::NWB::Core::Assets::IAsset&>(*this) = __adhd_rhs;\r\n");
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
                            const TypeRef* unwrappedField = m_symbols.unwrapTypeAlias(field->type);
                            const b8 isOptionalArenaField = unwrappedField
                                && unwrappedField->refMode == ParamPassingMode::None
                                && IsOptionalContainerType(unwrappedField->name)
                                && !unwrappedField->templateArgs.empty()
                                && unwrappedField->templateArgs[0].type
                                && layoutAnalyzer.requiresMotherArena(*unwrappedField->templateArgs[0].type);
                            if(isOptionalArenaField)
                                out.append("            ::AdhdAssignOptional((*this->__adhd_arena), this->");
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
                if(canCopyAssign){
                    emitAssignOp(false);
                }
                else{
                    out.append("    ");
                    out.append(decl.name.data(), decl.name.size());
                    out.append("& operator=(const ");
                    out.append(decl.name.data(), decl.name.size());
                    out.append("&) = delete;\r\n");
                }
                if(canMoveAssign)
                    emitAssignOp(true);
            }
            if(!decl.methods.empty())
                out.append("\r\n");
        }
    }
    else if(!decl.assetTypeName.empty()){
        b8 hasUserConstructor = false;
        for(const MethodDecl* m : decl.methods){
            if(m && m->isConstructor){
                hasUserConstructor = true;
                break;
            }
        }
        if(!hasUserConstructor){
            out.append("public:\r\n");
            out.append("    ");
            out.append(decl.name.data(), decl.name.size());
            out.append("()noexcept = default;\r\n");
            out.append("    explicit ");
            out.append(decl.name.data(), decl.name.size());
            out.append("(const ::Name& __adhd_virtual_path)noexcept : ::NWB::Core::Assets::TypedAsset<");
            out.append(decl.name.data(), decl.name.size());
            out.append(">(__adhd_virtual_path){}\r\n");
            out.append("    explicit ");
            out.append(decl.name.data(), decl.name.size());
            out.append("(::ArenaBase& __adhd_mother_arena)noexcept{ (void)__adhd_mother_arena; }\r\n");
            out.append("    ");
            out.append(decl.name.data(), decl.name.size());
            out.append("(::ArenaBase& __adhd_mother_arena, const ::Name& __adhd_virtual_path)noexcept : ::NWB::Core::Assets::TypedAsset<");
            out.append(decl.name.data(), decl.name.size());
            out.append(">(__adhd_virtual_path){ (void)__adhd_mother_arena; }\r\n");
            if(!decl.methods.empty())
                out.append("\r\n");
        }
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
    }
}

void HeaderEmitter::emitClassFieldsCpp(MString& out, const ClassDecl& decl, const Span<const FieldDecl* const> orderedFields)const{
    const b8 prevInFieldInit = m_sourceEmitter.inFieldInitializer();
    m_sourceEmitter.setInFieldInitializer(true);
    const MStringView arenaExpr = decl.isArenaOwner
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
                out.append(" = ::MakeNotNull(&(");
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
                || m_symbols.findEnum(baseFieldType) != nullptr
                || FindBuiltinEnum(baseFieldType) != nullptr
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

