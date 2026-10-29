// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_ADHD_EXPORT_PARALLEL_CORE
#define NWB_ADHD_EXPORT_PARALLEL_CORE(adhdName, astEnum, cppCostEnum, cppTargetEnum)
#define NWB_ADHD_EXPORT_PARALLEL_CORE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_PARALLEL_PRIORITY
#define NWB_ADHD_EXPORT_PARALLEL_PRIORITY(adhdName, astEnum, cppPriorityEnum)
#define NWB_ADHD_EXPORT_PARALLEL_PRIORITY_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND
#define NWB_ADHD_EXPORT_DIAGNOSTIC_KIND(adhdKeyword, astEnum, defaultMacroPrefix, essentialMacroPrefix, defaultMsgMacroPrefix, essentialMsgMacroPrefix)
#define NWB_ADHD_EXPORT_DIAGNOSTIC_KIND_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_ACCESS_SPECIFIER
#define NWB_ADHD_EXPORT_ACCESS_SPECIFIER(adhdKeyword, astEnum)
#define NWB_ADHD_EXPORT_ACCESS_SPECIFIER_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE
#define NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE(attrName, declField)
#define NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD
#define NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD(keyword, errorMessage)
#define NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD
#define NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD(keyword, errorMessage)
#define NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD
#define NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD(keyword, errorMessage)
#define NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD
#define NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD(keyword, isClassNestedKeyword)
#define NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD
#define NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD(keyword, errorMessage)
#define NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD_AUTO_UNDEF
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_EXPORT_PARALLEL_CORE("Any",        ParallelCoreKind::Any,        "Any",   "Worker")
NWB_ADHD_EXPORT_PARALLEL_CORE("Heavy",      ParallelCoreKind::Heavy,      "Heavy", "Worker")
NWB_ADHD_EXPORT_PARALLEL_CORE("Light",      ParallelCoreKind::Light,      "Light", "Worker")
NWB_ADHD_EXPORT_PARALLEL_CORE("MainThread", ParallelCoreKind::MainThread, "Any",   "MainThread")

NWB_ADHD_EXPORT_PARALLEL_PRIORITY("Normal",     ParallelPriorityKind::Normal,     "Normal")
NWB_ADHD_EXPORT_PARALLEL_PRIORITY("Critical",   ParallelPriorityKind::Critical,   "Critical")
NWB_ADHD_EXPORT_PARALLEL_PRIORITY("Background", ParallelPriorityKind::Background, "Background")

NWB_ADHD_EXPORT_DIAGNOSTIC_KIND("assert",  DiagnosticKind::Assert,  "NWB_ASSERT(",         "NWB_FATAL_ASSERT(",             "NWB_ASSERT_MSG(",         "NWB_FATAL_ASSERT_MSG(")
NWB_ADHD_EXPORT_DIAGNOSTIC_KIND("info",    DiagnosticKind::Info,    "NWB_LOGGER_INFO(",    "NWB_LOGGER_ESSENTIAL_INFO(",    "NWB_LOGGER_INFO(",        "NWB_LOGGER_ESSENTIAL_INFO(")
NWB_ADHD_EXPORT_DIAGNOSTIC_KIND("warning", DiagnosticKind::Warning, "NWB_LOGGER_WARNING(", "NWB_LOGGER_CRITICAL_WARNING(",  "NWB_LOGGER_WARNING(",     "NWB_LOGGER_CRITICAL_WARNING(")
NWB_ADHD_EXPORT_DIAGNOSTIC_KIND("error",   DiagnosticKind::Error,   "NWB_LOGGER_ERROR(",   "NWB_LOGGER_FATAL(",             "NWB_LOGGER_ERROR(",       "NWB_LOGGER_FATAL(")

NWB_ADHD_EXPORT_ACCESS_SPECIFIER("public",    AccessSpecifier::Public)
NWB_ADHD_EXPORT_ACCESS_SPECIFIER("private",   AccessSpecifier::Private)
NWB_ADHD_EXPORT_ACCESS_SPECIFIER("protected", AccessSpecifier::Protected)

NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE("reflect",  isReflect)
NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE("inline",   isForceInline)
NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE("noinline", isNoInline)

NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD("namespace", "explicit 'namespace' declarations are prohibited in adhd; namespaces are derived from file paths")
NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD("struct",    "'struct' is prohibited in adhd; use 'class' (or '[serialize] class' for 1-byte packed binary layout)")
NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD("union",     "'union' is prohibited in adhd")
NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD("static",    "'static' is prohibited in adhd")
NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD("template",  "C++ 'template' keyword is prohibited in adhd; write '<T>' directly on declarations")

NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD("null",             "'null' and 'nullptr' are prohibited in adhd; use Optional<T>")
NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD("nullptr",          "'null' and 'nullptr' are prohibited in adhd; use Optional<T>")
NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD("new",              "'new' and 'delete' are prohibited in adhd; use arena-backed containers")
NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD("delete",           "'new' and 'delete' are prohibited in adhd; use arena-backed containers")
NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD("Move",             "'Move' is prohibited in adhd; parameters borrow with 'in'/'mut' and last-use moves are automatic")
NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD("dynamic_cast",     "C++ cast keywords are prohibited in adhd; use 'cast<T>(x)' or 'bit_cast<T>(x)'")
NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD("static_cast",      "C++ cast keywords are prohibited in adhd; use 'cast<T>(x)' or 'bit_cast<T>(x)'")
NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD("reinterpret_cast", "C++ cast keywords are prohibited in adhd; use 'cast<T>(x)' or 'bit_cast<T>(x)'")
NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD("const_cast",       "C++ cast keywords are prohibited in adhd; use 'cast<T>(x)' or 'bit_cast<T>(x)'")

NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD("try",    "C++ exceptions ('try'/'catch'/'throw') are prohibited in adhd; use Optional<T> or Expected<T, E>")
NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD("catch",  "C++ exceptions ('try'/'catch'/'throw') are prohibited in adhd; use Optional<T> or Expected<T, E>")
NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD("throw",  "C++ exceptions ('try'/'catch'/'throw') are prohibited in adhd; use Optional<T> or Expected<T, E>")
NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD("goto",   "'goto' is prohibited in adhd")
NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD("static", "'static' variables are prohibited in adhd")

NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD("class",     true)
NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD("struct",    true)
NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD("interface", true)
NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD("enum",      true)
NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD("union",     true)
NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD("type",      true)
NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD("namespace", false)

NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD("const", "C++-style 'const' type qualifier is prohibited in adhd; use 'in' for read-only parameters")
NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD("bool",  "'bool' is prohibited in adhd; use 'b8'")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD
#endif


#ifdef NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD
#endif

#ifdef NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD
#endif

#ifdef NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD
#endif

#ifdef NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD
#endif

#ifdef NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE
#endif

#ifdef NWB_ADHD_EXPORT_ACCESS_SPECIFIER_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ACCESS_SPECIFIER_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ACCESS_SPECIFIER
#endif

#ifdef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND
#endif

#ifdef NWB_ADHD_EXPORT_PARALLEL_PRIORITY_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PARALLEL_PRIORITY_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PARALLEL_PRIORITY
#endif

#ifdef NWB_ADHD_EXPORT_PARALLEL_CORE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PARALLEL_CORE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PARALLEL_CORE
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
