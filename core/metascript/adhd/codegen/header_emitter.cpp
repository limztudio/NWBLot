// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_emitter.h"

#include "reflection_emitter.h"
#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_header_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendUnsigned(MString& out, usize value){
    char buf[32];
    usize idx = 0u;
    if(value == 0u){
        out.push_back('0');
        return;
    }
    while(value > 0u && idx < sizeof(buf)){
        buf[idx++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    }
    while(idx > 0u)
        out.push_back(buf[--idx]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MString HeaderEmitter::emitHeader(const TranslationUnit& unit, const MStringView namespacePath)const{
    MString out(m_arena);
    out.reserve(4096u);

    SourceEmitter sourceEmitter(m_arena, m_symbols, *this);
    ReflectionEmitter reflectionEmitter(m_arena, m_symbols, *this);

    out.append("// limztudio@gmail.com\r\n");
    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");
    out.append("#pragma once\r\n\r\n\r\n");
    out.append("#include <global/global.h>\r\n");
    out.append("#include <global/simdmath.h>\r\n");
    out.append("#include <global/inplace_function.h>\r\n");
    out.append("#include <global/not_null.h>\r\n");
    out.append("#include <core/alloc/global.h>\r\n");
    out.append("#include <core/alloc/scratch.h>\r\n");

    for(const MString& inc : unit.includedAdhdFiles){
        out.append("#include \"");
        out.append(inc.data(), inc.size());
        out.append(".adhd.h\"\r\n");
    }

    if(!unit.topLevelNativeBlocks.empty()){
        out.append("\r\n");
        for(const MStringView& rawBlock : unit.topLevelNativeBlocks){
            out.append(rawBlock.data(), rawBlock.size());
            out.append("\r\n");
        }
    }

    out.append("\r\n\r\n////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");

    if(!namespacePath.empty()){
        out.append("namespace ");
        out.append(namespacePath.data(), namespacePath.size());
        out.append("{\r\n\r\n\r\n");
    }

    for(const Stmt* c : unit.globalConstants){
        if(!c || !c->varType)
            continue;
        out.append("inline constexpr ");
        emitTypeCpp(out, *c->varType);
        out.push_back(' ');
        out.append(c->name.data(), c->name.size());
        out.append(" = ");
        if(c->expr)
            sourceEmitter.emitExpr(out, *c->expr);
        out.append(";\r\n");
    }
    if(!unit.globalConstants.empty())
        out.append("\r\n\r\n");

    for(const TypeAliasDecl* aliasDecl : unit.typeAliases){
        if(aliasDecl)
            emitTypeAliasDecl(out, *aliasDecl);
    }

    for(const EnumDecl* enumDecl : unit.enums){
        if(enumDecl)
            emitEnumDecl(out, *enumDecl);
    }

    for(const InterfaceDecl* ifaceDecl : unit.interfaces){
        if(ifaceDecl)
            emitInterfaceDecl(out, *ifaceDecl);
    }

    for(const ClassDecl* classDecl : unit.classes){
        if(classDecl)
            emitClassDecl(out, *classDecl);
    }

    for(const MethodDecl* fn : unit.freeFunctions){
        if(!fn)
            continue;
        const bool emitInlineBody = fn->isInline || !fn->templateParams.empty();
        emitTemplateHeaderCpp(out, fn->templateParams);
        if(fn->isInline)
            out.append("NWB_FORCE_INLINE ");
        else if(fn->isNoInline)
            out.append("NWB_NO_INLINE ");
        else if(emitInlineBody)
            out.append("inline ");

        emitMethodSignature(out, nullptr, *fn, false);
        if(emitInlineBody && fn->hasBody){
            out.append("{\r\n");
            sourceEmitter.emitMethodBodyBlock(out, nullptr, *fn, 1u);
            out.append("}\r\n\r\n");
        }
        else{
            out.append(";\r\n\r\n");
        }
    }

    if(!namespacePath.empty()){
        out.append("};\r\n\r\n\r\n");
    }

    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n");
    return out;
}

void HeaderEmitter::emitTypeCpp(MString& out, const TypeRef& type)const{
    if(type.refMode != ParamPassingMode::None){
        out.append("::NWB::NotNull<");
        if(type.refMode == ParamPassingMode::In)
            out.append("const ");
        out.append(type.name.data(), type.name.size());
        out.append("*>");
        return;
    }

    if(type.isAuto){
        out.append("auto");
        return;
    }

    if(type.isFuncType){
        out.append("::NWB::InplaceFunction<");
        __hidden_adhd_header_emitter::AppendUnsigned(out, type.inferredCaptureBytes);
        out.append("u, ");
        if(type.funcReturnType)
            emitTypeCpp(out, *type.funcReturnType);
        else
            out.append("void");
        out.push_back('(');
        for(usize i = 0u; i < type.funcParams.size(); ++i){
            if(i > 0u)
                out.append(", ");
            if(type.funcParams[i].type)
                emitParamTypeCpp(out, type.funcParams[i].mode, *type.funcParams[i].type);
        }
        out.append(")>");
        return;
    }

    const MStringView name = type.name;
    if(name == "String"){
        out.append("AString");
        return;
    }
    if(name == "StringView"){
        out.append("AStringView");
        return;
    }
    if(name == "CompactString"){
        out.append("ACompactString");
        return;
    }
    if(name == "f32x4"){ out.append("Float4"); return; }
    if(name == "i32x4"){ out.append("Int4"); return; }
    if(name == "u32x4"){ out.append("UInt4"); return; }
    if(name == "f32x3_i32"){ out.append("Float3Int"); return; }
    if(name == "f32x3_u32"){ out.append("Float3UInt"); return; }
    if(name == "f32x3x4"){ out.append("Float34"); return; }
    if(name == "f32x4x4"){ out.append("Float44"); return; }
    if(name == "f64x2"){ out.append("Double2"); return; }
    if(name == "f64x3"){ out.append("Double3"); return; }
    if(name == "f64x4"){ out.append("Double4"); return; }
    if(name == "i64x4"){ out.append("Int64x4"); return; }
    if(name == "u64x4"){ out.append("UInt64x4"); return; }
    if(name == "f64x3_i64"){ out.append("Double3Int"); return; }
    if(name == "f64x3_u64"){ out.append("Double3UInt"); return; }
    if(name == "f64x3x3"){ out.append("Double33"); return; }
    if(name == "f64x3x4"){ out.append("Double34"); return; }
    if(name == "f64x4x4"){ out.append("Double44"); return; }

    if(m_symbols.findEnum(name)){
        out.append(name.data(), name.size());
        out.append("::Enum");
        return;
    }

    out.append(name.data(), name.size());
    if(!type.templateArgs.empty()){
        out.push_back('<');
        for(usize i = 0u; i < type.templateArgs.size(); ++i){
            if(i > 0u)
                out.append(", ");
            const TypeArg& arg = type.templateArgs[i];
            if(arg.isConstant || !arg.constantText.empty())
                out.append(arg.constantText.data(), arg.constantText.size());
            else if(arg.type)
                emitTypeCpp(out, *arg.type);
        }
        out.push_back('>');
    }
}

void HeaderEmitter::emitParamTypeCpp(MString& out, const ParamPassingMode::Enum mode, const TypeRef& type)const{
    if(mode == ParamPassingMode::Mut){
        emitTypeCpp(out, type);
        out.push_back('&');
        return;
    }

    if(isSmallValueType(type.name, m_symbols) && type.templateArgs.empty() && !type.isFuncType){
        out.append("const ");
        emitTypeCpp(out, type);
        return;
    }

    out.append("const ");
    emitTypeCpp(out, type);
    out.push_back('&');
}

void HeaderEmitter::emitTemplateHeaderCpp(MString& out, const MVector<TemplateParamDecl>& params)const{
    if(params.empty())
        return;

    out.append("template<");
    for(usize i = 0u; i < params.size(); ++i){
        if(i > 0u)
            out.append(", ");
        const TemplateParamDecl& p = params[i];
        if(p.isConstValue && p.constValueType){
            emitTypeCpp(out, *p.constValueType);
            out.push_back(' ');
            out.append(p.name.data(), p.name.size());
        }
        else{
            out.append("typename ");
            out.append(p.name.data(), p.name.size());
        }
    }
    out.append(">\r\n");
}

void HeaderEmitter::emitEnumDecl(MString& out, const EnumDecl& decl)const{
    SourceEmitter sourceEmitter(m_arena, m_symbols, *this);

    out.append("namespace ");
    out.append(decl.name.data(), decl.name.size());
    out.append("{\r\n");
    out.append("    enum Enum : ");
    out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
    out.append("{\r\n");

    usize flagBitIndex = 0u;
    bool hasFlags = false;
    for(const EnumEntry& entry : decl.entries){
        out.append("        ");
        out.append(entry.name.data(), entry.name.size());
        if(entry.valueExpr){
            out.append(" = ");
            sourceEmitter.emitExpr(out, *entry.valueExpr);
        }
        else if(entry.isFlag){
            hasFlags = true;
            out.append(" = (static_cast<");
            out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
            out.append(">(1) << ");
            __hidden_adhd_header_emitter::AppendUnsigned(out, flagBitIndex++);
            out.push_back(')');
        }
        out.append(",\r\n");
        if(entry.isFlag)
            hasFlags = true;
    }

    out.append("    };\r\n");
    if(hasFlags){
        out.append("    using Flag = Enum;\r\n");
        out.append("    [[nodiscard]] constexpr Enum operator|(const Enum a, const Enum b)noexcept{ return static_cast<Enum>(static_cast<");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(a) | static_cast<");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(b)); }\r\n");
        out.append("    [[nodiscard]] constexpr Enum operator&(const Enum a, const Enum b)noexcept{ return static_cast<Enum>(static_cast<");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(a) & static_cast<");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(b)); }\r\n");
    }
    out.append("};\r\n\r\n");
}

