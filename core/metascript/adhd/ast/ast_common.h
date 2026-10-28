// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/metascript/attribute_parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_ADHD_BEGIN NWB_METASCRIPT_BEGIN namespace Adhd{
#define NWB_ADHD_END }; NWB_METASCRIPT_END
#define NWB_ADHD_COMPILER_VERSION "1.0.0"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SourceSpan{
    u32 line = 1;
    u32 column = 1;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AccessSpecifier{
    enum Enum : u8{
        None,
        Public,
        Private,
        Protected,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ParamPassingMode{
    enum Enum : u8{
        None,
        In,
        Mut,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ParallelCoreKind{
    enum Enum : u8{
        Any,
        Heavy,
        Light,
        MainThread,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ParallelPriorityKind{
    enum Enum : u8{
        Normal,
        Critical,
        Background,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticKind{
    enum Enum : u8{
        Assert,
        Info,
        Warning,
        Error,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] T* AllocateAstNode(MetaArena& arena){
    void* mem = arena.allocate(alignof(T), sizeof(T));
    return ::new(mem) T(arena);
}

inline void AppendAdhdUnsigned(MString& out, usize value){
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

[[nodiscard]] constexpr MStringView StripAdhdExtension(const MStringView path)noexcept{
    if(path.size() > 5u && path.substr(path.size() - 5u) == ".adhd")
        return path.substr(0u, path.size() - 5u);
    return path;
}

[[nodiscard]] constexpr bool IsCompoundOrSimpleAssignment(const TokenType::Enum op)noexcept{
    return op == TokenType::Equal
        || op == TokenType::PlusEqual
        || op == TokenType::MinusEqual
        || op == TokenType::StarEqual
        || op == TokenType::SlashEqual
        || op == TokenType::PercentEqual
        || op == TokenType::AmpersandEqual
        || op == TokenType::PipeEqual
        || op == TokenType::CaretEqual
        || op == TokenType::LessLessEqual
        || op == TokenType::GreaterGreaterEqual;
}

[[nodiscard]] constexpr MStringView ExtractAdhdFileStem(const MStringView path)noexcept{
    usize fileStart = 0u;
    for(usize i = 0u; i < path.size(); ++i){
        if(path[i] == '/' || path[i] == '\\')
            fileStart = i + 1u;
    }
    return StripAdhdExtension(path.substr(fileStart));
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

inline constexpr MStringView s_AdhdGeneratedFileBanner = "// Compiled by ADHD Compiler " NWB_ADHD_COMPILER_VERSION "\r\n";

inline void ReportAdhdDiagnostic(MetaArena& arena, MVector<ParseError>& errors, const SourceSpan span, const MStringView message){
    errors.push_back(ParseError{span.line, span.column, MString(message.data(), message.size(), arena)});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

