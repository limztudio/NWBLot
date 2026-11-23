// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "tool_cli.h"

#include <core/common/log.h>
#include <global/command_line.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Utilities{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ConsoleWrite(const AStringView text){
    FOUNDATION_COUT << text;
}

void ConsoleWriteError(const AStringView text){
    FOUNDATION_CERR << text;
}

b8 ConsoleReadLine(AString<::ArenaBase>& line){
    return ReadTextLine(FOUNDATION_CIN, line);
}


i32 ToolCliParseResult::exitToConsole()const{
    FOUNDATION_COUT << m_stdout;
    FOUNDATION_CERR << m_stderr;
    return m_exit;
}

i32 ToolCliParseResult::exitLogged()const{
    if(m_exit == 0)
        return exitToConsole();
    const auto report = m_stdout + m_stderr;
    NWB_LOGGER_ERROR(FOUNDATION_TEXT("{}"), ConvertString(report.empty() ? AStringView(m_message) : AStringView(report)));
    return m_exit;
}


ToolCliOption ToolCli::addText(const AStringView names, AString<::ArenaBase>& output, const AStringView description){
    return ToolCliOption(*m_app.add_option_function<AInteropString>(AInteropString(names), [&output](const AInteropString& value){
        output.assign(value.data(), value.size());
    }, AInteropString(description)));
}

ToolCliOption ToolCli::addTexts(const AStringView names, Vector<AString<::ArenaBase>, ::ArenaBase>& output, const AStringView description){
    return ToolCliOption(*m_app.add_option_function<InteropVector<AInteropString>>(AInteropString(names), [&output](const InteropVector<AInteropString>& values){
        output.clear();
        output.reserve(values.size());
        for(const auto& value : values)
            output.emplace_back(value.data(), value.size(), output.get_allocator().arena());
    }, AInteropString(description)));
}

ToolCliOption ToolCli::addFlag(const AStringView names, b8& output, const AStringView description){
    return ToolCliOption(*m_app.add_flag(AInteropString(names), output, AInteropString(description)));
}

ToolCliOption ToolCli::addF32(const AStringView names, f32& output, const AStringView description){
    return ToolCliOption(*m_app.add_option(AInteropString(names), output, AInteropString(description)));
}

ToolCliOption ToolCli::addF64(const AStringView names, f64& output, const AStringView description){
    return ToolCliOption(*m_app.add_option(AInteropString(names), output, AInteropString(description)));
}

ToolCliOption ToolCli::addU32(const AStringView names, u32& output, const AStringView description){
    return ToolCliOption(*m_app.add_option(AInteropString(names), output, AInteropString(description)));
}

ToolCliParseResult ToolCli::parse(const ConsoleArguments& arguments){
    try{
        CommandLineParseApp(m_app, arguments.count, arguments.values);
        return {};
    }
    catch(const CLI::ParseError& error){
        return resultFor(error);
    }
}

ToolCliParseResult ToolCli::resultFor(const CLI::ParseError& error)const{
    AInteropOutputStringStream output;
    AInteropOutputStringStream errors;
    ToolCliParseResult result;
    result.m_ok = false;
    result.m_help = error.get_name() == "CallForHelp";
    result.m_message = error.what();
    result.m_exit = m_app.exit(error, output, errors);
    result.m_stdout = Move(output).str();
    result.m_stderr = Move(errors).str();
    return result;
}

i32 ToolCli::validationExit(const AStringView name, const AStringView message)const{
    return resultFor(CLI::ValidationError(AInteropString(name), AInteropString(message))).exitLogged();
}

void ToolCli::printHelp(const b8 toError)const{
    (toError ? FOUNDATION_CERR : FOUNDATION_COUT) << m_app.help();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