void HeaderEmitter::emitTypeAliasDecl(MString& out, const TypeAliasDecl& decl)const{
    emitTemplateHeaderCpp(out, decl.templateParams);
    out.append("using ");
    out.append(decl.name.data(), decl.name.size());
    out.append(" = ");
    if(decl.aliasedType)
        emitTypeCpp(out, *decl.aliasedType);
    out.append(";\r\n\r\n");
}

void HeaderEmitter::emitInterfaceDecl(MString& out, const InterfaceDecl& decl)const{
    emitTemplateHeaderCpp(out, decl.templateParams);
    out.append("class ");
    out.append(decl.name.data(), decl.name.size());
    if(!decl.baseInterfaces.empty()){
        out.append(" : ");
        for(usize i = 0u; i < decl.baseInterfaces.size(); ++i){
            if(i > 0u)
                out.append(", ");
            out.append("public ");
            out.append(decl.baseInterfaces[i].data(), decl.baseInterfaces[i].size());
        }
    }
    out.append("{\r\npublic:\r\n");
    out.append("    virtual ~");
    out.append(decl.name.data(), decl.name.size());
    out.append("() = default;\r\n\r\n");

    for(const MethodDecl* method : decl.methods){
        if(!method)
            continue;
        out.append("    virtual ");
        emitMethodSignature(out, nullptr, *method, true);
        out.append(" = 0;\r\n");
    }

    out.append("};\r\n\r\n");
}

