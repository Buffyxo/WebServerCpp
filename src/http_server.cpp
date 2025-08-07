#include "http_server.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <fstream>

namespace web_server
{

    const std::map<std::string, std::string> HttpServer::MIME_TYPES = {
        {".html", "text/html"},
        {".txt", "text/plain"},
        {".jpg", "image/jpeg"},
        {".png", "image/png"},
        {".css", "text/css"},
        {".js", "application/javascript"}};

    HttpServer::HttpServer(int port, const std::string &web_root)
        : port_(port), web_root_(web_root), server_socket_(-1)
    {
        if (!std::filesystem::exists(web_root_))
        {
            std::filesystem::create_directory(web_root_);
        }
    }

    HttpServer::~HttpServer()
    {
        if (server_socket_ != -1)
        {
            CLOSE_SOCKET(server_socket_);
        }
        cleanupNetworking();
    }

    void HttpServer::initNetworking()
    {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            throw std::runtime_error("WSAStartup failed");
        }
#endif
    }

    void HttpServer::cleanupNetworking()
    {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    socket_t HttpServer::createServerSocket()
    {
        socket_t server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == -1)
        {
            throw std::runtime_error("Failed to create socket");
        }

        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&opt), sizeof(opt));

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port_);

        if (bind(server_socket, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) < 0)
        {
            CLOSE_SOCKET(server_socket);
            throw std::runtime_error("Failed to bind socket");
        }

        if (listen(server_socket, 10) < 0)
        {
            CLOSE_SOCKET(server_socket);
            throw std::runtime_error("Failed to listen on socket");
        }

        return server_socket;
    }

    std::string HttpServer::parseRequest(const std::string &request)
    {
        std::istringstream stream(request);
        std::string method, path, protocol;
        stream >> method >> path >> protocol;
        if (method != "GET")
        {
            return "";
        }
        size_t query_pos = path.find('?');
        if (query_pos != std::string::npos)
        {
            path = path.substr(0, query_pos);
        }
        std::string decoded_path;
        for (size_t i = 0; i < path.length(); ++i)
        {
            if (path[i] == '%' && i + 2 < path.length())
            {
                std::string hex = path.substr(i + 1, 2);
                try
                {
                    char decoded = static_cast<char>(std::stoi(hex, nullptr, 16));
                    decoded_path += decoded;
                    i += 2;
                }
                catch (...)
                {
                    decoded_path += path[i];
                }
            }
            else
            {
                decoded_path += path[i];
            }
        }
        return decoded_path;
    }

    std::string HttpServer::getMimeType(const std::string &path)
    {
        std::string ext = std::filesystem::path(path).extension().string();
        auto it = MIME_TYPES.find(ext);
        if (it != MIME_TYPES.end())
        {
            return it->second;
        }
        return "application/octet-stream";
    }

    std::string HttpServer::readFile(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return "";
        }
        std::ostringstream content;
        content << file.rdbuf();
        return content.str();
    }

    void HttpServer::sendResponse(socket_t client_socket, const std::string &status,
                                  const std::string &content_type, const std::string &content)
    {
        std::ostringstream response;
        response << "HTTP/1.1 " << status << "\r\n";
        response << "Content-Type: " << content_type << "\r\n";
        response << "Content-Length: " << content.length() << "\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << content;

        std::string response_str = response.str();
        send(client_socket, response_str.c_str(), response_str.length(), 0);
    }

    void HttpServer::handleClient(socket_t client_socket)
    {
        char buffer[4096] = {0};
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0)
        {
            CLOSE_SOCKET(client_socket);
            return;
        }
        std::string request(buffer, bytes_received);
        std::string path = parseRequest(request);
        if (path.empty())
        {
            sendResponse(client_socket, "400 Bad Request", "text/plain", "Only GET requests are supported");
            CLOSE_SOCKET(client_socket);
            return;
        }

        std::string file_path = web_root_ + (path == "/" ? "/index.html" : path);
        std::filesystem::path canonical_path = std::filesystem::canonical(web_root_) / path.substr(1);
        if (!canonical_path.string().starts_with(std::filesystem::canonical(web_root_).string()))
        {
            sendResponse(client_socket, "403 Forbidden", "text/plain", "Access denied");
            CLOSE_SOCKET(client_socket);
            return;
        }

        std::string content = readFile(file_path);
        if (content.empty())
        {
            sendResponse(client_socket, "404 Not Found", "text/plain", "File not found");
        }
        else
        {
            std::string mime_type = getMimeType(file_path);
            sendResponse(client_socket, "200 OK", mime_type, content);
        }
        CLOSE_SOCKET(client_socket);
    }

    void HttpServer::start()
    {
        server_socket_ = createServerSocket();
        std::cout << "Server running on port " << port_ << "\n";
        while (true)
        {
            socket_t client_socket = accept(server_socket_, nullptr, nullptr);
            if (client_socket == -1)
            {
                std::cerr << "Failed to accept connection\n";
                continue;
            }
            handleClient(client_socket);
        }
    }

}