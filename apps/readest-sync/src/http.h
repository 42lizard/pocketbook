#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <functional>

namespace readest {
struct HttpResponse { long status = 0; std::string body; size_t bytes = 0; };
// HTTPS only. Redirects are returned to the caller, never followed with auth.
// Call from a worker thread. Errors do not include URLs, headers, or bodies.
HttpResponse https_request(const std::string& url, const std::string& method,
                           const std::vector<std::string>& headers,
                           const std::string& body, const std::string& ca_bundle,
                           size_t max_response = 4 * 1024 * 1024);
// Streams to an already-open, new temporary file. No authorization headers.
// Caller owns validation, fsync, close, and final installation/removal.
HttpResponse https_download(const std::string& url, int fd, const std::string& ca_bundle,
                            size_t max_bytes);
// Both callbacks select one adapter. Tokens are borrowed only for synchronous calls.
struct HttpTransport {
    using Request = std::function<HttpResponse(const std::string&, const std::string&,
        const std::vector<std::string>&, const std::string&, const std::string&, size_t,
        const std::atomic<bool>&)>;
    using Download = std::function<HttpResponse(const std::string&, int, const std::string&, size_t,
        const std::atomic<bool>&)>;
    Request request;
    Download download;
    // Streams exactly size bytes from an open regular file; no bearer token.
    Download upload;
    std::function<HttpResponse(const std::string&, const std::string&, const std::vector<std::string>&,
        const std::string&, const std::string&, size_t)> bind_request(const std::atomic<bool>& cancel) const {
        auto call=request;
        return [call,&cancel](const std::string& url,const std::string& method,const std::vector<std::string>& headers,
            const std::string& body,const std::string& ca,size_t cap) { return call(url,method,headers,body,ca,cap,cancel); };
    }
    std::function<HttpResponse(const std::string&, int, const std::string&, size_t)>
    bind_download(const std::atomic<bool>& cancel) const {
        auto call=download;
        return [call,&cancel](const std::string& url,int fd,const std::string& ca,size_t cap) {
            return call(url,fd,ca,cap,cancel);
        };
    }
};
HttpTransport https_transport();
} // namespace readest
