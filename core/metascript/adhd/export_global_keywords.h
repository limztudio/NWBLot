// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_ADHD_EXPORT_PARALLEL_CORE
#define NWB_ADHD_EXPORT_PARALLEL_CORE(adhdName, enumName, cppCostEnum, cppTargetEnum)
#define NWB_ADHD_EXPORT_PARALLEL_CORE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_PARALLEL_PRIORITY
#define NWB_ADHD_EXPORT_PARALLEL_PRIORITY(adhdName, enumName, cppPriorityEnum)
#define NWB_ADHD_EXPORT_PARALLEL_PRIORITY_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SCHEDULING_ATTRIBUTE
#define NWB_ADHD_EXPORT_SCHEDULING_ATTRIBUTE(attrName, seenVar, outVar, parseFn, duplicateErrorMessage, argErrorMessage, invalidValueErrorMessage)
#define NWB_ADHD_EXPORT_SCHEDULING_ATTRIBUTE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND
#define NWB_ADHD_EXPORT_DIAGNOSTIC_KIND(adhdKeyword, enumName, defaultMacroPrefix, essentialMacroPrefix, defaultMsgMacroPrefix, essentialMsgMacroPrefix)
#define NWB_ADHD_EXPORT_DIAGNOSTIC_KIND_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_ACCESS_SPECIFIER
#define NWB_ADHD_EXPORT_ACCESS_SPECIFIER(adhdKeyword, enumName, cppLabelPrefix, maybeUnusedOnField)
#define NWB_ADHD_EXPORT_ACCESS_SPECIFIER_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_PARAM_PASSING_MODE
#define NWB_ADHD_EXPORT_PARAM_PASSING_MODE(adhdKeyword, enumName)
#define NWB_ADHD_EXPORT_PARAM_PASSING_MODE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_BOOL_LITERAL
#define NWB_ADHD_EXPORT_BOOL_LITERAL(adhdKeyword, intValue)
#define NWB_ADHD_EXPORT_BOOL_LITERAL_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD
#define NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD(adhdKeyword, isFlag)
#define NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_UNATTRIBUTED_STMT_KEYWORD
#define NWB_ADHD_EXPORT_UNATTRIBUTED_STMT_KEYWORD(adhdKeyword, parserMethod, rejectAttrMessage)
#define NWB_ADHD_EXPORT_UNATTRIBUTED_STMT_KEYWORD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_FOR_STMT_KEYWORD
#define NWB_ADHD_EXPORT_FOR_STMT_KEYWORD(adhdKeyword, isParallel)
#define NWB_ADHD_EXPORT_FOR_STMT_KEYWORD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SIMPLE_STMT_KEYWORD
#define NWB_ADHD_EXPORT_SIMPLE_STMT_KEYWORD(adhdKeyword, stmtKind, rejectAttrMessage, missingSemiMessage)
#define NWB_ADHD_EXPORT_SIMPLE_STMT_KEYWORD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_METHOD_QUALIFIER
#define NWB_ADHD_EXPORT_METHOD_QUALIFIER(adhdKeyword, declField, duplicateErrorMessage)
#define NWB_ADHD_EXPORT_METHOD_QUALIFIER_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_CAST_KEYWORD
#define NWB_ADHD_EXPORT_CAST_KEYWORD(adhdKeyword, exprKind)
#define NWB_ADHD_EXPORT_CAST_KEYWORD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE
#define NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE(attrName, declField)
#define NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_PROHIBITED_CALLABLE_ATTRIBUTE
#define NWB_ADHD_EXPORT_PROHIBITED_CALLABLE_ATTRIBUTE(attrName, errorMessage)
#define NWB_ADHD_EXPORT_PROHIBITED_CALLABLE_ATTRIBUTE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_CLASS_FLAG_ATTRIBUTE
#define NWB_ADHD_EXPORT_CLASS_FLAG_ATTRIBUTE(attrName, declField)
#define NWB_ADHD_EXPORT_CLASS_FLAG_ATTRIBUTE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_CLASS_STRING_ATTRIBUTE
#define NWB_ADHD_EXPORT_CLASS_STRING_ATTRIBUTE(attrName, declField, impliesReflect, argErrorMessage)
#define NWB_ADHD_EXPORT_CLASS_STRING_ATTRIBUTE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_FIELD_ATTRIBUTE
#define NWB_ADHD_EXPORT_FIELD_ATTRIBUTE(attrName, declField, impliesClassReflect)
#define NWB_ADHD_EXPORT_FIELD_ATTRIBUTE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_INTERFACE_FLAG_ATTRIBUTE
#define NWB_ADHD_EXPORT_INTERFACE_FLAG_ATTRIBUTE(attrName)
#define NWB_ADHD_EXPORT_INTERFACE_FLAG_ATTRIBUTE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_INTERFACE_STRING_ATTRIBUTE
#define NWB_ADHD_EXPORT_INTERFACE_STRING_ATTRIBUTE(attrName, declField)
#define NWB_ADHD_EXPORT_INTERFACE_STRING_ATTRIBUTE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_ENUM_FLAG_ATTRIBUTE
#define NWB_ADHD_EXPORT_ENUM_FLAG_ATTRIBUTE(attrName)
#define NWB_ADHD_EXPORT_ENUM_FLAG_ATTRIBUTE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_TOP_LEVEL_NATIVE_ATTRIBUTE
#define NWB_ADHD_EXPORT_TOP_LEVEL_NATIVE_ATTRIBUTE(attrName, isSource)
#define NWB_ADHD_EXPORT_TOP_LEVEL_NATIVE_ATTRIBUTE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_DIAGNOSTIC_ATTRIBUTE
#define NWB_ADHD_EXPORT_DIAGNOSTIC_ATTRIBUTE(attrName, stmtField)
#define NWB_ADHD_EXPORT_DIAGNOSTIC_ATTRIBUTE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD
#define NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD(keyword, errorMessage)
#define NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD
#define NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD(keyword, classErrorMessage, interfaceErrorMessage)
#define NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD_AUTO_UNDEF
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


