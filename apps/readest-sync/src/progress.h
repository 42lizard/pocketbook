#pragma once
#include "state.h"
namespace readest {
// Display only: use the newest reported Readest page tuple, never infer a CFI.
// A negative result means no usable percentage is available.
double reading_percentage(const std::string& library_json, const std::string& config_json = "");
struct RemoteProgress {
    std::string config, location, xpointer;
    long long updated_at = 0;
    bool exists = false;
};
RemoteProgress parse_progress(const std::string& response, const std::string& user,
                               const std::string& hash);
RemoteProgress fetch_progress(Cloud& cloud, const std::string& hash, long long now);
enum class ProgressChoice { Automatic, PocketBook, Readest };
// Runs on the sequential worker after a successful native position capture.
// An empty native CFI means no saved position, never a failed capture.
// Incoming positions are staged; only the explicit Open action may apply them.
SyncAction sync_managed(Cloud& cloud, State& state, const ManagedBook& book,
                        const std::string& native_cfi, long long now,
                        ProgressChoice choice = ProgressChoice::Automatic,
                        long long displayed_revision = 0,
                        const std::string& native_progress = "");
} // namespace readest
