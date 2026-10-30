// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../sema/type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr MStringView s_AdhdGeneratedFileBanner = "// Compiled by ADHD Compiler " NWB_ADHD_COMPILER_VERSION "\r\n";

inline void AppendCamelToSnakeCase(MString& out, const MStringView text){
    for(usize i = 0u; i < text.size(); ++i){
        const char ch = text[i];
        if(IsAsciiUpper(ch)){
            if(i > 0u){
                const char prev = text[i - 1u];
                const b8 prevLowerOrDigit = IsAsciiLower(prev) || IsAsciiDigit(prev);
                const b8 nextLower = (i + 1u < text.size()) && IsAsciiLower(text[i + 1u]);
                if(prevLowerOrDigit || nextLower)
                    out.push_back('_');
            }
            out.push_back(static_cast<char>(ch + ('a' - 'A')));
        }
        else{
            out.push_back(ch);
        }
    }
}

inline void AppendAdhdTextCrlf(MString& out, const MStringView text){
    for(usize i = 0u; i < text.size(); ++i){
        const char ch = text[i];
        if(ch == '\r')
            continue;
        if(ch == '\n')
            out.append("\r\n");
        else
            out.push_back(ch);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CppTypeEmitter final{
public:
    [[nodiscard]] static MStringView MapBuiltinCppType(MStringView typeName)noexcept;


public:
    explicit CppTypeEmitter(const SymbolTable& symbols)noexcept
        : m_symbols(symbols)
    {}


public:
    void emitTypeCpp(MString& out, const TypeRef& type)const;
    void emitTypeArgListCpp(MString& out, Span<const TypeArg> args)const;
    void emitParamTypeCpp(MString& out, ParamPassingMode::Enum mode, const TypeRef& type, b8 topLevelConst = true)const;
    void emitParamListCpp(MString& out, Span<const ParamDecl> params)const;
    void emitTemplateHeaderCpp(MString& out, Span<const TemplateParamDecl> params)const;
    void emitMethodSignature(MString& out, const ClassDecl* ownerClass, const MethodDecl& method, b8 isDeclarationInClass)const;


private:
    const SymbolTable& m_symbols;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

