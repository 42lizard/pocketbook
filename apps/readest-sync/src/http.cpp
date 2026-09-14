#include "http.h"
#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <limits>
#include <cerrno>
#include <unistd.h>

#ifdef READEST_SIMULATOR_TRANSPORT
namespace readest::live {
#else
namespace readest {
#endif
namespace {
thread_local const std::atomic<bool>* cancellation = nullptr;
int progress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
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
bool controls(const std::string& value) {
    for (unsigned char c : value) if (c < 32 || c == 127) return true;
    return false;
}
}

void set_http_cancellation(const std::atomic<bool>* cancel) { cancellation = cancel; }
static HttpResponse transfer(const std::string& url, const std::string& method,
                           const std::vector<std::string>& headers,
                           const std::string& body, const std::string& ca_bundle,
                           size_t max_response, int fd) {
    if(cancellation && cancellation->load()) throw std::runtime_error("Cancelled.");
    const auto authority_end = url.find_first_of("/?#", 8);
    if (url.compare(0, 8, "https://") != 0 || controls(url) ||
        url.substr(8, authority_end == std::string::npos ? authority_end : authority_end - 8).find('@') != std::string::npos ||
        (method != "GET" && method != "POST") || (method == "GET" && !body.empty()) ||
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
    Buffer buffer; buffer.limit = max_response; buffer.fd = fd;
#define SET(option, value) do { if (curl_easy_setopt(curl.get(), option, value) != CURLE_OK) \
    throw std::runtime_error("Unsupported HTTPS option"); } while (0)
    SET(CURLOPT_URL, url.c_str());
    SET(CURLOPT_HTTPHEADER, list.get());
    SET(CURLOPT_NOSIGNAL, 1L);
    SET(CURLOPT_NOPROGRESS, 0L);
    SET(CURLOPT_XFERINFOFUNCTION, progress);
    SET(CURLOPT_CONNECTTIMEOUT, 15L);
    SET(CURLOPT_TIMEOUT, fd < 0 ? 60L : 1800L);
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
} // namespace readest
