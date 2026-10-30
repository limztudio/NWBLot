// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_emitter.h"

#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void HeaderEmitter::emitClassDecl(MString& out, const ClassDecl& decl)const{
    if(!decl.subsystemTypeName.empty()){
        out.append("class ");
        out.append(decl.name.data(), decl.name.size());
        out.append(" final : public ::NWB::Impl::TypedSubsystem<");
        out.append(decl.name.data(), decl.name.size());
        out.append(">{\r\n");
        out.append("public:\r\n");
        out.append("    NWB_DEFINE_ASSET_TYPE(\"");
        out.append(decl.subsystemTypeName.data(), decl.subsystemTypeName.size());
        out.append("\")\r\n\r\n");

        b8 hasVectorField = false;
        for(const FieldDecl* field : decl.fields){
            if(!field)
                continue;
            MStringView assetName;
            b8 isVectorField = false;
            if(tryResolveSubsystemFieldAssetType(*field, assetName, isVectorField) && isVectorField){
                hasVectorField = true;
                break;
            }
        }

        out.append("public:\r\n");
        if(!hasVectorField){
            out.append("    using ::NWB::Impl::TypedSubsystem<");
            out.append(decl.name.data(), decl.name.size());
            out.append(">::TypedSubsystem;\r\n\r\n");
        }
        else{
            const auto emitVectorFieldInitializers = [&](){
                for(const FieldDecl* field : decl.fields){
                    if(!field)
                        continue;
                    MStringView assetName;
                    b8 isVectorField = false;
                    if(tryResolveSubsystemFieldAssetType(*field, assetName, isVectorField) && isVectorField){
                        out.append("        , ");
                        out.append(field->name.data(), field->name.size());
                        out.append("(arena)\r\n");
                    }
                }
            };
            out.append("    explicit ");
            out.append(decl.name.data(), decl.name.size());
            out.append("(::NWB::Core::Assets::AssetArena& arena)noexcept\r\n");
            out.append("        : TypedSubsystem(arena)\r\n");
            emitVectorFieldInitializers();
            out.append("    {}\r\n");
            out.append("    ");
            out.append(decl.name.data(), decl.name.size());
            out.append("(::NWB::Core::Assets::AssetArena& arena, const ::Name& virtualPath)noexcept\r\n");
            out.append("        : TypedSubsystem(arena, virtualPath)\r\n");
            emitVectorFieldInitializers();
            out.append("    {}\r\n\r\n");
        }

        out.append("public:\r\n");
        for(const FieldDecl* field : decl.fields){
            if(!field)
                continue;
            MStringView assetName;
            b8 isVectorField = false;
            if(!tryResolveSubsystemFieldAssetType(*field, assetName, isVectorField))
                continue;
            out.append(isVectorField ? "    ::NWB::Impl::SubsystemAssetRefVector<" : "    ::NWB::Core::Assets::AssetRef<");
            out.append(assetName.data(), assetName.size());
            out.append("> ");
            out.append(field->name.data(), field->name.size());
            out.append(";\r\n");
        }

        out.append("\r\npublic:\r\n");
        out.append("    static constexpr ::NWB::Impl::SubsystemFieldDescriptor<");
        out.append(decl.name.data(), decl.name.size());
        out.append("> s_Fields[] = {\r\n");
        for(const FieldDecl* field : decl.fields){
            if(!field)
                continue;
            MStringView assetName;
            b8 isVectorField = false;
            if(!tryResolveSubsystemFieldAssetType(*field, assetName, isVectorField))
                continue;
            out.append("        ::NWB::Impl::MakeSubsystemFieldDescriptor<");
            out.append(decl.name.data(), decl.name.size());
            out.append(", ");
            out.append(assetName.data(), assetName.size());
            out.append(", &");
            out.append(decl.name.data(), decl.name.size());
            out.append("::");
            out.append(field->name.data(), field->name.size());
            out.append(">(\"");
            AppendCamelToSnakeCase(out, field->name);
            out.append("\"),\r\n");
        }
        out.append("    };\r\n");
        out.append("};\r\n\r\n");
        return;
    }

    if(!decl.componentAssetTypeName.empty()){
        out.append("class ");
        out.append(decl.name.data(), decl.name.size());
        out.append(" final : public ::NWB::Impl::TypedComponent<");
        out.append(decl.name.data(), decl.name.size());
        out.append(">{\r\n");
        out.append("public:\r\n");
        out.append("    NWB_DEFINE_ASSET_TYPE(\"");
        out.append(decl.componentAssetTypeName.data(), decl.componentAssetTypeName.size());
        out.append("\")\r\n\r\n");
        out.append("public:\r\n    ");
        if(!decl.fields.empty() && decl.fields[0] && decl.fields[0]->type){
            m_typeEmitter.emitTypeCpp(out, *decl.fields[0]->type);
            out.push_back(' ');
            out.append(decl.fields[0]->name.data(), decl.fields[0]->name.size());
            out.append(";\r\n\r\n");
        }
        out.append("public:\r\n");
        out.append("    using ::NWB::Impl::TypedComponent<");
        out.append(decl.name.data(), decl.name.size());
        out.append(">::TypedComponent;\r\n\r\n");
        out.append("public:\r\n");
        out.append("    [[nodiscard]] virtual b8 loadComponentPayload(Span<const u8> payload)override;\r\n");
        out.append("    [[nodiscard]] virtual b8 validatePayload()const override;\r\n");
        out.append("    virtual void applyToEntity(::NWB::Core::ECS::Entity& entity)const override{\r\n");
        out.append("        entity.addComponent<");
        if(!decl.fields.empty() && decl.fields[0] && decl.fields[0]->type)
            m_typeEmitter.emitTypeCpp(out, *decl.fields[0]->type);
        out.append(">() = component;\r\n");
        out.append("    }\r\n");
        out.append("#if defined(NWB_COOK)\r\n");
        out.append("    [[nodiscard]] virtual b8 serializeComponentPayload(::NWB::Core::Assets::AssetBytes& outPayload)const override;\r\n");
        out.append("#endif\r\n");
        out.append("};\r\n\r\n");
        return;
    }

    ReflectionEmitter reflectionEmitter(m_symbols);

    if(decl.isSerialize)
        out.append("#pragma pack(push, 1)\r\n");

    m_typeEmitter.emitTemplateHeaderCpp(out, decl.templateParams);
    out.append("class ");
    out.append(decl.name.data(), decl.name.size());
    out.append(" final");

    if(!decl.implementedInterfaces.empty()){
        out.append(" : ");
        for(usize i = 0u; i < decl.implementedInterfaces.size(); ++i){
            if(i > 0u)
                out.append(", ");
            out.append("public ");
            if(decl.implementedInterfaces[i])
                m_typeEmitter.emitTypeCpp(out, *decl.implementedInterfaces[i]);
        }
    }
    out.append("{\r\n");

    emitClassMethodsCpp(out, decl, decl.orderedFields, !decl.templateParams.empty());

    if(!decl.orderedFields.empty())
        out.append("\r\n");

    emitClassFieldsCpp(out, decl.orderedFields);

    if(decl.isSerialize || decl.isReflect || !decl.assetTypeName.empty()){
        out.append("\r\n");
        if(decl.templateParams.empty())
            reflectionEmitter.emitClassReflectionDeclarations(out, decl);
        else
            reflectionEmitter.emitInlineClassReflection(out, decl, decl.orderedFields);
    }

    out.append("};\r\n");

    if(decl.isSerialize)
        out.append("#pragma pack(pop)\r\n");

    out.append("\r\n");
}

