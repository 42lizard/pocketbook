#include "integrity.h"
#include <iostream>
#include <exception>
int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) return 2;
    try {
        if(argc==3) { std::cout<<readest::xpointer_cfi(argv[1],argv[2])<<"\n"; return 0; }
        auto book = readest::inspect_epub(argv[1]);
        std::cout << book.readest_hash << "\n" << book.sha256 << "\n" << book.size << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n"; return 1;
    }
}
