#pragma once
#include <string>

namespace readest {
enum class SyncAction { None, EstablishBaseline, Upload, ApplyRemote, Conflict, Unsupported, BackwardUnsupported };

// Empty means no saved location, not a failed read. Callers must surface
// capture/network errors before reconciliation and retain their last baseline.
struct SyncPositions {
    std::string local, remote;
    std::string last_local, last_remote;
    bool has_baseline = false;
};

SyncAction reconcile(const SyncPositions& positions);

// Copy the complete remote config and change only progress-related fields.
// Throws on invalid input; no network or filesystem effects.
std::string progress_payload(const std::string& remote_config,
                             const std::string& book_hash,
                             const std::string& local_cfi,
                             long long updated_at,
                             const std::string& native_progress = "");
} // namespace readest
