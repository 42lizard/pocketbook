#include "sync.h"
#include "probe.h"
#include "json_util.h"
#include <stdexcept>

namespace readest {
namespace {
std::string normalize(const std::string& value) {
    const auto point = point_cfi(value);
    return point.empty() ? readest_start_cfi(value) : point;
}
bool same_position(const std::string& left, const std::string& right) {
    return left == right || (!left.empty() && !right.empty() && compare_cfi(left, right) == 0);
}
}

SyncAction reconcile(const SyncPositions& p) {
    const auto local = normalize(p.local), remote = normalize(p.remote);
    if ((!p.local.empty() && local.empty()) || (!p.remote.empty() && remote.empty()))
        return SyncAction::Unsupported;
    const auto old_local = normalize(p.last_local), old_remote = normalize(p.last_remote);
    const bool valid_baseline = (p.last_local.empty() || !old_local.empty()) &&
                               (p.last_remote.empty() || !old_remote.empty());
    if (same_position(local, remote))
        return p.has_baseline && valid_baseline && same_position(local, old_local) && same_position(remote, old_remote)
            ? SyncAction::None : SyncAction::EstablishBaseline;
    if (!p.has_baseline) {
        if (local.empty()) return SyncAction::ApplyRemote;
        if (remote.empty()) return SyncAction::Upload;
        return SyncAction::Conflict;
    }
    if (!valid_baseline) return SyncAction::Unsupported;
    const bool local_changed = !same_position(local, old_local), remote_changed = !same_position(remote, old_remote);
    // A disappearing location is not an instruction to erase reading progress.
    if ((local_changed && local.empty()) || (remote_changed && remote.empty()))
        return SyncAction::Conflict;
    if (local_changed && remote_changed) return SyncAction::Conflict;
    if (local_changed) return !remote.empty() && compare_cfi(local, remote) < 0
        ? SyncAction::BackwardUnsupported : SyncAction::Upload;
    if (remote_changed) return !local.empty() && compare_cfi(remote, local) < 0
        ? SyncAction::BackwardUnsupported : SyncAction::ApplyRemote;
    return SyncAction::None;
}

std::string progress_payload(const std::string& input, const std::string& hash,
                             const std::string& cfi, long long updated_at) {
    if (hash.empty() || point_cfi(cfi).empty() || updated_at <= 0 || input.size() > 1024 * 1024)
        throw std::runtime_error("Invalid progress update");
    auto config = parse_json(input);
    if (json_object_get_type(config.get()) != json_type_object)
        throw std::runtime_error("Invalid remote config JSON");
    json_object* book = nullptr;
    if (!json_object_object_get_ex(config.get(), "bookHash", &book) ||
        json_object_get_type(book) != json_type_string || hash != json_object_get_string(book))
        throw std::runtime_error("Remote config book identity mismatch");
    json_object* old_time = nullptr;
    if (json_object_object_get_ex(config.get(), "updatedAt", &old_time) &&
        (json_object_get_type(old_time) != json_type_int || updated_at <= json_object_get_int64(old_time)))
        throw std::runtime_error("Progress timestamp is not newer than remote config");
    json_object_object_add(config.get(), "location", json_object_new_string(point_cfi(cfi).c_str()));
    // Explicitly clear the competing KOReader representation on the server.
    json_object_object_add(config.get(), "xpointer", json_object_new_string(""));
    json_object_object_add(config.get(), "updatedAt", json_object_new_int64(updated_at));
    // Native page counts do not map to Readest progress. Preserve that field;
    // Readest must resolve the new CFI to derive its own display percentage.
    return json_object_to_json_string_ext(config.get(), JSON_C_TO_STRING_PLAIN);
}
} // namespace readest
