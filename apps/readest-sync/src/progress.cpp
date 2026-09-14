#include "progress.h"
#include "json_util.h"
#include "probe.h"
#include <algorithm>
#include <cmath>

namespace readest {
namespace {
void validate_hash(const std::string& hash) {
    if(hash.size()!=32 || hash.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::runtime_error("Invalid progress book identity");
}
void copy_string(json_object* out, json_object* row, const char* from, const char* to) {
    auto* value=member(row,from);
    if(value && json_object_get_type(value)!=json_type_string)
        throw std::runtime_error("Invalid remote progress field");
    json_object_object_add(out,to,value ? json_object_get(value) : nullptr);
}
bool same_content(const std::string& a, const std::string& b) {
    // Both configs are constructed in the same field order by our parser.
    auto left=parse_json(a), right=parse_json(b);
    json_object_object_del(left.get(),"updatedAt"); json_object_object_del(right.get(),"updatedAt");
    return json_text(left.get())==json_text(right.get());
}
}
double reading_percentage(const std::string& library_json, const std::string& config_json) {
    double result=-1;
    long long stamp=-1;
    for(const auto& source:{std::make_pair(library_json,"updated_at"),std::make_pair(config_json,"updatedAt")}) {
        if(source.first.empty()) continue;
        try {
            auto row=parse_json(source.first);
            const auto updated=record_timestamp(row.get(),source.second);
            if(updated<stamp) continue;
            auto* value=member(row.get(),"progress");
            Json decoded(nullptr,json_object_put);
            if(value && json_object_get_type(value)==json_type_string) {
                decoded=parse_json(json_object_get_string(value)); value=decoded.get();
            }
            double percentage=-1;
            if(value && json_object_get_type(value)==json_type_array && json_object_array_length(value)==2) {
                auto* current=json_object_array_get_idx(value,0);
                auto* total=json_object_array_get_idx(value,1);
                auto numeric=[](json_object* n) { return n && (json_object_get_type(n)==json_type_int || json_object_get_type(n)==json_type_double); };
                if(numeric(current) && numeric(total)) {
                    const double c=json_object_get_double(current),t=json_object_get_double(total);
                    if(std::isfinite(c) && std::isfinite(t) && c>=0 && t>0)
                        percentage=std::min(c/t,1.0)*100;
                }
            }
            result=percentage; stamp=updated;
        } catch(const std::exception&) { /* Bad display metadata must not block reading. */ }
    }
    return result;
}
RemoteProgress parse_progress(const std::string& response, const std::string& user, const std::string& hash) {
    validate_hash(hash);
    if(user.empty()) throw std::runtime_error("Missing progress account identity");
    auto json=parse_json(response); auto* rows=member(json.get(),"configs");
    if(!rows || json_object_get_type(rows)!=json_type_array || json_object_array_length(rows)>1)
        throw std::runtime_error("Invalid or ambiguous remote progress");
    RemoteProgress result;
    Json config(json_object_new_object(),json_object_put);
    json_object_object_add(config.get(),"bookHash",json_object_new_string(hash.c_str()));
    if(json_object_array_length(rows)==0) {
        json_object_object_add(config.get(),"updatedAt",json_object_new_int64(0));
        result.config=json_text(config.get()); return result;
    }
    auto* row=json_object_array_get_idx(rows,0);
    if(string_member(row,"user_id")!=user || string_member(row,"book_hash")!=hash || record_timestamp(row,"deleted_at")!=0)
        throw std::runtime_error("Remote progress identity or deletion mismatch");
    result.exists=true;
    for(const auto& key : {std::make_pair("meta_hash","metaHash"),std::make_pair("location","location"),std::make_pair("xpointer","xpointer")})
        copy_string(config.get(),row,key.first,key.second);
    result.location=member(row,"location")?string_member(row,"location"):"";
    result.xpointer=member(row,"xpointer")?string_member(row,"xpointer"):"";
    for(const auto& key : {std::make_pair("progress","progress"),std::make_pair("rsvp_position","rsvpPosition"),
                          std::make_pair("search_config","searchConfig"),std::make_pair("view_settings","viewSettings")}) {
        Json value(nullptr,json_object_put);
        if(member(row,key.first)) value=parse_json(string_member(row,key.first));
        json_object_object_add(config.get(),key.second,value.release());
    }
    result.updated_at=record_timestamp(row,"updated_at");
    if(result.updated_at<=0) throw std::runtime_error("Missing remote progress timestamp");
    json_object_object_add(config.get(),"updatedAt",json_object_new_int64(result.updated_at));
    result.config=json_text(config.get()); return result;
}
RemoteProgress fetch_progress(Cloud& cloud, const std::string& hash, long long now) {
    validate_hash(hash);
    auto response=cloud.get("/api/sync?type=configs&since=0&book="+hash,now);
    if(response.status!=200) throw std::runtime_error("Cannot fetch Readest reading progress");
    return parse_progress(response.body,cloud.session().user_id,hash);
}
SyncAction sync_managed(Cloud& cloud, State& state, const ManagedBook& book,
                        const std::string& local, long long now,
                        ProgressChoice choice, long long displayed_revision, const std::string& native_progress) {
    if(now<=0 || now>0x7fffffffffffffffLL/1000-1) throw std::runtime_error("Invalid device clock");
    const auto user=cloud.session().user_id;
    if(user.empty() || book.path.empty()) throw std::runtime_error("Download this book before syncing");
    const auto bytes=inspect_epub(book.path);
    if(bytes.readest_hash!=book.book.hash || bytes.sha256!=book.sha256 || bytes.size!=book.size)
        throw std::runtime_error("Local book bytes changed; synchronization stopped");
    auto saved=state.sync(user,book.book.hash);
    auto remote=fetch_progress(cloud,book.book.hash,now);
    if(remote.location.empty() && !remote.xpointer.empty()) {
        try { remote.location=xpointer_cfi(book.path,remote.xpointer); }
        catch(const std::exception&) { /* Unsupported retains the original config and baseline. */ }
    }
    const bool pending_current = !saved.pending_remote.empty() &&
        saved.pending_remote==readest_start_cfi(remote.location) &&
        saved.positions.local==local && saved.positions.remote==remote.location && saved.remote_config==remote.config;
    const bool decision_current = saved.revision==displayed_revision && saved.revision>0 &&
        saved.positions.local==local && saved.positions.remote==remote.location && saved.remote_config==remote.config;
    saved.positions.local=local; saved.positions.remote=remote.location;
    saved.remote_config=remote.config; saved.pending_remote.clear();
    auto action=(!remote.xpointer.empty() && remote.location.empty())?SyncAction::Unsupported:reconcile(saved.positions);
    // An explicit Readest choice remains valid for the next Open only while
    // both observations and the full remote config still match that choice.
    if(choice==ProgressChoice::Automatic && (action==SyncAction::Conflict || action==SyncAction::BackwardUnsupported) && pending_current)
        action=SyncAction::ApplyRemote;
    if(choice!=ProgressChoice::Automatic) {
        if(!decision_current) {
            state.save_sync(user,book.book.hash,saved); return SyncAction::Conflict;
        }
        if(action==SyncAction::Conflict || action==SyncAction::BackwardUnsupported) {
            const auto local_point=point_cfi(local), remote_point=readest_start_cfi(remote.location);
            if(choice==ProgressChoice::PocketBook) {
                if(local_point.empty()) action=SyncAction::Unsupported;
                else action=!remote_point.empty() && compare_cfi(local_point,remote_point)<0
                    ?SyncAction::BackwardUnsupported:SyncAction::Upload;
            } else {
                // Validate the selected side; the other side may have been cleared.
                action=remote_point.empty()?SyncAction::Unsupported:SyncAction::ApplyRemote;
            }
        }
    }
    if(action==SyncAction::Upload) {
        // Whole-row LWW has no CAS. Recheck immediately before the write and
        // verify after; no client can eliminate the remaining server-side race.
        auto fresh=fetch_progress(cloud,book.book.hash,now);
        if(fresh.config!=remote.config || fresh.exists!=remote.exists) {
            saved.positions.remote=fresh.location; saved.remote_config=fresh.config;
            state.save_sync(user,book.book.hash,saved); return SyncAction::Conflict;
        }
        // Persist observations before POST so an uncertain transport outcome
        // never advances the successful-sync baseline.
        state.save_sync(user,book.book.hash,saved); ++saved.revision;
        if(remote.updated_at>now*1000+5*60*1000)
            throw std::runtime_error("Readest timestamp is ahead; check the device clock");
        const auto stamp=std::max(now*1000,remote.updated_at+1);
        auto payload=progress_payload(remote.config,book.book.hash,local,stamp,native_progress);
        auto response=cloud.post("/api/sync","{\"books\":[],\"notes\":[],\"configs\":["+payload+"]}",now);
        if(response.status!=200) throw std::runtime_error("Readest progress upload failed; retry to check its outcome");
        auto verified=fetch_progress(cloud,book.book.hash,now);
        // An inserted row adds nullable server fields. Compare every preserved
        // field for existing configs; a new config has no previous fields.
        if(verified.location!=point_cfi(local) || !verified.xpointer.empty() ||
            (remote.exists && !same_content(verified.config,payload))) {
            saved.positions.remote=verified.location; saved.remote_config=verified.config;
            state.save_sync(user,book.book.hash,saved); return SyncAction::Conflict;
        }
        saved.positions.remote=verified.location; saved.remote_config=verified.config;
        saved.positions.last_local=local; saved.positions.last_remote=verified.location;
        saved.positions.has_baseline=true;
    } else if(action==SyncAction::ApplyRemote) {
        saved.pending_remote=readest_start_cfi(remote.location);
    } else if(action==SyncAction::EstablishBaseline) {
        saved.positions.last_local=local; saved.positions.last_remote=remote.location;
        saved.positions.has_baseline=true;
    }
    state.save_sync(user,book.book.hash,saved);
    return action;
}
} // namespace readest
