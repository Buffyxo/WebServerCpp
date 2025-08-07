#include "http_server.h"
#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        web_server::HttpServer server(8080, "./web");
        server.start();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}