// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "client.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


extern IClient* g_Logger;

template<typename... ARGS>
constexpr void IgnoreMessage(ARGS&&...){}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_LOGGER_REGISTER(inst) ::NWB::Log::__hidden_logger::g_Logger = inst

#if NWB_OCCUR_INFO
#define NWB_LOGGER_INFO(...) { NWB_FATAL_ASSERT(::NWB::Log::__hidden_logger::g_Logger != nullptr); ::NWB::Log::__hidden_logger::g_Logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Log::Type::Info); }
#else
#define NWB_LOGGER_INFO(...) static_cast<void>(sizeof((::NWB::Log::__hidden_logger::IgnoreMessage(__VA_ARGS__), 0)))
#endif

#if NWB_OCCUR_ESSENTIAL_INFO
#define NWB_LOGGER_ESSENTIAL_INFO(...) { NWB_FATAL_ASSERT(::NWB::Log::__hidden_logger::g_Logger != nullptr); ::NWB::Log::__hidden_logger::g_Logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Log::Type::EssentialInfo); }
#else
#define NWB_LOGGER_ESSENTIAL_INFO(...) static_cast<void>(sizeof((::NWB::Log::__hidden_logger::IgnoreMessage(__VA_ARGS__), 0)))
#endif

#if NWB_OCCUR_WARNING
#define NWB_LOGGER_WARNING(...) { NWB_FATAL_ASSERT(::NWB::Log::__hidden_logger::g_Logger != nullptr); ::NWB::Log::__hidden_logger::g_Logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Log::Type::Warning); }
#else
#define NWB_LOGGER_WARNING(...) static_cast<void>(sizeof((::NWB::Log::__hidden_logger::IgnoreMessage(__VA_ARGS__), 0)))
#endif

#if NWB_OCCUR_CRITICAL_WARNING
#define NWB_LOGGER_CRITICAL_WARNING(...) { NWB_FATAL_ASSERT(::NWB::Log::__hidden_logger::g_Logger != nullptr); ::NWB::Log::__hidden_logger::g_Logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Log::Type::CriticalWarning); }
#else
#define NWB_LOGGER_CRITICAL_WARNING(...) static_cast<void>(sizeof((::NWB::Log::__hidden_logger::IgnoreMessage(__VA_ARGS__), 0)))
#endif

#if NWB_OCCUR_ERROR
#define NWB_LOGGER_ERROR(...) { NWB_FATAL_ASSERT(::NWB::Log::__hidden_logger::g_Logger != nullptr); ::NWB::Log::__hidden_logger::g_Logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Log::Type::Error); NWB_SOFTBREAK; }
#define NWB_LOGGER_FATAL(...) { NWB_FATAL_ASSERT(::NWB::Log::__hidden_logger::g_Logger != nullptr); ::NWB::Log::__hidden_logger::g_Logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Log::Type::Fatal); NWB_HARDBREAK; }
#else
#define NWB_LOGGER_ERROR(...) static_cast<void>(sizeof((::NWB::Log::__hidden_logger::IgnoreMessage(__VA_ARGS__), 0)))
#define NWB_LOGGER_FATAL(...) static_cast<void>(sizeof((::NWB::Log::__hidden_logger::IgnoreMessage(__VA_ARGS__), 0)))
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
