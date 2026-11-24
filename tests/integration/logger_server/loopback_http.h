// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/inplace_function.h>
#include <global/span.h>
#include <microhttpd.h>
#if defined(FOUNDATION_PLATFORM_LINUX)
#include <arpa/inet.h>
#include <netinet/in.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::LoopbackHttp{

class Request final : NoCopy{
public:
    Request(MHD_Connection& connection, const AStringView method, const AStringView path, const char* data, size_t& size, void*& token, void* server)noexcept
        : m_connection(connection), m_method(method), m_path(path), m_data(data), m_size(size), m_token(token), m_server(server){}

    [[nodiscard]] AStringView method()const noexcept{ return m_method; }
    [[nodiscard]] AStringView path()const noexcept{ return m_path; }
    [[nodiscard]] b8 begin()noexcept{
        if(m_token)
            return false;
        m_token = m_server;
        return true;
    }
    [[nodiscard]] Span<const u8> body()const noexcept{ return { reinterpret_cast<const u8*>(m_data), m_size }; }
    void consume()noexcept{ m_size = 0u; }
    [[nodiscard]] b8 respond(const u32 status)noexcept{
        static char s_EmptyBody[1]{};
        auto* response = MHD_create_response_from_buffer(0u, s_EmptyBody, MHD_RESPMEM_PERSISTENT);
        if(!response)
            return false;
        const auto result = MHD_queue_response(&m_connection, status, response);
        MHD_destroy_response(response);
        return result == MHD_YES;
    }

private:
    MHD_Connection& m_connection;
    AStringView m_method;
    AStringView m_path;
    const char* m_data;
    size_t& m_size;
    void*& m_token;
    void* m_server;
};

class Server final : NoCopy{
public:
    using Handler = InplaceFunction<64u, b8(Request&)>;

    ~Server()noexcept{ stop(); }
    [[nodiscard]] b8 start(const Handler& handler, u16& port){
        if(m_daemon)
            return false;
        m_handler = handler;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        m_daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, 0u, nullptr, nullptr, &OnRequest, this,
            MHD_OPTION_SOCK_ADDR_LEN, static_cast<socklen_t>(sizeof(address)), &address, MHD_OPTION_END);
        if(!m_daemon)
            return false;
        const auto* info = MHD_get_daemon_info(m_daemon, MHD_DAEMON_INFO_LISTEN_FD);
        socklen_t size = sizeof(address);
        if(!info || info->listen_fd == MHD_INVALID_SOCKET || ::getsockname(info->listen_fd, reinterpret_cast<sockaddr*>(&address), &size) != 0
            || size != sizeof(address) || address.sin_family != AF_INET || !address.sin_port){
            stop();
            return false;
        }
        port = ntohs(address.sin_port);
        return true;
    }
    void stop()noexcept{
        if(m_daemon){
            MHD_stop_daemon(m_daemon);
            m_daemon = nullptr;
        }
    }

private:
    static MHD_Result OnRequest(void* server, MHD_Connection* connection, const char* path, const char* method, const char*, const char* data, size_t* size, void** token)noexcept{
        if(!server || !connection || !method || !size || !token)
            return MHD_NO;
        Request request(*connection, method, path ? AStringView(path) : AStringView(), data, *size, *token, server);
        return static_cast<Server*>(server)->m_handler(request) ? MHD_YES : MHD_NO;
    }

    Handler m_handler;
    MHD_Daemon* m_daemon = nullptr;
};

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