void HeaderEmitter::emitClassDecl(MString& out, const ClassDecl& decl)const{
    SourceEmitter sourceEmitter(m_arena, m_symbols, *this);
    ReflectionEmitter reflectionEmitter(m_arena, m_symbols, *this);

    if(decl.isSerialize)
        out.append("#pragma pack(push, 1)\r\n");

    emitTemplateHeaderCpp(out, decl.templateParams);
    out.append("class ");
    out.append(decl.name.data(), decl.name.size());
    out.append(" final");

    if(!decl.implementedInterfaces.empty()){
        out.append(" : ");
        for(usize i = 0u; i < decl.implementedInterfaces.size(); ++i){
            if(i > 0u)
                out.append(", ");
            out.append("public ");
            out.append(decl.implementedInterfaces[i].data(), decl.implementedInterfaces[i].size());
        }
    }
    out.append("{\r\n");

    if(decl.needsClassArena){
        out.append("private:\r\n");
        out.append("    inline static ::NWB::Core::Alloc::GlobalArena s_ClassArena{65536};\r\n\r\n");
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

        emitTemplateHeaderCpp(out, method->templateParams);
        const bool emitInlineBody = method->isInline || !decl.templateParams.empty() || !method->templateParams.empty();
        if(method->isInline)
            out.append("NWB_FORCE_INLINE ");
        else if(method->isNoInline)
            out.append("NWB_NO_INLINE ");

        emitMethodSignature(out, &decl, *method, true);
        if(emitInlineBody && method->hasBody){
            out.append("{\r\n");
            sourceEmitter.emitMethodBodyBlock(out, &decl, *method, 2u);
            out.append("    }\r\n");
        }
        else{
            out.append(";\r\n");
        }
    }

    if(!decl.orderedFields.empty())
        out.append("\r\n");

    for(const FieldDecl* field : decl.orderedFields){
        if(!field || !field->type)
            continue;
        if(field->access == AccessSpecifier::Public)
            out.append("public:\r\n    ");
        else if(field->access == AccessSpecifier::Protected)
            out.append("protected:\r\n    ");
        else
            out.append("private:\r\n    ");

        emitTypeCpp(out, *field->type);
        out.push_back(' ');
        out.append(field->name.data(), field->name.size());

        if(field->defaultExpr){
            out.append(" = ");
            sourceEmitter.emitExpr(out, *field->defaultExpr);
        }
        else if(field->hasCtorInit){
            out.push_back('{');
            for(usize i = 0u; i < field->ctorArgs.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                if(field->ctorArgs[i])
                    sourceEmitter.emitExpr(out, *field->ctorArgs[i]);
            }
            out.push_back('}');
        }
        else if(field->usesClassArena){
            out.append("{s_ClassArena}");
        }

        out.append(";\r\n");
    }

    if(decl.isSerialize || decl.isReflect || !decl.assetTypeName.empty()){
        out.append("\r\n");
        reflectionEmitter.emitClassReflectionDeclarations(out, decl);
    }

    out.append("};\r\n");

    if(decl.isSerialize)
        out.append("#pragma pack(pop)\r\n");

    out.append("\r\n");
}