NWB_ADHD_EXPORT_DIAGNOSTIC_KIND("assert",  Assert,  "FOUNDATION_ASSERT(",         "FOUNDATION_FATAL_ASSERT(",             "FOUNDATION_ASSERT_MSG(",         "FOUNDATION_FATAL_ASSERT_MSG(")

NWB_ADHD_EXPORT_ACCESS_SPECIFIER("public",    Public,    "public:\r\n    ",    false)
NWB_ADHD_EXPORT_ACCESS_SPECIFIER("private",   Private,   "private:\r\n    ",   true)
NWB_ADHD_EXPORT_ACCESS_SPECIFIER("protected", Protected, "protected:\r\n    ", false)

NWB_ADHD_EXPORT_PARAM_PASSING_MODE("in",  In)
NWB_ADHD_EXPORT_PARAM_PASSING_MODE("mut", Mut)

NWB_ADHD_EXPORT_BOOL_LITERAL("true",  1)
NWB_ADHD_EXPORT_BOOL_LITERAL("false", 0)

NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD("enum", false)
NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD("flag", true)

NWB_ADHD_EXPORT_UNATTRIBUTED_STMT_KEYWORD("if",     parseIfStatement,      "attributes are not permitted on 'if' statements")
NWB_ADHD_EXPORT_UNATTRIBUTED_STMT_KEYWORD("switch", parseSwitchStatement,  "attributes are not permitted on 'switch' statements")
NWB_ADHD_EXPORT_UNATTRIBUTED_STMT_KEYWORD("while",  parseWhileStatement,   "attributes are not permitted on 'while' statements")
NWB_ADHD_EXPORT_UNATTRIBUTED_STMT_KEYWORD("do",     parseDoWhileStatement, "attributes are not permitted on 'do-while' statements")
NWB_ADHD_EXPORT_UNATTRIBUTED_STMT_KEYWORD("return", parseReturnStatement,  "attributes are not permitted on 'return' statements")
NWB_ADHD_EXPORT_UNATTRIBUTED_STMT_KEYWORD("native", parseNativeStatement,  "attributes are not permitted on 'native' blocks")
NWB_ADHD_EXPORT_UNATTRIBUTED_STMT_KEYWORD("const",  parseConstDeclaration, "attributes are not permitted on local 'const' declarations")

