#pragma once
#include <string>
#include <vector>
#include <atomic>

namespace readest {
// Worker-scoped cancellation; pointer must outlive every request on that thread.
void set_http_cancellation(const std::atomic<bool>* cancel);
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
} // namespace readest
