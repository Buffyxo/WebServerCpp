#ifndef WEB_SERVER_HTTP_SERVER_H
#define WEB_SERVER_HTTP_SERVER_H

#include <string>
#include <map>
#include <filesystem>
#include <thread>

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
        std::pair<std::string, std::string> parseRequest(const std::string &request);
        // std::string parseRequest(const std::string &request);
        std::string getMimeType(const std::string &path);
        std::string readFile(const std::string &path);
        std::string generateDirectoryListing(const std::string &dir_path, const std::string &relative_path);
        std::string generateUploadForm(const std::string &relative_path);
        std::pair<std::string, std::string> parseMultipartFormData(const std::string &request, const std::string &boundary);
        bool saveUploadedFile(const std::string &filename, const std::string &content, const std::string &destination_dir);
        void sendResponse(socket_t client_socket, const std::string &status,
                          const std::string &content_type, const std::string &content);
    };

}

#endif