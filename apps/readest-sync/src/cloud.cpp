#include "cloud.h"
#include "json_util.h"
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <utility>
#include <vector>
#include <cstdlib>
#include <cstdio>

namespace readest {
namespace {
bool safe_token(const std::string& s) {
    if (s.empty() || s.size() > 16384) return false;
    for (unsigned char c : s) if (c <= 32 || c >= 127) return false;
    return true;
}
void validate(const Session& s) {
    if (!safe_token(s.access_token) || !safe_token(s.refresh_token) ||
        !safe_token(s.user_id) || s.expires_at <= 0)
        throw std::runtime_error("Invalid authentication session");
}
void check_origin(const std::string& s) {
    if (s.compare(0, 8, "https://") || s.size() <= 8 ||
        s.find_first_of("/@?#\\", 8) != std::string::npos || !safe_token(s))
        throw std::runtime_error("Invalid cloud origin");
}
Json object() { return Json(json_object_new_object(), json_object_put); }
void put(json_object* o, const char* k, const std::string& v) {
    json_object_object_add(o, k, json_object_new_string_len(v.data(), static_cast<int>(v.size())));
}
}

Cloud::Cloud(std::string path, std::string ca, std::string key,
             std::string auth, std::string api, Transport transport)
    : path_(std::move(path)), ca_(std::move(ca)), key_(std::move(key)),
      auth_(std::move(auth)), api_(std::move(api)), transport_(std::move(transport)) {
    check_origin(auth_); check_origin(api_);
    if (path_.empty() || path_.find('\0') != std::string::npos || !safe_token(key_))
        throw std::runtime_error("Missing cloud configuration");
}

void Cloud::load_session() {
    session_=Session(); // Failed reload must not retain stale credentials.
    int fd = open(path_.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        if (errno == ENOENT) { session_ = Session(); return; }
        throw std::runtime_error("Cannot read saved session");
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > 65536) {
        close(fd); throw std::runtime_error("Unsafe saved session file");
    }
    std::string data;
    char buffer[4096];
    for (;;) {
        auto n = read(fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 || data.size() + (n > 0 ? static_cast<size_t>(n) : 0) > 65536) {
            close(fd); throw std::runtime_error("Cannot read saved session");
        }
        if (!n) break;
        data.append(buffer, static_cast<size_t>(n));
    }
    close(fd);
    auto json = parse_json(data);
    if (json_object_get_type(json.get()) != json_type_object)
        throw std::runtime_error("Invalid saved session");
    if (json_object_object_length(json.get()) == 0) { session_ = Session(); return; }
    Session next;
    next.access_token = string_member(json.get(), "access_token");
    next.refresh_token = string_member(json.get(), "refresh_token");
    next.user_id = string_member(json.get(), "user_id");
    next.expires_at = integer_member(json.get(), "expires_at");
    validate(next); session_ = next;
}

void Cloud::commit(const Session& next) {
    auto json = object();
    if (next.signed_in()) {
        validate(next);
        put(json.get(), "access_token", next.access_token);
        put(json.get(), "refresh_token", next.refresh_token);
        put(json.get(), "user_id", next.user_id);
        json_object_object_add(json.get(), "expires_at", json_object_new_int64(next.expires_at));
    }
    const auto data = json_text(json.get());
    struct stat st;
    if (lstat(path_.c_str(), &st) == 0 && !S_ISREG(st.st_mode))
        throw std::runtime_error("Unsafe saved session path");
    std::string name = path_ + ".tmp-XXXXXX";
    std::vector<char> temp(name.begin(), name.end()); temp.push_back(0);
    int fd = mkstemp(temp.data()); // Exclusive creation, mode 0600.
    if (fd < 0) throw std::runtime_error("Cannot create saved session");
    try {
        size_t at = 0;
        while (at < data.size()) {
            auto n = write(fd, data.data() + at, data.size() - at);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("Cannot write saved session");
            at += static_cast<size_t>(n);
        }
        if (fsync(fd) != 0) throw std::runtime_error("Cannot flush saved session");
        if (close(fd) != 0) { fd = -1; throw std::runtime_error("Cannot close saved session"); }
        fd = -1;
        if (rename(temp.data(), path_.c_str()) != 0) throw std::runtime_error("Cannot commit saved session");
        session_ = next;
        auto slash = path_.find_last_of('/');
        const auto parent = slash == std::string::npos ? "." : slash == 0 ? "/" : path_.substr(0, slash);
        int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
        if (directory < 0) throw std::runtime_error("Session committed but directory cannot be opened");
        int flushed = fsync(directory);
        int error = errno;
        close(directory);
        // FAT may not support directory fsync; the session file was flushed.
        if (flushed != 0 && error != EINVAL && error != ENOTSUP)
            throw std::runtime_error("Session committed but directory flush failed");
    } catch (...) {
        if (fd >= 0) close(fd);
        unlink(temp.data());
        throw;
    }
}

Session Cloud::token_request(const std::string& grant, const std::string& body, long long now) {
    if (now <= 0) throw std::runtime_error("Set the device clock before signing in");
    const auto response = transport_(auth_ + "/auth/v1/token?grant_type=" + grant, "POST",
        {"apikey: " + key_, "Content-Type: application/json", "Accept: application/json"}, body, ca_, 65536);
    if (response.status != 200)
        throw std::runtime_error("Authentication failed (HTTP " + std::to_string(response.status) + ")");
    auto json = parse_json(response.body);
    Session next;
    next.access_token = string_member(json.get(), "access_token");
    next.refresh_token = string_member(json.get(), "refresh_token");
    next.user_id = string_member(member(json.get(), "user"), "id");
    next.expires_at = integer_member(json.get(), "expires_at");
    validate(next);
    if (next.expires_at <= now + 30) throw std::runtime_error("Authentication session already expired; check device clock");
    return next;
}

void Cloud::sign_in(const std::string& email, const std::string& password, long long now) {
    if (email.empty() || email.size() > 320 || password.empty() || password.size() > 4096)
        throw std::runtime_error("Enter email and password");
    auto body = object(); put(body.get(), "email", email); put(body.get(), "password", password);
    commit(token_request("password", json_text(body.get()), now));
}

void Cloud::refresh(long long now) {
    if (!session_.signed_in()) throw std::runtime_error("Sign in to Readest first");
    auto body = object(); put(body.get(), "refresh_token", session_.refresh_token);
    auto next = token_request("refresh_token", json_text(body.get()), now);
    if (next.user_id != session_.user_id) throw std::runtime_error("Refreshed session identity mismatch");
    commit(next);
}

void Cloud::sign_out() { commit(Session()); }

HttpResponse Cloud::request(const std::string& method, const std::string& path,
                            const std::string& body, long long now) {
    // Paths are relative to our fixed API origin; never attach auth to a signed
    // object-storage URL. Those downloads use the bare HTTPS transport.
    if (path.compare(0, 5, "/api/") || path.find_first_of("\\\r\n#") != std::string::npos)
        throw std::runtime_error("Invalid Readest API path");
    if (!session_.signed_in()) throw std::runtime_error("Sign in to Readest first");
    if (session_.expires_at <= now + 60) refresh(now);
    auto send = [&] {
        return transport_(api_ + path, method,
            {"Authorization: Bearer " + session_.access_token, "Content-Type: application/json", "Accept: application/json"},
            body, ca_, 4 * 1024 * 1024);
    };
    auto response = send();
    if (response.status == 401) { refresh(now); response = send(); }
    return response;
}
HttpResponse Cloud::get(const std::string& path, long long now) { return request("GET", path, "", now); }
HttpResponse Cloud::post(const std::string& path, const std::string& json, long long now) {
    parse_json(json);
    return request("POST", path, json, now);
}
} // namespace readest
