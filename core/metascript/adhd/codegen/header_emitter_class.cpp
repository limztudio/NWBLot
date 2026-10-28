// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_emitter.h"

#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void HeaderEmitter::emitClassDecl(MString& out, const ClassDecl& decl)const{
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
    const MVector<FieldDecl*>& orderedFields,
    const bool forceInlineBody
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
        const bool emitInlineBody = forceInlineBody || method->isInline || !method->templateParams.empty();
        if(method->isInline)
            out.append("NWB_INLINE ");
        else if(method->isNoInline)
            out.append("NWB_NOINLINE ");

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

void HeaderEmitter::emitClassFieldsCpp(MString& out, const MVector<FieldDecl*>& orderedFields)const{
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
            u32 ctorVecLanes = 0u;
            const bool isScalarSplatCtor = !baseFieldType.empty()
                && m_symbols.isSimdVectorType(baseFieldType, ctorVecLanes)
                && field->ctorArgs.size() == 1u && field->ctorArgs[0]
                && field->ctorArgs[0]->resolvedSimdType.empty();
            if(!baseFieldType.empty() && m_symbols.isSimdMatrixType(baseFieldType)){
                if(field->ctorArgs.empty()){
                    out.append("{}");
                }
                else{
                    out.append("{ .raw = { ");
                    for(usize i = 0u; i < field->ctorArgs.size(); ++i){
                        if(i > 0u)
                            out.append(", ");
                        if(field->ctorArgs[i])
                            m_sourceEmitter.emitExpr(out, *field->ctorArgs[i]);
                    }
                    out.append(" } }");
                }
            }
            else if(isScalarSplatCtor){
                out.push_back('{');
                for(u32 lane = 0u; lane < ctorVecLanes; ++lane){
                    if(lane > 0u)
                        out.append(", ");
                    m_sourceEmitter.emitExpr(out, *field->ctorArgs[0]);
                }
                out.push_back('}');
            }
            else{
                out.push_back('{');
                for(usize i = 0u; i < field->ctorArgs.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    if(field->ctorArgs[i])
                        m_sourceEmitter.emitExpr(out, *field->ctorArgs[i]);
                }
                if(field->usesClassArena){
                    if(!field->ctorArgs.empty())
                        out.append(", ");
                    out.append("s_ClassArena");
                }
                out.push_back('}');
            }
        }
        else if(field->usesClassArena){
            out.append("{s_ClassArena}");
        }
        else if(fieldRefMode == ParamPassingMode::None
            && (m_symbols.resolveBaseTypeName(field->type->name) == "Array"
                || m_symbols.findEnum(m_symbols.resolveBaseTypeName(field->type->name)) != nullptr))
        {
            out.append("{}");
        }
        out.append(";\r\n");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

