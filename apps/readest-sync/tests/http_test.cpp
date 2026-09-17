#include "http.h"
#include <iostream>
#include <stdexcept>
#include <cstdio>
#include <memory>
#include <thread>
#include <chrono>
int main(int argc, char** argv) {
    if (argc != 5) return 2;
    try {
        std::atomic<bool> cancelled(std::string(argv[3]) == "CANCEL");
        const auto transport=readest::https_transport();
        const std::string mode=argv[3];
        if(mode=="LATE_CANCEL_GET" || mode=="LATE_CANCEL_DOWNLOAD" || mode=="LATE_CANCEL_UPLOAD") {
            auto close_file=[](FILE* f) { fclose(f); };
            std::unique_ptr<FILE,decltype(close_file)> file(tmpfile(),close_file);
            if(!file) throw std::runtime_error("Cannot create test file");
            if(mode=="LATE_CANCEL_UPLOAD") {
                const std::string chunk(16384,'u');
                for(int i=0;i<1024;++i) if(fwrite(chunk.data(),1,chunk.size(),file.get())!=chunk.size()) return 1;
                fflush(file.get());rewind(file.get());
            }
            std::thread stopper([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); cancelled=true; });
            bool stopped=false;
            try {
                if(mode=="LATE_CANCEL_UPLOAD") transport.upload(argv[1],fileno(file.get()),argv[2],16*1024*1024,cancelled);
                else if(mode=="LATE_CANCEL_DOWNLOAD") transport.download(argv[1],fileno(file.get()),argv[2],std::stoul(argv[4]),cancelled);
                else transport.request(argv[1],"GET",{},"",argv[2],std::stoul(argv[4]),cancelled);
            } catch(const std::exception& e) { stopped=std::string(e.what())=="Cancelled."; }
            stopper.join();
            if(!stopped) throw std::runtime_error("Transfer did not observe cancellation");
            const std::atomic<bool> next{false};
            const auto url=std::string(argv[1]);
            const auto response=transport.request(url.substr(0,url.rfind('/'))+"/","GET",{},"",argv[2],4096,next);
            if(response.status!=200) throw std::runtime_error("Subsequent request failed");
            std::cout<<"Cancelled active transfer; next operation succeeded.";
            return 0;
        }
        if(mode=="UPLOAD") {
            std::unique_ptr<FILE,int(*)(FILE*)> file(tmpfile(),fclose);
            if(!file) throw std::runtime_error("Cannot create test file");
            const std::string bytes(8192,'u');
            if(fwrite(bytes.data(),1,bytes.size(),file.get())!=bytes.size()) return 1;
            fflush(file.get()); rewind(file.get());
            const auto response=transport.upload(argv[1],fileno(file.get()),argv[2],bytes.size(),cancelled);
            std::cout<<response.status<<'\n'<<response.body; return 0;
        }
        if (std::string(argv[3]) == "DOWNLOAD") {
            std::unique_ptr<FILE, int(*)(FILE*)> file(tmpfile(), fclose);
            if (!file) throw std::runtime_error("Cannot create test file");
            const auto response = transport.download(argv[1], fileno(file.get()), argv[2], std::stoul(argv[4]), cancelled);
            std::cout << response.status << '\n' << response.bytes << '\n';
            rewind(file.get());
            char buffer[1024]; size_t n;
            while ((n = fread(buffer, 1, sizeof(buffer), file.get()))) std::cout.write(buffer, n);
            return 0;
        }
        const auto response = transport.request(argv[1], cancelled.load() ? "GET" : argv[3],
            {"Authorization: Bearer fixture-token", "Content-Type: application/json"},
            std::string(argv[3]) == "POST" ? "{\"fixture\":true}" : "", argv[2], std::stoul(argv[4]), cancelled);
        std::cout << response.status << '\n' << response.body;
    } catch (const std::exception& e) { std::cerr << e.what(); return 1; }
}
