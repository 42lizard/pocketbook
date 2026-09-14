#pragma once
#include "library.h"
#include "integrity.h"

namespace readest {
struct StoredBook {
    std::string path;
    BookIntegrity integrity;
};
using DownloadTransport = std::function<HttpResponse(const std::string&, int, const std::string&, size_t)>;
std::string cover_path(const std::string& root,const std::string& user,const std::string& hash,const BookFiles& files);
enum class CoverFormat { None, PNG, JPEG };
CoverFormat cover_format(const std::string& path);
bool valid_cover(const std::string& path);
// Returns false when the server has no cover.
bool cache_cover(Cloud& cloud,const std::string& root,const std::string& hash,const BookFiles& files,
                 const std::string& ca,long long now,DownloadTransport transfer=https_download);
// Sequential worker operation. Root must be the app's managed Books/Readest
// directory. Each attempt owns a new directory; existing books are never replaced.
StoredBook download_book(Cloud& cloud, const LibraryBook& book, const std::string& root,
                         const std::string& ca, long long now,
                         DownloadTransport transfer = https_download);
} // namespace readest
