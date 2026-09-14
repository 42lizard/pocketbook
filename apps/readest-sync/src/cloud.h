#pragma once
#include "http.h"
#include <functional>
#include <string>

namespace readest {
struct Session {
    std::string access_token, refresh_token, user_id;
    long long expires_at = 0;
    bool signed_in() const { return !refresh_token.empty(); }
};

// Sequential worker-thread client; never call concurrently. The session path
// must be app-owned. Rotated tokens are committed before subsequent API calls.
class Cloud {
public:
    using Transport = std::function<HttpResponse(const std::string&, const std::string&,
        const std::vector<std::string>&, const std::string&, const std::string&, size_t)>;
    Cloud(std::string session_path, std::string ca_bundle, std::string public_key,
          std::string auth_origin = "https://readest.supabase.co",
          std::string api_origin = "https://web.readest.com",
          Transport transport = https_request);
    void load_session();
    void sign_in(const std::string& email, const std::string& password, long long now);
    void refresh(long long now);
    void sign_out(); // Local logout; never revokes the user's other clients.
    HttpResponse get(const std::string& api_path, long long now);
    HttpResponse post(const std::string& api_path, const std::string& json, long long now);
    const Session& session() const { return session_; }
private:
    std::string path_, ca_, key_, auth_, api_;
    Transport transport_;
    Session session_;
    void commit(const Session& next);
    Session token_request(const std::string& grant, const std::string& body, long long now);
    HttpResponse request(const std::string& method, const std::string& path,
                         const std::string& body, long long now);
};
} // namespace readest
