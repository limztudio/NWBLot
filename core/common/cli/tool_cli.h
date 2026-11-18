// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/common/global.h>
#include <global/containers.h>
#include <global/expected.h>
#include <global/inplace_function.h>
#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ConsoleArguments{
    i32 count;
    char** values;
};

[[nodiscard]] Expected<i32> TryConsoleCommand(const ConsoleArguments& arguments, const InplaceFunction<64u, i32(const ConsoleArguments&)>& command)noexcept;
void ConsoleWrite(AStringView text);
b8 ConsoleReadLine(AString<::ArenaBase>& line);

class ToolCliOption{
public:
    explicit ToolCliOption(CLI::Option& option)noexcept : m_option(option){}
    void required(){ m_option.required(); }
    void expected(i32 count){ m_option.expected(count); }
    void expectedRange(i32 minimum, i32 maximum){ m_option.expected(minimum, maximum); }
    [[nodiscard]] usize count()const{ return m_option.count(); }


private:
    CLI::Option& m_option;
};

class ToolCliParseResult{
public:
    [[nodiscard]] b8 ok()const noexcept{ return m_ok; }
    [[nodiscard]] b8 helpRequested()const noexcept{ return m_help; }
    [[nodiscard]] AStringView message()const noexcept{ return m_message; }
    [[nodiscard]] i32 exitToConsole()const;
    [[nodiscard]] i32 exitLogged()const;


private:
    friend class ToolCli;
    b8 m_ok = true;
    b8 m_help = false;
    i32 m_exit = 0;
    AInteropString m_message;
    AInteropString m_stdout;
    AInteropString m_stderr;
};

class ToolCli final : NoCopy{
public:
    explicit ToolCli(AStringView description) : m_app(AInteropString(description)){}
    void setHelpFlag(AStringView names, AStringView description){ m_app.set_help_flag(AInteropString(names), AInteropString(description)); }
    ToolCliOption addText(AStringView names, AString<::ArenaBase>& output, AStringView description);
    ToolCliOption addTexts(AStringView names, Vector<AString<::ArenaBase>, ::ArenaBase>& output, AStringView description);
    ToolCliOption addFlag(AStringView names, b8& output, AStringView description);
    ToolCliOption addF32(AStringView names, f32& output, AStringView description);
    ToolCliOption addF64(AStringView names, f64& output, AStringView description);
    ToolCliOption addU32(AStringView names, u32& output, AStringView description);
    [[nodiscard]] ToolCliParseResult parse(const ConsoleArguments& arguments);
    [[nodiscard]] i32 validationExit(AStringView name, AStringView message)const;
    [[nodiscard]] usize count(AStringView name)const{ return m_app.count(AInteropString(name)); }
    void printHelp(b8 toError)const;


private:
    ToolCliParseResult resultFor(const CLI::ParseError& error)const;
    CLI::App m_app;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

