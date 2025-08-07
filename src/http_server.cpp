#include "http_server.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <fstream>
#include <regex>

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

    std::pair<std::string, std::string> HttpServer::parseRequest(const std::string &request)
    {
        std::istringstream stream(request);
        std::string method, path, protocol;
        stream >> method >> path >> protocol;
        if (method != "GET" && method != "POST")
        {
            return {"", ""};
        }
        size_t query_pos = path.find('?');
        std::string query;
        if (query_pos != std::string::npos)
        {
            query = path.substr(query_pos);
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
        return {decoded_path, method + query};
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

    std::string HttpServer::generateDirectoryListing(const std::string &dir_path, const std::string &relative_path)
    {
        std::ostringstream html;
        html << "<!DOCTYPE html><html><head><title>Directory Listing</title></head><body>";
        html << "<h1>Directory: " << relative_path << "</h1><ul>";
        html << "<form action=\"/upload?path=" << relative_path << "\" method=\"post\" enctype=\"multipart/form-data\">";
        html << "<input type=\"file\" name=\"file\"><input type=\"submit\" value=\"Upload\"></form>";
        html << "<ul>";
        try
        {
            for (const auto &entry : std::filesystem::directory_iterator(dir_path))
            {
                std::string name = entry.path().filename().string();
                std::string link = relative_path + (relative_path == "/" ? "" : "/") + name;
                if (std::filesystem::is_directory(entry))
                {
                    html << "<li><a href=\"" << link << "/\">" << name << "/</a></li>";
                }
                else
                {
                    html << "<li><a href=\"" << link << "\">" << name << "</a></li>";
                }
            }
        }
        catch (const std::filesystem::filesystem_error &e)
        {
            html << "<li>Error reading directory: " << e.what() << "</li>";
        }
        html << "</ul></body></html>";
        return html.str();
    }

    std::string HttpServer::generateUploadForm(const std::string &relative_path)
    {
        std::ostringstream html;
        html << "<!DOCTYPE html><html><head><title>Upload File</title></head><body>";
        html << "<h1>Upload to " << relative_path << "</h1>";
        html << "<form action=\"/upload?path=" << relative_path << "\" method=\"post\" enctype=\"multipart/form-data\">";
        html << "<input type=\"file\" name=\"file\"><input type=\"submit\" value=\"Upload\">";
        html << "</form></body></html>";
        return html.str();
    }

    std::pair<std::string, std::string> HttpServer::parseMultipartFormData(const std::string &request, const std::string &boundary)
    {
        std::string delimiter = "--" + boundary;
        std::string end_delimiter = delimiter + "--";
        size_t start = request.find(delimiter);
        if (start == std::string::npos)
        {
            return {"", ""};
        }
        size_t end = request.find(end_delimiter, start);
        if (end == std::string::npos)
        {
            end = request.length();
        }

        std::string part = request.substr(start + delimiter.length(), end - start - delimiter.length());
        std::regex disposition_regex(R"delim(Content-Disposition:.*filename="([^"]+)")delim");
        std::smatch match;
        if (!std::regex_search(part, match, disposition_regex))
        {
            return {"", ""};
        }
        std::string filename = match[1].str();

        size_t content_start = part.find("\r\n\r\n") + 4;
        if (content_start == std::string::npos || content_start >= part.length())
        {
            return {"", ""};
        }
        std::string content = part.substr(content_start);
        return {filename, content};
    }

    bool HttpServer::saveUploadedFile(const std::string &filename, const std::string &content, const std::string &destination_dir)
    {
        if (filename.empty() || filename.find("..") != std::string::npos || filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos)
        {
            return false;
        }
        std::filesystem::path file_path = std::filesystem::path(destination_dir) / filename;
        try
        {
            std::filesystem::path canonical_path = std::filesystem::canonical(destination_dir) / filename;
            if (!canonical_path.string().starts_with(std::filesystem::canonical(web_root_).string()))
            {
                return false;
            }
            std::ofstream file(file_path, std::ios::binary);
            if (!file)
            {
                return false;
            }
            file.write(content.c_str(), content.length());
            return file.good();
        }
        catch (const std::filesystem::filesystem_error &)
        {
            return false;
        }
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
        char buffer[8192] = {0};
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0)
        {
            CLOSE_SOCKET(client_socket);
            return;
        }
        std::string request(buffer, bytes_received);
        auto [path, method_query] = parseRequest(request);
        std::string method = method_query.substr(0, method_query.find('?'));
        std::string query = method_query.find('?') != std::string::npos ? method_query.substr(method_query.find('?')) : "";

        if (method.empty())
        {
            sendResponse(client_socket, "400 Bad Request", "text/plain", "Only GET and POST requests are supported");
            CLOSE_SOCKET(client_socket);
            return;
        }

        if (method == "POST" && path == "/upload")
        {
            std::string destination_path = "/";
            std::regex path_regex(R"(\?path=([^ \r\n]*))");
            std::smatch match;
            if (std::regex_search(query, match, path_regex))
            {
                destination_path = match[1].str();
            }
            std::string file_path = web_root_ + (destination_path == "/" ? "" : destination_path);
            try
            {
                std::filesystem::path canonical_path = std::filesystem::canonical(web_root_) / (destination_path == "/" ? "" : destination_path.substr(1));
                if (!canonical_path.string().starts_with(std::filesystem::canonical(web_root_).string()))
                {
                    sendResponse(client_socket, "403 Forbidden", "text/plain", "Access denied");
                    CLOSE_SOCKET(client_socket);
                    return;
                }
                if (!std::filesystem::is_directory(file_path))
                {
                    sendResponse(client_socket, "400 Bad Request", "text/plain", "Upload destination must be a directory");
                    CLOSE_SOCKET(client_socket);
                    return;
                }
                std::regex boundary_regex(R"(Content-Type: multipart/form-data; boundary=([^\r\n]+))");
                if (std::regex_search(request, match, boundary_regex))
                {
                    auto [filename, content] = parseMultipartFormData(request, match[1].str());
                    if (filename.empty() || !saveUploadedFile(filename, content, file_path))
                    {
                        sendResponse(client_socket, "400 Bad Request", "text/plain", "Failed to upload file");
                    }
                    else
                    {
                        sendResponse(client_socket, "200 OK", "text/plain", "File uploaded successfully");
                    }
                }
                else
                {
                    sendResponse(client_socket, "400 Bad Request", "text/plain", "Invalid multipart/form-data");
                }
            }
            catch (const std::filesystem::filesystem_error &)
            {
                sendResponse(client_socket, "404 Not Found", "text/plain", "Upload path not found");
            }
            CLOSE_SOCKET(client_socket);
            return;
        }

        if (method == "GET" && path == "/upload")
        {
            std::string destination_path = "/";
            std::regex path_regex(R"(\?path=([^ \r\n]*))");
            std::smatch match;
            if (std::regex_search(query, match, path_regex))
            {
                destination_path = match[1].str();
            }
            std::string upload_form = generateUploadForm(destination_path);
            sendResponse(client_socket, "200 OK", "text/html", upload_form);
            CLOSE_SOCKET(client_socket);
            return;
        }

        std::string file_path = web_root_ + (path == "/" ? "" : path);
        std::filesystem::path canonical_path;
        try
        {
            canonical_path = std::filesystem::canonical(web_root_) / (path == "/" ? "" : path.substr(1));
        }
        catch (const std::filesystem::filesystem_error &)
        {
            sendResponse(client_socket, "404 Not Found", "text/plain", "Path not found");
            CLOSE_SOCKET(client_socket);
            return;
        }
        if (!canonical_path.string().starts_with(std::filesystem::canonical(web_root_).string()))
        {
            sendResponse(client_socket, "403 Forbidden", "text/plain", "Access denied");
            CLOSE_SOCKET(client_socket);
            return;
        }

        if (std::filesystem::is_directory(file_path))
        {
            std::string listing = generateDirectoryListing(file_path, path);
            sendResponse(client_socket, "200 OK", "text/html", listing);
        }
        else
        {
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
            // handleClient(client_socket);
            std::thread client_thread(&HttpServer::handleClient, this, client_socket);
            client_thread.detach();
        }
    }

} // namespace web_server