void HeaderEmitter::emitClassTemplateSpecialization(MString& out, const ClassTemplateInstantiation& inst)const{
    if(!inst.templateDecl)
        return;

    const ClassDecl& decl = *inst.templateDecl;
    ReflectionEmitter reflectionEmitter(m_symbols);

    if(decl.isSerialize)
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
                const TypeRef* resolvedIface = m_symbols.substituteTypeParams(
                    *decl.implementedInterfaces[i],
                    decl.templateParams,
                    inst.templateArgs
                );
                m_typeEmitter.emitTypeCpp(out, *resolvedIface);
            }
        }
    }
    out.append("{\r\n");

    const usize aliasCount = decl.templateParams.size() < inst.templateArgs.size()
        ? decl.templateParams.size()
        : inst.templateArgs.size();
    if(aliasCount > 0u){
        out.append("private:\r\n");
        for(usize i = 0u; i < aliasCount; ++i){
            const TemplateParamDecl& param = decl.templateParams[i];
            const TypeArg& arg = inst.templateArgs[i];
            if(param.constValueType){
                const MStringView valText = arg.type ? arg.type->name : arg.constantText;
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

    emitClassMethodsCpp(out, decl, inst.orderedFields, true);

    if(!inst.orderedFields.empty() && !decl.methods.empty())
        out.append("\r\n");

    emitClassFieldsCpp(out, inst.orderedFields);

    out.append("\r\n");
    reflectionEmitter.emitInlineClassReflection(out, decl, inst.orderedFields);

    out.append("};\r\n");
    if(decl.isSerialize)
        out.append("#pragma pack(pop)\r\n");
    out.append("\r\n");
}

void HeaderEmitter::emitClassMethodsCpp(
    MString& out,
    const ClassDecl& decl,
    const Span<const FieldDecl* const> orderedFields,
    const b8 forceInlineBody
)const{
    if(decl.needsClassArena){
        out.append("private:\r\n");
        out.append("    inline static ::NWB::Core::Alloc::GlobalArena s_ClassArena{::Name(\"");
        out.append(decl.name.data(), decl.name.size());
        out.append("\")};\r\n\r\n");
    }

    for(const MethodDecl* method : decl.methods){
        if(!method)
            continue;
        if(method->access == AccessSpecifier::Public)
            out.append("public:\r\n    ");
        else if(method->access == AccessSpecifier::Protected)
            out.append("protected:\r\n    ");
        else
            out.append("private:\r\n    ");

        if(!method->templateParams.empty()){
            m_typeEmitter.emitTemplateHeaderCpp(out, method->templateParams);
            out.append("    ");
        }
        const b8 emitInlineBody = forceInlineBody || method->isInline || !method->templateParams.empty();
        if(emitInlineBody && method->hasBody){
            if(method->isForceInline)
                out.append("NWB_FORCE_INLINE ");
            else if(method->isNoInline)
                out.append("NWB_NOINLINE ");
        }
        m_typeEmitter.emitMethodSignature(out, &decl, *method, true);
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

void HeaderEmitter::emitClassFieldsCpp(MString& out, const Span<const FieldDecl* const> orderedFields)const{
    for(const FieldDecl* field : orderedFields){
        if(!field || !field->type)
            continue;
        if(field->access == AccessSpecifier::Public)
            out.append("public:\r\n    ");
        else if(field->access == AccessSpecifier::Protected)
            out.append("protected:\r\n    ");
        else
            out.append("private:\r\n    [[maybe_unused]] ");

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
                out.push_back('{');
                m_sourceEmitter.emitExpr(out, *field->defaultExpr);
                out.append(", s_ClassArena}");
            }
            else{
                out.append(" = ");
                m_sourceEmitter.emitExpr(out, *field->defaultExpr);
            }
        }
        else if(field->hasCtorInit){
            const MStringView baseFieldType = m_symbols.resolveBaseTypeName(field->type->name);
            m_sourceEmitter.emitCtorOrCallArgs(
                out,
                baseFieldType,
                field->ctorArgs,
                field->usesClassArena ? MStringView("s_ClassArena") : MStringView{},
                true,
                true
            );
        }
        else if(field->usesClassArena){
            out.append("{s_ClassArena}");
        }
        else if(fieldRefMode == ParamPassingMode::None){
            const MStringView baseFieldType = m_symbols.resolveBaseTypeName(field->type->name);
            if(baseFieldType == "Array"
                || baseFieldType == "StringView"
                || SymbolTable::isUnalignedVectorType(baseFieldType)
                || m_symbols.findEnum(baseFieldType) != nullptr
                || m_symbols.findClass(baseFieldType) != nullptr)
            {
                out.append("{}");
            }
        }
        out.append(";\r\n");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

