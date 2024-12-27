// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <cassert>

#include "client.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger{
    extern Client* g_logger;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_LOGGER_REGISTER(inst) NWB::Log::__hidden_logger::g_logger = inst

#define NWB_LOGGER_INFO(...) assert(NWB::Log::__hidden_logger::g_logger != nullptr) NWB::Log::__hidden_logger::g_logger->enqueue(std::format(NWB_TEXT(__VA_ARGS__)), NWB::Log::Type::Info)
#define NWB_LOGGER_WARNING(...) assert(NWB::Log::__hidden_logger::g_logger != nullptr) NWB::Log::__hidden_logger::g_logger->enqueue(std::format(NWB_TEXT(__VA_ARGS__)), NWB::Log::Type::Warning)
#define NWB_LOGGER_ERROR(...) assert(NWB::Log::__hidden_logger::g_logger != nullptr) NWB::Log::__hidden_logger::g_logger->enqueue(std::format(NWB_TEXT(__VA_ARGS__)), NWB::Log::Type::Error)
#define NWB_LOGGER_FATAL(...) assert(NWB::Log::__hidden_logger::g_logger != nullptr) NWB::Log::__hidden_logger::g_logger->enqueue(std::format(NWB_TEXT(__VA_ARGS__)), NWB::Log::Type::Fatal)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

