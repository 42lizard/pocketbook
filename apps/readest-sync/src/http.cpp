#include "http.h"
#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <limits>
#include <cerrno>
#include <unistd.h>
#include <sys/stat.h>

namespace readest {
namespace {
int progress(void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* cancellation=static_cast<const std::atomic<bool>*>(context);
    return cancellation && cancellation->load() ? 1 : 0;
}
struct Buffer { std::string data; size_t limit; size_t bytes = 0; int fd = -1; };
size_t receive(char* data, size_t size, size_t count, void* user) {
    auto& buffer = *static_cast<Buffer*>(user);
    if (size && count > std::numeric_limits<size_t>::max() / size) return 0;
    size_t bytes = size * count;
    if (bytes > buffer.limit - buffer.bytes) return 0;
    if (buffer.fd < 0) {
        try { buffer.data.append(data, bytes); }
        catch (...) { return 0; } // Never unwind through libcurl's C callback.
    } else {
        size_t at = 0;
        while (at < bytes) {
            auto n = write(buffer.fd, data + at, bytes - at);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return 0;
            at += static_cast<size_t>(n);
        }
    }
    buffer.bytes += bytes;
    return bytes;
}
struct UploadSource { int fd; size_t remaining; };
size_t send_bytes(char* data,size_t size,size_t count,void* context) {
    auto& source=*static_cast<UploadSource*>(context);
    if(size && count>std::numeric_limits<size_t>::max()/size) return CURL_READFUNC_ABORT;
    const auto cap=std::min(size*count,source.remaining);
    if(!cap) return 0;
    ssize_t n; do { n=read(source.fd,data,cap); } while(n<0 && errno==EINTR);
    if(n<=0) return CURL_READFUNC_ABORT;
    source.remaining-=n; return static_cast<size_t>(n);
}
bool controls(const std::string& value) {
    for (unsigned char c : value) if (c < 32 || c == 127) return true;
    return false;
}
}

static HttpResponse transfer(const std::string& url, const std::string& method,
                           const std::vector<std::string>& headers,
                           const std::string& body, const std::string& ca_bundle,
                           size_t max_response, int fd, const std::atomic<bool>* cancellation=nullptr, int upload_fd=-1, size_t upload_size=0) {
    if(cancellation && cancellation->load()) throw std::runtime_error("Cancelled.");
    const auto authority_end = url.find_first_of("/?#", 8);
    if (url.compare(0, 8, "https://") != 0 || controls(url) ||
        url.substr(8, authority_end == std::string::npos ? authority_end : authority_end - 8).find('@') != std::string::npos ||
        (method != "GET" && method != "POST" && !(method=="PUT" && upload_fd>=0)) || (method == "GET" && !body.empty()) ||
        ca_bundle.empty() || controls(ca_bundle) || max_response == 0 ||
        max_response > (fd < 0 ? 16UL * 1024 * 1024 : 1024UL * 1024 * 1024))
        throw std::runtime_error("Invalid HTTPS request");
    for (const auto& header : headers)
        if (controls(header) || header.find(':') == std::string::npos)
            throw std::runtime_error("Invalid HTTP header");
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw std::runtime_error("Cannot initialize HTTPS");
    });
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("Cannot allocate HTTPS request");
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> list(nullptr, curl_slist_free_all);
    for (const auto& header : headers) {
        auto* next = curl_slist_append(list.get(), header.c_str());
        if (!next) throw std::runtime_error("Cannot allocate HTTP headers");
        list.release(); list.reset(next);
    }
    UploadSource source{upload_fd,upload_size};
    Buffer buffer; buffer.limit = max_response; buffer.fd = fd;
#define SET(option, value) do { if (curl_easy_setopt(curl.get(), option, value) != CURLE_OK) \
    throw std::runtime_error("Unsupported HTTPS option"); } while (0)
    SET(CURLOPT_URL, url.c_str());
    SET(CURLOPT_HTTPHEADER, list.get());
    SET(CURLOPT_NOSIGNAL, 1L);
    SET(CURLOPT_NOPROGRESS, 0L);
    SET(CURLOPT_XFERINFOFUNCTION, progress);
    SET(CURLOPT_XFERINFODATA, cancellation);
    SET(CURLOPT_CONNECTTIMEOUT, 15L);
    SET(CURLOPT_TIMEOUT, fd < 0 && upload_fd<0 ? 60L : 1800L);
    SET(CURLOPT_LOW_SPEED_LIMIT, 16L);
    SET(CURLOPT_LOW_SPEED_TIME, 60L);
    SET(CURLOPT_SSL_VERIFYPEER, 1L);
    SET(CURLOPT_SSL_VERIFYHOST, 2L);
    SET(CURLOPT_CAINFO, ca_bundle.c_str());
    SET(CURLOPT_FOLLOWLOCATION, 0L);
