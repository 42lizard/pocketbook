#pragma once
#include "state.h"
#include "probe.h"
#include <atomic>
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
// Operation-scoped proof of full EPUB validation, tied to an immutable book copy.
// Construct afresh for each operation; never cache across reader handoffs.
class VerifiedManagedBook {
    ManagedBook book_;
public:
    explicit VerifiedManagedBook(const ManagedBook& book);
    const ManagedBook& book() const { return book_; }
};
SyncAction sync_managed(Cloud& cloud, State& state, const VerifiedManagedBook& verified,
                        const std::string& native_cfi, long long now,
                        ProgressChoice choice = ProgressChoice::Automatic,
                        long long displayed_revision = 0,
                        const std::string& native_progress = "");
// Runs on the sequential worker after a successful native position capture.
// An empty native CFI means no saved position, never a failed capture.
// Incoming positions are staged; only the explicit Open action may apply them.
SyncAction sync_managed(Cloud& cloud, State& state, const ManagedBook& book,
                        const std::string& native_cfi, long long now,
                        ProgressChoice choice = ProgressChoice::Automatic,
                        long long displayed_revision = 0,
                        const std::string& native_progress = "");
struct NativeResumeContext {
    std::string database, audit_directory, model, firmware;
};
enum class ResumeOutcome { NotRequested, NoPending, Blocked, NeedsNativeSettings, Applied, SyncUnavailable, AppliedUnrecorded, CommitUncertain };
struct ProgressTransition {
    SyncAction action=SyncAction::None;
    ResumeOutcome outcome=ResumeOutcome::NotRequested;
    long long revision=0;
    int position_order=0;
    std::string error, warning;
};
// Reconcile fresh remote/native observations on the sequential worker. Only an
// explicit Open applies the staged target. Reuse the operation's verified book.
ProgressTransition transition_progress(Cloud& cloud,State& state,const VerifiedManagedBook& verified,
    const NativePosition& native,long long now,ProgressChoice choice,long long displayed_revision,
    bool open,const NativeResumeContext& context,const std::atomic<bool>& cancel);
} // namespace readest