void HeaderEmitter::emitMethodSignature(
    MString& out,
    const ClassDecl* ownerClass,
    const MethodDecl& method,
    const bool isDeclarationInClass
)const{
    if(!method.isConstructor && !method.isDestructor){
        if(method.returnType)
            emitTypeCpp(out, *method.returnType);
        else
            out.append("void");
        out.push_back(' ');
    }

    if(!isDeclarationInClass && ownerClass){
        out.append(ownerClass->name.data(), ownerClass->name.size());
        out.append("::");
    }

    if(method.isDestructor)
        out.push_back('~');
    if(method.isOperator)
        out.append("operator");
    out.append(method.name.data(), method.name.size());

    out.push_back('(');
    for(usize i = 0u; i < method.params.size(); ++i){
        if(i > 0u)
            out.append(", ");
        const ParamDecl& p = method.params[i];
        if(p.type)
            emitParamTypeCpp(out, p.mode, *p.type);
        out.push_back(' ');
        out.append(p.name.data(), p.name.size());
    }
    out.push_back(')');

    if(method.isConst)
        out.append("const");
    if(isDeclarationInClass && method.isOverride)
        out.append(" override");
}

bool HeaderEmitter::isSmallValueType(const MStringView typeName, const SymbolTable& symbols)noexcept{
    if(typeName == "bool"
        || typeName == "i8" || typeName == "u8"
        || typeName == "i16" || typeName == "u16"
        || typeName == "i32" || typeName == "u32"
        || typeName == "i64" || typeName == "u64"
        || typeName == "isize" || typeName == "usize"
        || typeName == "f32" || typeName == "f64"
        || typeName == "f32x4" || typeName == "i32x4" || typeName == "u32x4"
        || typeName == "f32x3_i32" || typeName == "f32x3_u32"
        || typeName == "f64x2"
        || typeName == "Name" || typeName == "StringView")
    {
        return true;
    }
    if(symbols.findEnum(typeName))
        return true;
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

