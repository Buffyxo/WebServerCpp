#ifndef WEB_SERVER_HTTP_SERVER_H
#define WEB_SERVER_HTTP_SERVER_H

#include <string>
#include <map>
#include <filesystem>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define CLOSE_SOCKET closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
using socket_t = int;
#define CLOSE_SOCKET close
#endif

namespace web_server
{

    class HttpServer
    {
    public:
        HttpServer(int port, const std::string &web_root);
        ~HttpServer();
        void start();

    private:
        int port_;
        std::string web_root_;
        socket_t server_socket_;
        static const std::map<std::string, std::string> MIME_TYPES;

        void initNetworking();
        void cleanupNetworking();
        socket_t createServerSocket();
        void handleClient(socket_t client_socket);
        std::string parseRequest(const std::string &request);
        std::string getMimeType(const std::string &path);
        std::string readFile(const std::string &path);
        std::string generateDirectoryListing(const std::string &dir_path, const std::string &relative_path);
        void sendResponse(socket_t client_socket, const std::string &status,
                          const std::string &content_type, const std::string &content);
    };

}

#endif