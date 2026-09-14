#include "probe.h"
#include "sync.h"
#include <cassert>
#include <json-c/json.h>
#include <stdexcept>

void sync_checks() {
    using namespace readest;
    const std::string alpha = "epubcfi(/6/2!/4/2/1:0)";
    const std::string bravo = "epubcfi(/6/4[bravo]!/4/2)";
    const std::string charlie = "epubcfi(/6/6!/4/2/1)";
    const std::string range = "epubcfi(/6/4[bravo]!/4,/2,/12[BRAVO-05]/1:179)";
    assert(readest_start_cfi(range) == bravo);
    assert(readest_start_cfi(charlie) == charlie);
    assert(compare_cfi(alpha, bravo) < 0);
    assert(compare_cfi(charlie, bravo) > 0);
    assert(compare_cfi("epubcfi(/6/6[x]!/4/2/1)", charlie) == 0);
    assert(compare_cfi("epubcfi(/6/6!/4/2/1:10)", "epubcfi(/6/6!/4/2/1:9)") > 0);
    assert(readest_start_cfi("epubcfi(/6/4[a^,b]!/4/2/1,:3,:9)") == "epubcfi(/6/4[a^,b]!/4/2/1:3)");
    assert(readest_start_cfi("epubcfi(/6/4!/4,/2/1:3[ab,cd],/2/1:9)") == "epubcfi(/6/4!/4/2/1:3[ab,cd])");
    for (const auto& bad : {"", "epubcfi(/6/4!/4,,/2)", "epubcfi(/6/4!/4,/2,)",
         "epubcfi(/6/4!/4,/2,/4,/6)", "epubcfi(/6/4!/4,/2,/4:-1)",
         "epubcfi(/6/4!/4,/2,/4)junk", "epubcfi(/6/4!/4,/2/1:0[;s=b],/4)",
         "epubcfi(/6/4!/4,/2,/4~1)", "epubcfi(/6/4!/4,/2,/4[broken)",
         "epubcfi(/6/4!/4:1,/2,/4)", "epubcfi(/6/4!/4,/4,/2)"}) assert(readest_start_cfi(bad).empty());

    SyncPositions p;
    p.local = alpha; p.remote = range;
    assert(reconcile(p) == SyncAction::Conflict);
    p.has_baseline = true; p.last_local = alpha; p.last_remote = range;
    assert(reconcile(p) == SyncAction::None);
    p.local = charlie;
    assert(reconcile(p) == SyncAction::Upload);
    p.remote = charlie;
    assert(reconcile(p) == SyncAction::EstablishBaseline);
    p.local = alpha;
    assert(reconcile(p) == SyncAction::ApplyRemote);
    p.local = bravo;
    assert(reconcile(p) == SyncAction::Conflict);
    p.local = charlie; p.last_local = charlie; p.last_remote = charlie; p.remote = bravo;
    assert(reconcile(p) == SyncAction::BackwardUnsupported);
    p.local = alpha; p.last_local = bravo; p.remote = charlie;
    assert(reconcile(p) == SyncAction::BackwardUnsupported);
    p.local = "invalid";
    assert(reconcile(p) == SyncAction::Unsupported);
    p.local = "";
    assert(reconcile(p) == SyncAction::Conflict);
    p.has_baseline = false;
    assert(reconcile(p) == SyncAction::ApplyRemote);
    p.local = charlie; p.remote = "";
    assert(reconcile(p) == SyncAction::Upload);

    // Optional assertions must not invent a position change on either side.
    const std::string plain = "epubcfi(/6/4!/4/2/1:12)";
    const std::string asserted = "epubcfi(/6/4[chapter]!/4/2/1:12)";
    const std::string later = "epubcfi(/6/4!/4/2/1:13)";
    for (const auto& equivalent : {asserted,
            std::string("epubcfi(/6/4[chapter]!/4/2/1:12[before,after])"),
            std::string("epubcfi(/6/4[chapter]!/4,/2/1:12,/2/1:20)")}) {
        SyncPositions same;
        same.local = plain; same.remote = equivalent;
        assert(reconcile(same) == SyncAction::EstablishBaseline);
        same.has_baseline = true; same.last_local = asserted; same.last_remote = plain;
        assert(reconcile(same) == SyncAction::None);
        assert(same.local == plain && same.remote == equivalent);
        assert(same.last_local == asserted && same.last_remote == plain);
        same.local = later;
        assert(reconcile(same) == SyncAction::Upload);
        same.local = equivalent; same.remote = later;
        assert(reconcile(same) == SyncAction::ApplyRemote);
    }
    SyncPositions different;
    different.has_baseline = true;
    different.last_local = plain; different.last_remote = later;
    different.local = asserted; different.remote = "epubcfi(/6/4[chapter]!/4/2/1:13)";
    assert(reconcile(different) == SyncAction::None);
    different.local = "epubcfi(/6/4!/4/2/1:14)";
    different.remote = "epubcfi(/6/4!/4/2/1:15)";
    assert(reconcile(different) == SyncAction::Conflict);
    different.local.clear(); different.remote = later;
    assert(reconcile(different) == SyncAction::Conflict);
    different.last_local = "invalid";
    assert(reconcile(different) == SyncAction::Unsupported);
    different.local.clear(); different.remote.clear();
    assert(reconcile(different) == SyncAction::EstablishBaseline);

    const std::string config = R"({"bookHash":"fixture","metaHash":"same-bytes-only","updatedAt":123,"progress":[15,44],"location":"old","xpointer":"stale","viewSettings":{"fontSize":22},"unknown":{"keep":[true,null,7]},"booknotes":[{"note":"keep"}]})";
    const auto output = progress_payload(config, "fixture", charlie, 124);
    json_object* result = json_tokener_parse(output.c_str());
    json_object* original = json_tokener_parse(config.c_str());
    json_object* value = nullptr;
    json_object_object_get_ex(result, "location", &value);
    assert(charlie == json_object_get_string(value));
    json_object_object_get_ex(result, "xpointer", &value);
    assert(std::string(json_object_get_string(value)).empty());
    for (const auto& key : {"bookHash", "metaHash", "progress", "viewSettings", "unknown", "booknotes"}) {
        json_object *a = nullptr, *b = nullptr;
        json_object_object_get_ex(result, key, &a);
        json_object_object_get_ex(original, key, &b);
        assert(std::string(json_object_to_json_string(a)) == json_object_to_json_string(b));
    }
    json_object_put(result); json_object_put(original);
    for(const auto& bad : {"[1,0]", "[-1,10]", "[11,10]", "[1.5,10]", "[1]", "{}", "[null,10]"}) {
        bool rejected=false;
        try { progress_payload(config,"fixture",charlie,124,bad); }
        catch(const std::runtime_error&) { rejected=true; }
        assert(rejected);
    }
    for (const auto& bad : {"{}", "[]", "{", "{\"bookHash\":5}"}) {
        bool failed = false;
        try { progress_payload(bad, "fixture", charlie, 124); }
        catch (const std::runtime_error&) { failed = true; }
        assert(failed);
    }
    bool failed = false;
    try { progress_payload(config + " trailing", "fixture", charlie, 124); }
    catch (const std::runtime_error&) { failed = true; }
    assert(failed);
    failed = false;
    try { progress_payload(config, "fixture", charlie, 122); }
    catch (const std::runtime_error&) { failed = true; }
    assert(failed);
}