#if LIBCURL_VERSION_NUM >= 0x075500
    SET(CURLOPT_PROTOCOLS_STR, "https");
#else
    SET(CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
#endif
    SET(CURLOPT_WRITEFUNCTION, receive);
    SET(CURLOPT_WRITEDATA, &buffer);
    SET(CURLOPT_USERAGENT, "PocketBook-Readest-Sync/0.1");
    if (method == "POST") {
        SET(CURLOPT_POST, 1L);
        SET(CURLOPT_POSTFIELDS, body.c_str());
        SET(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    }
    if(upload_fd>=0) {
        SET(CURLOPT_UPLOAD,1L);
        SET(CURLOPT_READFUNCTION,send_bytes);
        SET(CURLOPT_READDATA,&source);
        SET(CURLOPT_INFILESIZE_LARGE,static_cast<curl_off_t>(upload_size));
    }
#undef SET
    const auto result = curl_easy_perform(curl.get());
    if (result != CURLE_OK) {
        if(result==CURLE_ABORTED_BY_CALLBACK) throw std::runtime_error("Cancelled.");
        std::string message="HTTPS transfer failed (curl "+std::to_string(static_cast<int>(result))+"): "+curl_easy_strerror(result);
        if(result==CURLE_COULDNT_RESOLVE_HOST || result==CURLE_COULDNT_RESOLVE_PROXY)
            message+=". DNS is unavailable. Check Wi-Fi and try again.";
        else if(result==CURLE_COULDNT_CONNECT || result==CURLE_OPERATION_TIMEDOUT)
            message+=". Check the connection and try again.";
        // Only library/version metadata: never log URLs, headers or credentials.
        const auto* version=curl_version_info(CURLVERSION_NOW);
        if(version) message+=" [curl "+std::string(version->version?version->version:"unknown")+
            "; resolver "+(version->ares?std::string("c-ares ")+version->ares:std::string("system"))+"]";
        throw std::runtime_error(message);
    }
    HttpResponse response;
    if (curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status) != CURLE_OK)
        throw std::runtime_error("Missing HTTP response status");
    response.body.swap(buffer.data);
    response.bytes = buffer.bytes;
    return response;
}
HttpResponse https_request(const std::string& url, const std::string& method,
                           const std::vector<std::string>& headers,
                           const std::string& body, const std::string& ca_bundle,
                           size_t max_response) {
    return transfer(url, method, headers, body, ca_bundle, max_response, -1);
}
HttpResponse https_download(const std::string& url, int fd, const std::string& ca, size_t max_bytes) {
    if (fd < 0) throw std::runtime_error("Invalid download file");
    return transfer(url, "GET", {}, "", ca, max_bytes, fd);
}
HttpTransport https_transport() {
    HttpTransport transport;
    transport.request=[](const std::string& url,const std::string& method,const std::vector<std::string>& headers,
        const std::string& body,const std::string& ca,size_t cap,const std::atomic<bool>& cancel) {
        return transfer(url,method,headers,body,ca,cap,-1,&cancel);
    };
    transport.download=[](const std::string& url,int fd,const std::string& ca,size_t cap,const std::atomic<bool>& cancel) {
        if(fd<0) throw std::runtime_error("Invalid download file");
        return transfer(url,"GET",{},"",ca,cap,fd,&cancel);
    };
    transport.upload=[](const std::string& url,int fd,const std::string& ca,size_t size,const std::atomic<bool>& cancel) {
        struct stat st;
        if(fd<0 || fstat(fd,&st) || !S_ISREG(st.st_mode) || size==0 || size>256UL*1024*1024 || st.st_size!=static_cast<off_t>(size) || lseek(fd,0,SEEK_SET)<0)
            throw std::runtime_error("Invalid upload file");
        return transfer(url,"PUT",{"Content-Type: application/octet-stream"},"",ca,65536,-1,&cancel,fd,size);
    };
    return transport;
}
} // namespace readest
