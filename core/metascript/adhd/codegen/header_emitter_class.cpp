// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_emitter.h"
#include "constructor_argument_emitter.h"
#include "asset_class_header_emitter.h"

#include "reflection_emitter.h"
#include "simd_constructor_emitter.h"

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

    if(packClass)
        out.append("#pragma pack(pop)\r\n");

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

    emitClassMethodsCpp(out, decl, inst.orderedFields, true);

    if(!inst.orderedFields.empty() && !decl.methods.empty())
        out.append("\r\n");

    emitClassFieldsCpp(out, inst.orderedFields);

    if(decl.isSerialize || decl.isReflect || !decl.assetTypeName.empty()){
        out.append("\r\n");
        reflectionEmitter.emitInlineClassReflection(out, decl, inst.orderedFields);
    }

    out.append("};\r\n");
    if(packClass)
        out.append("#pragma pack(pop)\r\n");
    out.append("\r\n");
}

void HeaderEmitter::emitClassMethodsCpp(MString& out, const ClassDecl& decl, const Span<const FieldDecl* const> orderedFields, const b8 forceInlineBody)const{
    if(decl.needsClassArena){
        out.append("private:\r\n");
        out.append("    inline static ::NWB::Core::Alloc::GlobalArena s_ClassArena{::Name(\"");
        out.append(decl.name.data(), decl.name.size());
        out.append("\")};\r\n\r\n");
    }

    for(const MethodDecl* method : decl.methods){
        if(!method)
            continue;
        __hidden_adhd_header_emitter_class::EmitAccessSpecifier(out, method->access, false);

        if(!method->templateParams.empty()){
            m_typeEmitter.emitTemplateHeaderCpp(out, method->templateParams);
            out.append("    ");
        }
        const b8 emitInlineBody = forceInlineBody || method->isInline || !method->templateParams.empty();
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
            if(SymbolTable::IsSimdRegisterType(baseFieldType)){
                out.append(" = ");
                SimdConstructorEmitter(m_sourceEmitter, out, baseFieldType, field->ctorArgs).emit();
            }
            else{
                ConstructorArgumentEmitter(m_sourceEmitter).emit(out, baseFieldType, field->ctorArgs, field->usesClassArena ? MStringView("s_ClassArena") : MStringView{}, true, true);
            }
        }
        else if(field->usesClassArena){
            out.append("{s_ClassArena}");
        }
        else if(fieldRefMode == ParamPassingMode::None){
            const MStringView baseFieldType = m_symbols.resolveBaseTypeName(field->type->name);
            if(
                NeedsBraceDefaultFieldInit(baseFieldType)
                || SymbolTable::IsSimdType(baseFieldType)
                || SymbolTable::IsUnalignedVectorType(baseFieldType)
                || m_symbols.findEnum(baseFieldType) != nullptr
                || (m_symbols.findClass(baseFieldType) != nullptr && m_symbols.isSelfDefaultedType(baseFieldType))
            )
                out.append("{}");
        }
        out.append(";\r\n");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

