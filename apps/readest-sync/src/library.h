#pragma once
#include "cloud.h"
#include <vector>
#include <map>

namespace readest {
struct LibraryBook {
    std::string hash, title, author, format, raw;
    bool deleted = false;
    long long cursor = 0;
};
struct BookFiles {
    int epubs=0;
    std::string cover_key;
    long long cover_size=0, cover_stamp=0;
};
// Full successful storage scan only; failure must never imply a missing EPUB.
std::map<std::string,BookFiles> fetch_book_files(Cloud& cloud,long long now,const std::string& hash="");
struct LibraryPage {
    std::vector<LibraryBook> books;
    long long cursor = 0;
    bool more = false;
};
// Parsing limit=0 denotes a complete, server-paginated delta response.
LibraryPage parse_library_page(const std::string& response, const std::string& user_id,
                               long long since, size_t limit);
LibraryPage fetch_library_page(Cloud& cloud, long long since, size_t limit, long long now,
                               const std::string& hash = "");
} // namespace readest
