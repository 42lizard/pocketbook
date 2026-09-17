#pragma once

#include <string>
#include <atomic>
#include <vector>
#include <map>

namespace readest {

// A deliberately limited point-CFI syntax check, not an EPUB resolver.
// Unsupported locations fail closed; no page/percentage fallback.
std::string point_cfi(const std::string& position);
// Readest stores visible ranges. Return the start, retaining assertions.
// Syntax-only: callers must still validate against the original EPUB.
std::string readest_start_cfi(const std::string& position);
// Structural ordering only, for positions in the same validated EPUB bytes.
// Throws for unsupported syntax; assertions are retained for resolution but
// are not part of CFI sorting. Returns -1, 0, or 1.
int compare_cfi(const std::string& left, const std::string& right);

struct NativeState {
    std::string report;
    std::string cfi;
    std::string status;
};

// Both arguments must be local database snapshots, never live firmware DBs.
NativeState inspect(const std::string& explorer, const std::string& books,
                    const std::string& book_path);

std::string read_text(const std::string& path);
void write_text(const std::string& path, const std::string& value);
void make_directory(const std::string& path);
// Copies the DB and any WAL/journal into an app-owned directory before queries.
void snapshot_database(const std::string& source, const std::string& destination);

// Explicit diagnostic write: fixed fixture identity and captured Readest BRAVO start only.
// backup_directory must be a new, app-owned trial directory.
void apply_readest_trial(const std::string& database, const std::string& backup_directory);

struct NativePosition {
    std::string book_path, book_id, profile_id, fast_hash, raw_position, timestamp, cfi;
    // Display page counts from the same snapshot as the CFI, never a location.
    std::string progress;
    bool indexed = false, has_settings = false;
};
// Read an app-owned snapshot after validating the managed EPUB bytes.
// Missing settings require a normal first Open before applying remote progress.
NativePosition native_position(const std::string& snapshot, const std::string& book_path);
// Display-only page ratios from a read-only snapshot; independent of CFI support.
// Missing or ambiguous profiles and invalid page counts are omitted.
std::map<std::string,double> native_percentages(const std::string& snapshot,
                                               const std::vector<std::string>& paths);
// Transactionally copy the live database to a new app-owned file; source is
// opened read-only and is never created. Unlike diagnostic filesystem copying,
// SQLite's backup API gives one consistent snapshot while WAL is active.
void backup_native_database(const std::string& source, const std::string& destination);
// Call only for an explicit Open on a validated managed EPUB. Requires the same
// state seen at reconciliation; retains a complete backup and touches two fields.
enum class NativeApplyStatus { Committed, Uncertain };
struct NativeApplyResult {
    NativeApplyStatus status=NativeApplyStatus::Committed;
    std::string warning;
};
// Pre-commit failures throw. Post-commit audit failure returns a warning;
// an unconfirmed commit/rollback returns Uncertain, never a false no-write claim.
// Cancellation is honored immediately before UPDATE; after mutation, finish commit.
[[nodiscard]] NativeApplyResult apply_native_position(const std::string& database, const std::string& backup_directory,
                           const NativePosition& expected, const std::string& readest_cfi,
                           const std::string& model, const std::string& firmware,
                           const std::atomic<bool>* cancel=nullptr);

} // namespace readest
