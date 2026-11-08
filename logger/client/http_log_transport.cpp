// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "http_log_transport.h"

#include <global/diagnostics.h>
#include <curl/curl.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


namespace __hidden_log_http_transport{

static constexpr long s_ConnectTimeoutMs = 1000L;
static constexpr long s_RequestTimeoutMs = 2000L;

static void CaptureInitializationFailure(const AStringView operation, const CURLcode code)noexcept{
    const DiagnosticEventText detail = MakeDiagnosticEventText("{}: native_category=curl native_code={} detail={}", operation, code, curl_easy_strerror(code));
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = "logger_client_initialization", .message = detail.view(), .file = __FILE__, .line = __LINE__, });
}

};


b8 LogHttpTransport::GlobalInit()noexcept{
    CURLcode curlResult;

    curlResult = curl_global_init(CURL_GLOBAL_ALL);
    if(curlResult != CURLE_OK){
        __hidden_log_http_transport::CaptureInitializationFailure("curl_global_init", curlResult);
        return false;
    }

    return true;
}


LogHttpTransport::~LogHttpTransport()noexcept{
    if(m_curl)
        curl_easy_cleanup(static_cast<CURL*>(m_curl));
}


b8 LogHttpTransport::init(){
    m_curl = curl_easy_init();
    if(!m_curl){
        __hidden_log_http_transport::CaptureInitializationFailure("curl_easy_init", CURLE_FAILED_INIT);
        return false;
    }

    const NotNull<CURL*> curlHandle = MakeNotNull(static_cast<CURL*>(m_curl));
    CURLcode curlResult;

    curlResult = curl_easy_setopt(curlHandle.get(), CURLOPT_POST, 1);
    if(curlResult != CURLE_OK){
        __hidden_log_http_transport::CaptureInitializationFailure("CURLOPT_POST", curlResult);
        return false;
    }

    curlResult = curl_easy_setopt(curlHandle.get(), CURLOPT_NOSIGNAL, 1L);
    if(curlResult != CURLE_OK){
        __hidden_log_http_transport::CaptureInitializationFailure("CURLOPT_NOSIGNAL", curlResult);
        return false;
    }

    curlResult = curl_easy_setopt(curlHandle.get(), CURLOPT_CONNECTTIMEOUT_MS, __hidden_log_http_transport::s_ConnectTimeoutMs);
    if(curlResult != CURLE_OK){
        __hidden_log_http_transport::CaptureInitializationFailure("CURLOPT_CONNECTTIMEOUT_MS", curlResult);
        return false;
    }

    curlResult = curl_easy_setopt(curlHandle.get(), CURLOPT_TIMEOUT_MS, __hidden_log_http_transport::s_RequestTimeoutMs);
    if(curlResult != CURLE_OK){
        __hidden_log_http_transport::CaptureInitializationFailure("CURLOPT_TIMEOUT_MS", curlResult);
        return false;
    }

    return true;
}


LogHttpTransport::Result LogHttpTransport::send(const char* url, const u8* bytes, const usize byteCount){
    const NotNull<CURL*> curlHandle = MakeNotNull(static_cast<CURL*>(m_curl));
    CURLcode result = curl_easy_setopt(curlHandle.get(), CURLOPT_URL, url);
    if(result != CURLE_OK)
        return Result{ static_cast<i32>(result), "setting URL" };
    result = curl_easy_setopt(curlHandle.get(), CURLOPT_POSTFIELDS, reinterpret_cast<const char*>(bytes));
    if(result != CURLE_OK)
        return Result{ static_cast<i32>(result), "setting payload" };
    result = curl_easy_setopt(curlHandle.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(byteCount));
    if(result != CURLE_OK)
        return Result{ static_cast<i32>(result), "setting payload size" };
    result = curl_easy_perform(curlHandle.get());
    if(result != CURLE_OK)
        return Result{ static_cast<i32>(result), "sending payload" };
    long responseCode = 0;
    result = curl_easy_getinfo(curlHandle.get(), CURLINFO_RESPONSE_CODE, &responseCode);
    if(result != CURLE_OK)
        return Result{ static_cast<i32>(result), "reading HTTP status", 0, true };
    return Result{ 0, nullptr, responseCode, true };
}


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
