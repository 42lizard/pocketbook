#pragma once
#include "progress.h"
#include "probe.h"
#include <atomic>
#include <memory>

namespace readest {
struct ApplicationConfig {
    std::string root, books_root, database, ca, model, firmware;
    std::vector<std::string> book_roots;
    std::string public_key, auth_origin="https://readest.supabase.co", api_origin="https://web.readest.com";
    Cloud::Transport transport;
};
struct BookId {
    std::string account, hash;
    bool operator==(const BookId& other) const { return account==other.account && hash==other.hash; }
};
struct LibraryEntry {
    BookId id;
    ManagedBook book;
    SavedSync sync;
    Availability availability=Availability::Unknown;
    double local_percentage=-1, remote_percentage=-1;
    std::string cover;
};
struct LibrarySnapshot {
    bool initialized=false, signed_in=false;
    std::string account;
    std::vector<LibraryEntry> books;
};
enum class Command { Initialize, SignIn, SignOut, Refresh, Scan, Download, Sync, Open, ReadOffline, Resume };
struct Request {
    Command command=Command::Initialize;
    BookId book;
    std::string email, password;
    ProgressChoice choice=ProgressChoice::Automatic;
    long long revision=0;
};
enum class Outcome { Ready, SessionInvalid, SignedIn, SignedOut, Refreshed, Scanned, Downloaded, Reused,
    Synced, LocalOpen, SyncUnavailable, NeedsNativeSettings, Applied, Failed, Cancelled };
struct OperationResult {
    LibrarySnapshot library;
    Outcome outcome=Outcome::Ready;
    SyncAction sync_action=SyncAction::None;
    long long revision=0;
    int position_order=0;
    size_t matched=0;
    int covers=0, absent=0, failed_covers=0;
    std::string error, metadata_error, recovery_warning, open_path;
};
// Called exclusively by one sequential operation runner. No Qt or screen state.
class ApplicationService {
public:
    explicit ApplicationService(ApplicationConfig config);
    OperationResult execute(const Request& request, const std::atomic<bool>& cancel);
private:
    ApplicationConfig config_;
    std::unique_ptr<Cloud> cloud_;
    std::unique_ptr<State> state_;
    unsigned sequence_=0;
    void check_cancel(const std::atomic<bool>& cancel) const;
    size_t scan(const std::atomic<bool>& cancel);
    LibrarySnapshot snapshot();
    NativePosition capture(const std::string& path);
    ManagedBook resolve(const BookId& id);
    void synchronize(const Request& request, OperationResult& result, const std::atomic<bool>& cancel);
};
}