NWB_ADHD_EXPORT_FOR_STMT_KEYWORD("for",          false)
NWB_ADHD_EXPORT_FOR_STMT_KEYWORD("parallel_for", true)

NWB_ADHD_EXPORT_SIMPLE_STMT_KEYWORD("break",    StmtKind::Break,    "attributes are not permitted on 'break' statements",    "expected ';' after 'break'")
NWB_ADHD_EXPORT_SIMPLE_STMT_KEYWORD("continue", StmtKind::Continue, "attributes are not permitted on 'continue' statements", "expected ';' after 'continue'")

NWB_ADHD_EXPORT_METHOD_QUALIFIER("const",    isConst,    "duplicate 'const' qualifier on method")
NWB_ADHD_EXPORT_METHOD_QUALIFIER("override", isOverride, "duplicate 'override' qualifier on method")

NWB_ADHD_EXPORT_CAST_KEYWORD("cast",     ExprKind::Cast)
NWB_ADHD_EXPORT_CAST_KEYWORD("bit_cast", ExprKind::BitCast)

NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE("reflect",  isReflect)

NWB_ADHD_EXPORT_PROHIBITED_CALLABLE_ATTRIBUTE("inline",   "'inline' and 'noinline' attributes are prohibited in adhd; inlining is decided automatically by the adhd compiler")
NWB_ADHD_EXPORT_PROHIBITED_CALLABLE_ATTRIBUTE("noinline", "'inline' and 'noinline' attributes are prohibited in adhd; inlining is decided automatically by the adhd compiler")

NWB_ADHD_EXPORT_CLASS_FLAG_ATTRIBUTE("serialize", isSerialize)
NWB_ADHD_EXPORT_CLASS_FLAG_ATTRIBUTE("reflect",   isReflect)
NWB_ADHD_EXPORT_CLASS_FLAG_ATTRIBUTE("arena",     isArenaOwner)

NWB_ADHD_EXPORT_FIELD_ATTRIBUTE("transient", isTransient, false)
NWB_ADHD_EXPORT_FIELD_ATTRIBUTE("reflect",   isReflect,   true)

NWB_ADHD_EXPORT_INTERFACE_FLAG_ATTRIBUTE("reflect")

NWB_ADHD_EXPORT_ENUM_FLAG_ATTRIBUTE("reflect")

NWB_ADHD_EXPORT_TOP_LEVEL_NATIVE_ATTRIBUTE("header", false)
NWB_ADHD_EXPORT_TOP_LEVEL_NATIVE_ATTRIBUTE("source", true)

NWB_ADHD_EXPORT_DIAGNOSTIC_ATTRIBUTE("essential", isEssential)

NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD("typename", "do not write 'typename' or 'class' inside '<...>' template parameters in adhd")
NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD("class",    "do not write 'typename' or 'class' inside '<...>' template parameters in adhd")

NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD("static",   "'static' members are prohibited in adhd",                                                                     "'static' methods and members are prohibited inside 'interface' in adhd")
NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD("virtual",  "'virtual' is prohibited in adhd; declare contracts in an 'interface' and mark implementations with 'override'", "'virtual' keyword is prohibited in adhd; interface methods are implicitly abstract contracts")
NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD("inline",   "'inline' and 'noinline' are prohibited in adhd; inlining is decided automatically by the adhd compiler",      "'inline' and 'noinline' are prohibited in adhd; inlining is decided automatically by the adhd compiler")
NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD("noinline", "'inline' and 'noinline' are prohibited in adhd; inlining is decided automatically by the adhd compiler",      "'inline' and 'noinline' are prohibited in adhd; inlining is decided automatically by the adhd compiler")

NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD("namespace", "explicit 'namespace' declarations are prohibited in adhd; namespaces are derived from file paths")
NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD("struct",    "'struct' is prohibited in adhd; use 'class' (or '[serialize] class' for 1-byte packed binary layout)")
NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD("union",     "'union' is prohibited in adhd")
NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD("static",    "'static' is prohibited in adhd")
NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD("template",  "C++ 'template' keyword is prohibited in adhd; write '<T>' directly on declarations")
NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD("inline",    "'inline' is prohibited in adhd; inlining is decided automatically by the adhd compiler")
NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD("noinline",  "'noinline' is prohibited in adhd; inlining is decided automatically by the adhd compiler")

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
NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD("flag",      true)
NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD("union",     true)
NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD("type",      true)
NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD("namespace", false)

NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD("const",    "C++-style 'const' type qualifier is prohibited in adhd; use 'in' for read-only parameters")
NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD("bool",     "'bool' is prohibited in adhd; use 'b8'")
NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD("int",      "'int' is prohibited in adhd; use 'i32'")
NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD("float",    "'float' is prohibited in adhd; use 'f32'")
NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD("double",   "'double' is prohibited in adhd; use 'f64'")
NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD("char",     "'char' is prohibited in adhd; use 'u8' or 'i8'")
NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD("short",    "'short' is prohibited in adhd; use 'i16'")
NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD("long",     "'long' is prohibited in adhd; use 'i64'")
NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD("unsigned", "'unsigned' is prohibited in adhd; use 'u32' or 'u64'")
NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD("signed",   "'signed' is prohibited in adhd; use 'i32' or 'i64'")
NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD("NotNull",  "'NotNull' is prohibited in adhd; use 'in T' or 'mut T'")


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

#ifdef NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD
#endif

#ifdef NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD
#endif

#ifdef NWB_ADHD_EXPORT_TOP_LEVEL_NATIVE_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_TOP_LEVEL_NATIVE_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_TOP_LEVEL_NATIVE_ATTRIBUTE
#endif

#ifdef NWB_ADHD_EXPORT_DIAGNOSTIC_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_ATTRIBUTE
#endif

#ifdef NWB_ADHD_EXPORT_ENUM_FLAG_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ENUM_FLAG_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ENUM_FLAG_ATTRIBUTE
#endif

#ifdef NWB_ADHD_EXPORT_INTERFACE_STRING_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_INTERFACE_STRING_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_INTERFACE_STRING_ATTRIBUTE
#endif

#ifdef NWB_ADHD_EXPORT_INTERFACE_FLAG_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_INTERFACE_FLAG_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_INTERFACE_FLAG_ATTRIBUTE
#endif

#ifdef NWB_ADHD_EXPORT_FIELD_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FIELD_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FIELD_ATTRIBUTE
#endif

#ifdef NWB_ADHD_EXPORT_CLASS_STRING_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_CLASS_STRING_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_CLASS_STRING_ATTRIBUTE
#endif

#ifdef NWB_ADHD_EXPORT_CLASS_FLAG_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_CLASS_FLAG_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_CLASS_FLAG_ATTRIBUTE
#endif

#ifdef NWB_ADHD_EXPORT_PROHIBITED_CALLABLE_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_CALLABLE_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_CALLABLE_ATTRIBUTE
#endif

#ifdef NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE
#endif

#ifdef NWB_ADHD_EXPORT_CAST_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_CAST_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_CAST_KEYWORD
#endif

#ifdef NWB_ADHD_EXPORT_METHOD_QUALIFIER_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_METHOD_QUALIFIER_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_METHOD_QUALIFIER
#endif

#ifdef NWB_ADHD_EXPORT_SIMPLE_STMT_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMPLE_STMT_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMPLE_STMT_KEYWORD
#endif

#ifdef NWB_ADHD_EXPORT_FOR_STMT_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FOR_STMT_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FOR_STMT_KEYWORD
#endif

#ifdef NWB_ADHD_EXPORT_UNATTRIBUTED_STMT_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_UNATTRIBUTED_STMT_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_UNATTRIBUTED_STMT_KEYWORD
#endif

#ifdef NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD
#endif

#ifdef NWB_ADHD_EXPORT_BOOL_LITERAL_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BOOL_LITERAL_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BOOL_LITERAL
#endif

#ifdef NWB_ADHD_EXPORT_PARAM_PASSING_MODE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PARAM_PASSING_MODE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PARAM_PASSING_MODE
#endif

#ifdef NWB_ADHD_EXPORT_ACCESS_SPECIFIER_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ACCESS_SPECIFIER_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ACCESS_SPECIFIER
#endif

#ifdef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND
#endif

#ifdef NWB_ADHD_EXPORT_SCHEDULING_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SCHEDULING_ATTRIBUTE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SCHEDULING_ATTRIBUTE
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

