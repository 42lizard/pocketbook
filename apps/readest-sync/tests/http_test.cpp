#include "http.h"
#include <iostream>
#include <stdexcept>
#include <cstdio>
#include <memory>
int main(int argc, char** argv) {
    if (argc != 5) return 2;
    try {
        std::atomic<bool> cancelled(std::string(argv[3]) == "CANCEL");
        readest::set_http_cancellation(&cancelled);
        if (std::string(argv[3]) == "DOWNLOAD") {
            std::unique_ptr<FILE, decltype(&fclose)> file(tmpfile(), fclose);
            if (!file) throw std::runtime_error("Cannot create test file");
            const auto response = readest::https_download(argv[1], fileno(file.get()), argv[2], std::stoul(argv[4]));
            std::cout << response.status << '\n' << response.bytes << '\n';
            rewind(file.get());
            char buffer[1024]; size_t n;
            while ((n = fread(buffer, 1, sizeof(buffer), file.get()))) std::cout.write(buffer, n);
            return 0;
        }
        const auto response = readest::https_request(argv[1], cancelled.load() ? "GET" : argv[3],
            {"Authorization: Bearer fixture-token", "Content-Type: application/json"},
            std::string(argv[3]) == "POST" ? "{\"fixture\":true}" : "", argv[2], std::stoul(argv[4]));
        std::cout << response.status << '\n' << response.body;
    } catch (const std::exception& e) { std::cerr << e.what(); return 1; }
}
