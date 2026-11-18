// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_EXPORT_PARALLEL_CORE("Any",        Any,        "Any",   "Worker")
NWB_ADHD_EXPORT_PARALLEL_CORE("Heavy",      Heavy,      "Heavy", "Worker")
NWB_ADHD_EXPORT_PARALLEL_CORE("Light",      Light,      "Light", "Worker")
NWB_ADHD_EXPORT_PARALLEL_CORE("MainThread", MainThread, "Any",   "MainThread")

NWB_ADHD_EXPORT_PARALLEL_PRIORITY("Normal",     Normal,     "Normal")
NWB_ADHD_EXPORT_PARALLEL_PRIORITY("Critical",   Critical,   "Critical")
NWB_ADHD_EXPORT_PARALLEL_PRIORITY("Background", Background, "Background")

NWB_ADHD_EXPORT_SCHEDULING_ATTRIBUTE("core",     seenCore,     outCore,     TryParseParallelCoreKind,     "duplicate 'core' attribute on parallel statement",     "'core' attribute requires a single identifier argument (Heavy, Light, MainThread, or Any)", "invalid 'core' attribute value; expected Heavy, Light, MainThread, or Any")
NWB_ADHD_EXPORT_SCHEDULING_ATTRIBUTE("priority", seenPriority, outPriority, TryParseParallelPriorityKind, "duplicate 'priority' attribute on parallel statement", "'priority' attribute requires a single identifier argument (Critical, Normal, or Background)",  "invalid 'priority' attribute value; expected Critical, Normal, or Background")

NWB_ADHD_EXPORT_DIAGNOSTIC_KIND("info",    Info,    "NWB_LOGGER_INFO(",    "NWB_LOGGER_ESSENTIAL_INFO(",    "NWB_LOGGER_INFO(",        "NWB_LOGGER_ESSENTIAL_INFO(")
NWB_ADHD_EXPORT_DIAGNOSTIC_KIND("warning", Warning, "NWB_LOGGER_WARNING(", "NWB_LOGGER_CRITICAL_WARNING(",  "NWB_LOGGER_WARNING(",     "NWB_LOGGER_CRITICAL_WARNING(")
NWB_ADHD_EXPORT_DIAGNOSTIC_KIND("error",   Error,   "NWB_LOGGER_ERROR(",   "NWB_LOGGER_FATAL(",             "NWB_LOGGER_ERROR(",       "NWB_LOGGER_FATAL(")

NWB_ADHD_EXPORT_CLASS_STRING_ATTRIBUTE("asset",     assetTypeName,     true,  "'[asset(\"...\")]' requires a single string literal argument")
NWB_ADHD_EXPORT_CLASS_STRING_ATTRIBUTE("system",    systemTypeName,    false, "'[system(\"...\")]' requires a single string literal argument")
NWB_ADHD_EXPORT_CLASS_STRING_ATTRIBUTE("subsystem", subsystemTypeName, false, "'[subsystem(\"...\")]' requires a single string literal argument")
NWB_ADHD_EXPORT_CLASS_STRING_ATTRIBUTE("component", componentTypeName, true,  "'[component(\"...\")]' requires a single string literal argument")

NWB_ADHD_EXPORT_INTERFACE_STRING_ATTRIBUTE("asset", assetTypeName)

NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(AssetWriter, "asset_writer")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(CookDocument, "cook_document")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(VolumePrepare, "volume_prepare")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(CookSetup, "cook_setup")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(GatherMerge, "gather_merge")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(CookRegistrar, "cook_registrar")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(CookSidecar, "cook_sidecar")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(CookEntry, "cook_entry")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(CookDocumentOnly, "document_only")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(CookDocumentAndValue, "document_and_value")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(CookQuiet, "quiet")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(CookLogFailure, "log_failure")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(RegistrationUsing, "using")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(RegisterAsset, "register_asset")
NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(Registration, "registration")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

