#include "http_server.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <fstream>
#include <regex>
#include <algorithm>

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

    // Comparison function for sorting directory entries
    bool comparePaths(const std::filesystem::directory_entry &a, const std::filesystem::directory_entry &b)
    {
        return a.path().filename().string() < b.path().filename().string();
    }

    std::string HttpServer::generateDirectoryTree(const std::string &dir_path, const std::string &relative_path, int depth)
    {
        std::ostringstream html;
        try
        {
            std::vector<std::filesystem::directory_entry> directories;
            std::vector<std::filesystem::directory_entry> files;

            // Separate directories and files for sorting
            for (const auto &entry : std::filesystem::directory_iterator(dir_path))
            {
                if (std::filesystem::is_directory(entry))
                {
                    directories.push_back(entry);
                }
                else
                {
                    files.push_back(entry);
                }
            }

            // Sort directories and files alphabetically
            std::sort(directories.begin(), directories.end(), comparePaths);
            std::sort(files.begin(), files.end(), comparePaths);

            // Process directories
            for (const auto &entry : directories)
            {
                std::string name = entry.path().filename().string();
                std::string link = relative_path + (relative_path == "/" ? "" : "/") + name;
                std::string tree_id = "tree-" + relative_path + "/" + name;
                std::replace(tree_id.begin(), tree_id.end(), '/', '_'); // Replace / with _ for valid ID
                html << "<li class='directory'>";
                html << "<span class='toggle' onclick=\"toggleTree('" << tree_id << "')\">"
                     << "<span class='arrow'>&#9654;</span> "
                     << "<a href='" << link << "/' onclick=\"event.stopPropagation();\">" << name << "/</a>"
                     << "</span>";
                html << "<ul id='" << tree_id << "' style='display: none;'>";
                html << generateDirectoryTree(entry.path().string(), link, depth + 1);
                html << "</ul></li>";
            }

            // Process files
            for (const auto &entry : files)
            {
                std::string name = entry.path().filename().string();
                std::string link = relative_path + (relative_path == "/" ? "" : "/") + name;
                html << "<li class='file'><a href='" << link << "'>" << name << "</a></li>";
            }
        }
        catch (const std::filesystem::filesystem_error &e)
        {
            html << "<li>Error reading directory: " << e.what() << "</li>";
        }
        std::string result = html.str();

        return result;
    }

    std::string HttpServer::generateDirectoryListing(const std::string &dir_path, const std::string &relative_path)
    {
        std::string template_path = web_root_ + "/templates/tree_template.html";
        std::string html = readFile(template_path);
        if (html.empty())
        {
            std::cout << "Failed to load template, using fallback\n";
            html = "<!DOCTYPE html><html><head><title>Directory Listing</title></head><body>"
                   "<h1>Directory: {{RELATIVE_PATH}}</h1>"
                   "<form action='/upload?path={{RELATIVE_PATH}}' method='post' enctype='multipart/form-data'>"
                   "<input type='file' name='file'><input type='submit' value='Upload'>"
                   "</form><ul>{{TREE_CONTENT}}</ul></body></html>";
        }

        std::string clean_relative_path = relative_path;

        if (!clean_relative_path.empty() && clean_relative_path.back() == '}')
        {
            clean_relative_path = clean_relative_path.substr(0, clean_relative_path.length() - 1);
        }

        std::string tree_content = generateDirectoryTree(dir_path, relative_path, 0);
        if (!tree_content.empty() && tree_content.back() == '}')
        {
            tree_content = tree_content.substr(0, tree_content.length() - 1);
        }

        // Replace placeholders
        std::string result = html;
        size_t pos;
        while ((pos = result.find("{{RELATIVE_PATH}}")) != std::string::npos)
        {
            result.replace(pos, 17, clean_relative_path);
        }
        while ((pos = result.find("{{TREE_CONTENT}}")) != std::string::npos)
        {
            result.replace(pos, 16, tree_content);
        }

        return result;
    }

    std::string HttpServer::generateUploadForm(const std::string &relative_path)
    {
        std::string template_path = web_root_ + "/templates/upload_template.html";
        std::string html = readFile(template_path);
        if (html.empty())
        {
            std::cout << "Failed to load upload template, using fallback\n";
            html = "<!DOCTYPE html><html><head><title>Upload File</title></head><body>"
                   "<h1>Upload to {{RELATIVE_PATH}}</h1>"
                   "<form action='/upload?path={{RELATIVE_PATH}}' method='post' enctype='multipart/form-data'>"
                   "<input type='file' name='file'><input type='submit' value='Upload'>"
                   "</form></body></html>";
        }

        std::string clean_relative_path = relative_path;
        if (clean_relative_path.empty() || clean_relative_path == "/")
        {
            clean_relative_path = "/";
        }

        std::string result = html;
        size_t pos;
        while ((pos = result.find("{{RELATIVE_PATH}}")) != std::string::npos)
        {
            result.replace(pos, 17, clean_relative_path);
        }

        std::cout << "Upload form HTML size: " << result.size() << " bytes\n";
        return result;
    }

    std::pair<std::string, std::string> HttpServer::parseMultipartFormData(const std::string &request, const std::string &boundary)
    {
        std::cout << "Parsing multipart form-data with boundary: " << boundary << "\n";
        std::string delimiter = "--" + boundary;      // "--abc123"
        std::string end_delimiter = delimiter + "--"; // "--abc123--"

        size_t start = request.find(delimiter); // Finds the first "--abc123--"
        if (start == std::string::npos)
        {
            std::cout << "Failed to find start delimiter: " << delimiter << "\n";
            return {"", ""};
        }
        size_t part_start = start + delimiter.length() + 2;          // Skip "--abc123\r\n" (delimeter + CRLF)
        size_t next_delimiter = request.find(delimiter, part_start); // not necessary but useful for other use cases
        size_t end = request.find(end_delimiter, start);
        if (end == std::string::npos)
        {
            end = request.length();
            std::cout << "No end delimiter found, using request length: " << end << "\n";
        }
        if (next_delimiter != std::string::npos && next_delimiter < end)
        {
            end = next_delimiter;
        }

        std::string part = request.substr(part_start, end - part_start - 2); // Remove trailing \r\n and extracts a single part

        // Extract filename from header in the part
        std::regex disposition_regex(R"delim(Content-Disposition:.*filename="([^"]+)")delim");
        std::smatch match;
        if (!std::regex_search(part, match, disposition_regex))
        {
            std::cout << "Failed to find filename in Content-Disposition\n";
            return {"", ""}; // if no filename is found
        }
        std::string filename = match[1].str();
        std::cout << "Extracted filename: " << filename << "\n";

        // Find where headers end and content starts in the part
        size_t content_start = part.find("\r\n\r\n");
        if (content_start == std::string::npos)
        {
            std::cout << "Failed to find content start after headers\n";
            return {"", ""};
        }
        content_start += 4;                               // Skip \r\n\r\n (blank line)
        std::string content = part.substr(content_start); // Extract the file's binary data as a a string
        std::cout << "Extracted content length: " << content.length() << " bytes\n";
        return {filename, content};
    }

    bool HttpServer::saveUploadedFile(const std::string &filename, const std::string &content, const std::string &destination_dir)
    {
        std::cout << "Attempting to save file: " << filename << " to " << destination_dir << "\n";
        if (filename.empty() || filename.find("..") != std::string::npos || filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos)
        {
            std::cout << "Invalid filename: " << filename << "\n";
            return false;
        }
        std::filesystem::path file_path = std::filesystem::path(destination_dir) / filename;
        std::cout << "Constructed file path: " << file_path.string() << "\n";
        try
        {
            std::filesystem::create_directories(destination_dir);
            std::filesystem::path canonical_path = std::filesystem::canonical(destination_dir) / filename;
            std::cout << "Canonical path: " << canonical_path.string() << "\n";
            if (!canonical_path.string().starts_with(std::filesystem::canonical(web_root_).string()))
            {
                std::cout << "Directory traversal detected: " << canonical_path.string() << " not in " << web_root_ << "\n";
                return false;
            }
            std::ofstream file(file_path, std::ios::binary);
            if (!file)
            {
                std::cout << "Failed to open file for writing: " << file_path.string() << "\n";
                return false;
            }
            file.write(content.c_str(), content.length());
            if (!file.good())
            {
                std::cout << "Failed to write content to file: " << file_path.string() << "\n";
                return false;
            }
            std::cout << "Successfully wrote file: " << file_path.string() << ", size: " << content.length() << " bytes\n";
            return true;
        }
        catch (const std::filesystem::filesystem_error &e)
        {
            std::cout << "Filesystem error: " << e.what() << "\n";
            return false;
        }
    }

    void HttpServer::sendResponse(socket_t client_socket, const std::string &status,
                                  const std::string &content_type, const std::string &content)
    {
        std::ostringstream response;
        response << "HTTP/1.1 " << status << "\r\n";
        response << "Content-Type: " << content_type << "; charset=UTF-8\r\n";
        response << "Content-Length: " << content.length() << "\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << content;

        std::string response_str = response.str();
        send(client_socket, response_str.c_str(), response_str.length(), 0);
    }

    void HttpServer::handleClient(socket_t client_socket)
    {

        char buffer[32768] = {0}; // Increased to 32KB
        std::string request;
        size_t total_bytes = 0;

        // Read headers to find Content-Length
        std::string headers;
        while (true)
        {
            int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
            if (bytes_received <= 0)
            {
                std::cout << "Failed to receive data from client\n";
                CLOSE_SOCKET(client_socket);
                return;
            }
            buffer[bytes_received] = '\0';
            headers += std::string(buffer, bytes_received);
            total_bytes += bytes_received;
            if (headers.find("\r\n\r\n") != std::string::npos)
            {
                break;
            }
        }

        // Extract Content-Length
        size_t content_length = 0;
        std::regex content_length_regex(R"(Content-Length: (\d+))");
        std::smatch match;
        if (std::regex_search(headers, match, content_length_regex))
        {
            content_length = std::stoul(match[1].str());
            std::cout << "Content-Length: " << content_length << " bytes\n";
        }

        // Read remaining body based on Content-Length
        request = headers;
        size_t body_received = headers.length() - (headers.find("\r\n\r\n") + 4);
        while (body_received < content_length)
        {
            int bytes_received = recv(client_socket, buffer, std::min<size_t>(sizeof(buffer) - 1, content_length - body_received), 0);
            if (bytes_received <= 0)
            {
                std::cout << "Failed to receive full body, received: " << body_received << "/" << content_length << "\n";
                CLOSE_SOCKET(client_socket);
                return;
            }
            buffer[bytes_received] = '\0';
            request += std::string(buffer, bytes_received);
            body_received += bytes_received;
            total_bytes += bytes_received;
        }

        auto [path, method_query] = parseRequest(request);
        std::string method = method_query.substr(0, method_query.find('?'));
        std::string query = method_query.find('?') != std::string::npos ? method_query.substr(method_query.find('?')) : "";

        if (method.empty())
        {
            std::cout << "Unsupported method\n";
            sendResponse(client_socket, "400 Bad Request", "text/plain", "Only GET and POST requests are supported");
            CLOSE_SOCKET(client_socket);
            return;
        }

        if (method == "POST" && path == "/upload")
        {
            std::string destination_path = "/";
            std::regex path_regex(R"(\?path=([^ \r\n]*))");
            if (std::regex_search(query, match, path_regex))
            {
                destination_path = match[1].str();
                std::cout << "Upload destination path: " << destination_path << "\n";
            }
            std::string file_path = web_root_ + (destination_path == "/" ? "" : destination_path);
            std::cout << "Upload file path: " << file_path << "\n";
            try
            {
                std::filesystem::path canonical_path = std::filesystem::canonical(web_root_) / (destination_path == "/" ? "" : destination_path.substr(1));
                if (!canonical_path.string().starts_with(std::filesystem::canonical(web_root_).string()))
                {
                    std::cout << "Directory traversal detected in upload path: " << canonical_path.string() << "\n";
                    sendResponse(client_socket, "403 Forbidden", "text/plain", "Access denied");
                    CLOSE_SOCKET(client_socket);
                    return;
                }
                if (!std::filesystem::is_directory(file_path))
                {
                    std::cout << "Upload destination is not a directory: " << file_path << "\n";
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
                        std::cout << "Upload failed: filename=" << filename << ", content_length=" << content.length() << "\n";
                        sendResponse(client_socket, "400 Bad Request", "text/plain", "Failed to upload file");
                    }
                    else
                    {
                        std::cout << "Upload succeeded: " << filename << " to " << file_path << "\n";
                        sendResponse(client_socket, "200 OK", "text/plain", "File uploaded successfully");
                    }
                }
                else
                {
                    std::cout << "Invalid multipart/form-data in POST request\n";
                    sendResponse(client_socket, "400 Bad Request", "text/plain", "Invalid multipart/form-data");
                }
            }
            catch (const std::filesystem::filesystem_error &e)
            {
                std::cout << "Filesystem error in upload: " << e.what() << "\n";
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
            if (path.find("/templates/") == 0)
            {
                sendResponse(client_socket, "403 Forbidden", "text/plain", "Access to templates directory is forbidden");
                CLOSE_SOCKET(client_socket);
                return;
            }
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