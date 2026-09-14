#include "download.h"
#include "json_util.h"
#include <cerrno>
#include <cctype>
#include <fcntl.h>
#include <map>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace readest {
namespace {
std::string filename_component(const std::string& value, size_t limit) {
    std::string result;
    for (unsigned char c : value) {
        if (c <= 32 || c == 127 || std::strchr("<>:\"/\\|?*", c)) c = ' ';
        if (c == ' ' && (result.empty() || result.back() == ' ')) continue;
        result += static_cast<char>(c);
    }
    const auto start = result.find_first_not_of(" .");
    if (start == std::string::npos) return "";
    result.erase(0, start);
    if (result.size() > limit) {
        // Keep UTF-8 characters intact at the byte limit.
        while (limit && (static_cast<unsigned char>(result[limit]) & 0xc0) == 0x80) --limit;
        result.resize(limit);
    }
    const auto end = result.find_last_not_of(" .");
    result.resize(end == std::string::npos ? 0 : end + 1);
    return result;
}
std::string epub_filename(const LibraryBook& book) {
    auto title = filename_component(book.title, 120);
    const auto author = filename_component(book.author, 60);
    if (title.empty()) title = "Untitled";
    std::string stem = title + (author.empty() ? "" : " - " + author);
    auto base = stem.substr(0, stem.find('.'));
    for (auto& c : base) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
        (base.size() == 4 && (base.substr(0, 3) == "COM" || base.substr(0, 3) == "LPT") &&
         base[3] >= '1' && base[3] <= '9')) stem.insert(0, "_");
    return stem + ".epub";
}
std::string encode(const std::string& s) {
    const char* hex = "0123456789ABCDEF"; std::string result;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') result += c;
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}
std::string signed_url(Cloud& cloud, const std::string& key, long long now) {
    auto response = cloud.get("/api/storage/download?fileKey=" + encode(key), now);
    if (response.status != 200) throw std::runtime_error("Cannot obtain book download URL");
    auto body = parse_json(response.body);
    return string_member(body.get(), "downloadUrl"); // HTTPS enforced by transport.
}
void write_all(int fd, const std::string& text) {
    size_t at = 0;
    while (at < text.size()) {
        auto n = write(fd, text.data() + at, text.size() - at);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("Cannot save downloaded book metadata");
        at += static_cast<size_t>(n);
    }
}
void flush_directory(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) throw std::runtime_error("Cannot open downloaded book directory");
    const int result = fsync(fd), error = errno;
    close(fd);
    // FAT can reject directory fsync; file contents have already been flushed.
    if (result != 0 && error != EINVAL && error != ENOTSUP)
        throw std::runtime_error("Cannot flush downloaded book directory");
}
struct Attempt {
    std::string dir, temporary, metadata, final;
    int fd = -1;
    bool installed = false;
    ~Attempt() {
        if (fd >= 0) close(fd);
        if (!installed && !dir.empty()) {
            unlink(temporary.c_str()); unlink(metadata.c_str()); rmdir(dir.c_str());
        }
    }
};
}
StoredBook download_book(Cloud& cloud, const LibraryBook& book, const std::string& root,
                         const std::string& ca, long long now, DownloadTransport transfer) {
    std::string format = book.format;
    for (auto& c : format) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (book.deleted || format != "epub" || book.hash.size() != 32 ||
        book.hash.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Select an available EPUB book");
    const std::string user = cloud.session().user_id;
    if (user.empty()) throw std::runtime_error("Sign in before downloading");
    std::map<std::string, long long> candidates;
    for (int page = 1;; ++page) {
        if (page > 100) throw std::runtime_error("Too many storage pages for one book");
        auto response = cloud.get("/api/storage/list?bookHash=" + book.hash +
                                  "&page=" + std::to_string(page) + "&pageSize=1000", now);
        if (response.status != 200) throw std::runtime_error("Cannot list uploaded book files");
        auto body = parse_json(response.body);
        auto* files = member(body.get(), "files");
        auto total_pages = integer_member(body.get(), "totalPages");
        if (!files || json_object_get_type(files) != json_type_array ||
            integer_member(body.get(), "page") != page || total_pages < 0 || total_pages > 100)
            throw std::runtime_error("Invalid storage listing");
        const size_t count = json_object_array_length(files);
        for (size_t i = 0; i < count; ++i) {
            auto* file = json_object_array_get_idx(files, i);
            if (string_member(file, "book_hash") != book.hash)
                throw std::runtime_error("Storage book identity mismatch");
            auto key = string_member(file, "file_key");
            auto extension = key.size() < 5 ? "" : key.substr(key.size() - 5);
            for (auto& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (extension != ".epub") continue;
            auto size = integer_member(file, "file_size");
            if (key.size() > 4096 || key.find('\0') != std::string::npos || size <= 0 || size > 256LL * 1024 * 1024)
                throw std::runtime_error("Unsupported uploaded EPUB");
            auto existing = candidates.find(key);
            if (existing != candidates.end() && existing->second != size)
                throw std::runtime_error("Book file changed during listing");
            candidates[key] = size;
        }
        if (page >= total_pages) break;
    }
    if (candidates.size() != 1)
        throw std::runtime_error(candidates.empty() ? "Upload the EPUB to Readest first" : "Multiple EPUB files found for this book");
    const auto candidate = *candidates.begin();
    std::string url = signed_url(cloud, candidate.first, now);
    struct stat st;
    if (lstat(root.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        throw std::runtime_error("Managed books directory is unavailable");
    Attempt attempt;
    std::string pattern = root + "/" + book.hash + "-XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end()); name.push_back(0);
    if (!mkdtemp(name.data())) throw std::runtime_error("Cannot create book download directory");
    attempt.dir = name.data(); attempt.temporary = attempt.dir + "/download.part";
    attempt.metadata = attempt.dir + "/readest.json";
    attempt.final = attempt.dir + "/" + epub_filename(book);
    attempt.fd = open(attempt.temporary.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (attempt.fd < 0) throw std::runtime_error("Cannot create download file");
    auto response = transfer(url, attempt.fd, ca, static_cast<size_t>(candidate.second));
    if (response.status == 401 || response.status == 403) {
        // Signed URLs may expire. Obtain one fresh URL and retry into this same
        // private temporary file; never refresh or forward bearer auth to storage.
        url = signed_url(cloud, candidate.first, now);
        if (ftruncate(attempt.fd, 0) != 0 || lseek(attempt.fd, 0, SEEK_SET) < 0)
            throw std::runtime_error("Cannot restart download");
        response = transfer(url, attempt.fd, ca, static_cast<size_t>(candidate.second));
    }
    if (response.status != 200 || response.bytes != static_cast<size_t>(candidate.second) || fsync(attempt.fd) != 0)
        throw std::runtime_error("Book download was incomplete");
    StoredBook stored; stored.integrity = inspect_epub(attempt.temporary);
    if (stored.integrity.readest_hash != book.hash || stored.integrity.size != candidate.second || cloud.session().user_id != user)
        throw std::runtime_error("Downloaded book identity mismatch");
    if (close(attempt.fd) != 0) { attempt.fd = -1; throw std::runtime_error("Cannot finish book download"); }
    attempt.fd = -1;
    Json metadata(json_object_new_object(), json_object_put);
    json_object_object_add(metadata.get(), "userId", json_object_new_string(user.c_str()));
    json_object_object_add(metadata.get(), "bookHash", json_object_new_string(book.hash.c_str()));
    json_object_object_add(metadata.get(), "sha256", json_object_new_string(stored.integrity.sha256.c_str()));
    json_object_object_add(metadata.get(), "size", json_object_new_int64(stored.integrity.size));
    json_object_object_add(metadata.get(), "filename", json_object_new_string(attempt.final.substr(attempt.dir.size() + 1).c_str()));
    attempt.fd = open(attempt.metadata.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (attempt.fd < 0) throw std::runtime_error("Cannot create book metadata");
    write_all(attempt.fd, json_text(metadata.get()));
    if (fsync(attempt.fd) != 0) throw std::runtime_error("Cannot save book metadata");
    if (close(attempt.fd) != 0) { attempt.fd = -1; throw std::runtime_error("Cannot finish book metadata"); }
    attempt.fd = -1;
    if (rename(attempt.temporary.c_str(), attempt.final.c_str()) != 0)
        throw std::runtime_error("Cannot install downloaded book");
    attempt.installed = true; stored.path = attempt.final;
    flush_directory(attempt.dir);
    flush_directory(root);
    return stored;
}
std::string cover_path(const std::string& root,const std::string& user,const std::string& hash,const BookFiles& files) {
    if(user.empty() || hash.size()!=32 || hash.find_first_not_of("0123456789abcdef")!=std::string::npos || files.cover_stamp<0)
        return "";
    return root+"/cover-"+encode(user)+"-"+hash+"-"+std::to_string(files.cover_stamp)+"-"+std::to_string(files.cover_size)+".png";
}
CoverFormat cover_format(const std::string& path) {
    int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW);
    if(fd<0) return CoverFormat::None;
    struct stat st;
    if(fstat(fd,&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<16 || st.st_size>2*1024*1024) {
        close(fd); return CoverFormat::None;
    }
    std::vector<unsigned char> data(static_cast<size_t>(st.st_size)); size_t at=0;
    while(at<data.size()) {
        const auto n=read(fd,data.data()+at,data.size()-at);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) { close(fd); return CoverFormat::None; }
        at+=static_cast<size_t>(n);
    }
    close(fd);
    auto bounded=[](unsigned w,unsigned h) {
        return w>0 && h>0 && w<=4096 && h<=4096 && static_cast<unsigned long long>(w)*h<=4*1024*1024;
    };
    const unsigned char signature[]={137,80,78,71,13,10,26,10};
    if(data.size()>=24 && !std::memcmp(data.data(),signature,8) && !std::memcmp(data.data()+12,"IHDR",4)) {
        auto integer=[](const unsigned char* p) { return (static_cast<unsigned>(p[0])<<24)|(static_cast<unsigned>(p[1])<<16)|(p[2]<<8)|p[3]; };
        return bounded(integer(data.data()+16),integer(data.data()+20))?CoverFormat::PNG:CoverFormat::None;
    }
    // Readest's cover.png may contain an unchanged embedded JPEG. Inspect SOF
    // dimensions before decoding; never select the decoder from the filename.
    if(data[0]!=0xff || data[1]!=0xd8) return CoverFormat::None;
    at=2;
    while(at<data.size()) {
        if(data[at++]!=0xff) return CoverFormat::None;
        while(at<data.size() && data[at]==0xff) ++at;
        if(at>=data.size()) break;
        const auto marker=data[at++];
        if(marker==0xda || marker==0xd9 || marker==0) break;
        if(marker==1 || (marker>=0xd0 && marker<=0xd7)) continue;
        if(at+2>data.size()) break;
        const size_t length=(data[at]<<8)|data[at+1];
        if(length<2 || length>data.size()-at) break;
        if(marker>=0xc0 && marker<=0xcf && marker!=0xc4 && marker!=0xc8 && marker!=0xcc) {
            if(length<8) break;
            const unsigned h=(data[at+3]<<8)|data[at+4],w=(data[at+5]<<8)|data[at+6];
            return bounded(w,h)?CoverFormat::JPEG:CoverFormat::None;
        }
        at+=length;
    }
    return CoverFormat::None;
}
bool valid_cover(const std::string& path) { return cover_format(path)!=CoverFormat::None; }
bool cache_cover(Cloud& cloud,const std::string& root,const std::string& hash,const BookFiles& files,
                 const std::string& ca,long long now,DownloadTransport transfer) {
    if(files.cover_size<0 || files.cover_size>2*1024*1024) return false;
    const auto path=cover_path(root,cloud.session().user_id,hash,files);
    if(path.empty()) throw std::runtime_error("Invalid cover identity");
    if(valid_cover(path)) return true;
    // Readest resolves this canonical key even when the stored filename differs.
    const auto key=files.cover_key.empty()?cloud.session().user_id+"/Readest/Books/"+hash+"/cover.png":files.cover_key;
    auto resolved=cloud.get("/api/storage/download?fileKey="+encode(key),now);
    if(resolved.status==404) return false;
    if(resolved.status!=200) throw std::runtime_error("Cannot obtain cover download URL");
    auto body=parse_json(resolved.body);
    const auto url=string_member(body.get(),"downloadUrl");
    std::string pattern=path+".tmp-XXXXXX";
    std::vector<char> name(pattern.begin(),pattern.end()); name.push_back(0);
    int fd=mkstemp(name.data());
    if(fd<0) throw std::runtime_error("Cannot cache book cover");
    try {
        const size_t cap=files.cover_size?static_cast<size_t>(files.cover_size):2*1024*1024;
        auto response=transfer(url,fd,ca,cap);
        if(response.status==404) { close(fd); fd=-1; unlink(name.data()); return false; }
        if(response.status!=200 || response.bytes>cap || (files.cover_size && response.bytes!=static_cast<size_t>(files.cover_size)) || fsync(fd)!=0 || !valid_cover(name.data()))
            throw std::runtime_error("Cover image unavailable");
        close(fd); fd=-1;
        if(rename(name.data(),path.c_str())!=0) throw std::runtime_error("Cannot save book cover");
        return true;
    } catch(...) { if(fd>=0) close(fd); unlink(name.data()); throw; }
}
} // namespace readest
