#pragma once
#include "download.h"
#include "sync.h"
#include <sqlite3.h>

namespace readest {
bool missing_download(const std::string& path);
struct ManagedBook {
    LibraryBook book;
    std::string path, sha256;
    long long size = 0;
    int epubs=-1; // -1 means not checked.
    BookFiles files;
};
struct SavedSync {
    SyncPositions positions;
    std::string pending_remote, remote_config;
    long long revision = 0;
};
// App-owned SQLite database only. All records are partitioned by account.
class State {
public:
    explicit State(const std::string& path);
    ~State();
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    long long cursor(const std::string& user);
    void apply_page(const std::string& user, long long since, const LibraryPage& page);
    std::vector<ManagedBook> books(const std::string& user);
    void save_book_files(const std::string& user,const std::map<std::string,BookFiles>& files);
    void register_download(const std::string& user, const std::string& hash, const StoredBook& book);
    SavedSync sync(const std::string& user, const std::string& hash);
    void save_sync(const std::string& user, const std::string& hash, const SavedSync& next);
private:
    sqlite3* db_ = nullptr;
};
// Recover completed installations missed by an interrupted state update.
// Unknown directories and incomplete attempts are left untouched. Returns
// warnings for invalid managed metadata; these must be surfaced by the UI.
std::vector<std::string> recover_downloads(State& state, const std::string& user,
                                          const std::string& root);
} // namespace readest
