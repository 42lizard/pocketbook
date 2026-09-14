#include "progress.h"
#include "json_util.h"
#include <cassert>
using namespace readest;
int main(int argc, char** argv) {
    assert(reading_percentage(R"({"progress":[15,60]})")==25);
    assert(reading_percentage(R"({"progress":"[0,60]"})")==0);
    assert(reading_percentage(R"({"progress":[70,60]})")==100);
    for(const auto& bad:{"{}", "bad", R"({"progress":[1,0]})", R"({"progress":[-1,60]})",
            R"({"progress":["15",60]})", R"({"progress":[15]})"})
        assert(reading_percentage(bad)<0);
    assert(reading_percentage(R"({"progress":[15,60],"updated_at":200})",
        R"({"progress":[30,60],"updatedAt":100})")==25);
    assert(reading_percentage(R"({"progress":[15,60],"updated_at":100})",
        R"({"progress":[30,60],"updatedAt":200})")==50);
    assert(reading_percentage(R"({"progress":[15,60],"updated_at":100})",
        R"({"progress":null,"updatedAt":200})")<0);
    assert(argc==3); const std::string root=argv[1], hash="81fbcb860e2eed5d223c359063680f87";
    const std::string alpha="epubcfi(/6/2!/4/2)", bravo="epubcfi(/6/4!/4/2)", charlie="epubcfi(/6/6!/4/2)";
    std::string location=alpha, xpointer, settings="{\"fontSize\":22}", posted;
    long long timestamp=1000000;
    int gets=0, posts=0, race=0; bool fail_post=false;
    auto wire=[&] {
        Json row(json_object_new_object(),json_object_put);
        for(const auto& field : {std::make_pair("user_id",std::string("user")), std::make_pair("book_hash",hash),
                std::make_pair("location",location),std::make_pair("xpointer",xpointer),std::make_pair("view_settings",settings),
                std::make_pair("search_config",std::string("{\"query\":\"keep me\"}")),std::make_pair("progress",std::string("[15,44]"))})
            json_object_object_add(row.get(),field.first,json_object_new_string(field.second.c_str()));
        json_object_object_add(row.get(),"updated_at",json_object_new_int64(timestamp));
        return "{\"configs\":["+json_text(row.get())+"]}";
    };
    auto api=[&](const std::string& url,const std::string& method,const std::vector<std::string>&,
                 const std::string& body,const std::string&,size_t) {
        HttpResponse r; r.status=200;
        if(url.find("grant_type=password")!=std::string::npos) {
            r.body=R"({"access_token":"dummy","refresh_token":"dummy-refresh","expires_at":99999,"user":{"id":"user"}})"; return r;
        }
        if(method=="GET") {
            assert(url=="https://api.test/api/sync?type=configs&since=0&book="+hash);
            ++gets; if((race==1 && gets==2)||(race==2 && gets==3)) { location=charlie; ++timestamp; }
            r.body=wire(); return r;
        }
        assert(url=="https://api.test/api/sync"); ++posts; posted=body;
        if(fail_post) { r.status=503; return r; }
        auto json=parse_json(body); auto* configs=member(json.get(),"configs");
        assert(json_object_array_length(member(json.get(),"books"))==0 && json_object_array_length(member(json.get(),"notes"))==0);
        auto* config=json_object_array_get_idx(configs,0);
        location=string_member(config,"location"); xpointer=string_member(config,"xpointer"); timestamp=integer_member(config,"updatedAt");
        assert(json_text(member(config,"viewSettings"))==settings);
        assert(json_text(member(config,"searchConfig"))=="{\"query\":\"keep me\"}");
        assert(json_text(member(config,"progress"))=="[15,44]");
        r.body="{}"; return r;
    };
    Cloud cloud(root+"/progress-session.json","ca","public","https://auth.test","https://api.test",api);
    cloud.sign_in("test","dummy",1000);
    ManagedBook book; book.book.hash=hash; book.path=argv[2];
    auto integrity=inspect_epub(book.path); book.sha256=integrity.sha256; book.size=integrity.size;
    State state(root+"/progress-state.db");
    assert(sync_managed(cloud,state,book,"",1001)==SyncAction::ApplyRemote);
    auto saved=state.sync("user",hash); assert(saved.pending_remote==alpha && !saved.positions.has_baseline && posts==0);
    assert(sync_managed(cloud,state,book,alpha,1002)==SyncAction::EstablishBaseline);
    assert(state.sync("user",hash).pending_remote.empty());
    xpointer="/stale/koreader";
    assert(sync_managed(cloud,state,book,bravo,1003)==SyncAction::Upload);
    assert(posts==1 && location==bravo && xpointer.empty());
    saved=state.sync("user",hash);
    assert(saved.positions.last_local==bravo && saved.positions.last_remote==bravo);
    assert(sync_managed(cloud,state,book,bravo,1004)==SyncAction::None);
    // Remote-only forward movement is persisted for Open, not applied here.
    location=charlie; ++timestamp;
    assert(sync_managed(cloud,state,book,bravo,1005)==SyncAction::ApplyRemote);
    assert(state.sync("user",hash).pending_remote==charlie);
    assert(sync_managed(cloud,state,book,alpha,1006)==SyncAction::Conflict);
    // Only XPointer is not an empty position that we may overwrite.
    location=""; xpointer="/body/DocFragment[3]";
    assert(sync_managed(cloud,state,book,bravo,1007)==SyncAction::Unsupported && posts==1);
    xpointer="/body[1]/DocFragment[3]/body[1]/h1";
    assert(sync_managed(cloud,state,book,bravo,1008)==SyncAction::ApplyRemote && posts==1);
    assert(state.sync("user",hash).pending_remote==charlie);
    for(int test=1;test<=3;++test) {
        State isolated(root+"/progress-"+std::to_string(test)+".db");
        SavedSync baseline; baseline.positions.has_baseline=true;
        baseline.positions.last_local=alpha; baseline.positions.last_remote=alpha;
        isolated.save_sync("user",hash,baseline);
        location=alpha; xpointer=""; timestamp=1000000; gets=posts=0; race=test; fail_post=test==3;
        if(test==3) {
            bool failed=false;
            try { sync_managed(cloud,isolated,book,bravo,1010); } catch(const std::runtime_error&) { failed=true; }
            assert(failed && posts==1);
        } else {
            assert(sync_managed(cloud,isolated,book,bravo,1010)==SyncAction::Conflict);
            assert(posts==(test==1?0:1));
        }
        assert(isolated.sync("user",hash).positions.last_local==alpha);
    }
    race=0; fail_post=false; location=bravo; xpointer=""; timestamp=1000000;
    State conflict(root+"/conflict-state.db");
    assert(sync_managed(cloud,conflict,book,charlie,1011)==SyncAction::Conflict);
    auto revision=conflict.sync("user",hash).revision;
    const auto before_posts=posts;
    assert(sync_managed(cloud,conflict,book,charlie,1012,ProgressChoice::PocketBook,revision-1)==SyncAction::Conflict);
    assert(posts==before_posts);
    revision=conflict.sync("user",hash).revision;
    assert(sync_managed(cloud,conflict,book,charlie,1013,ProgressChoice::Readest,revision)==SyncAction::ApplyRemote);
    assert(conflict.sync("user",hash).pending_remote==bravo && posts==before_posts);
    assert(sync_managed(cloud,conflict,book,charlie,1013)==SyncAction::ApplyRemote);
    revision=conflict.sync("user",hash).revision;
    assert(sync_managed(cloud,conflict,book,charlie,1014,ProgressChoice::PocketBook,revision)==SyncAction::Upload);
    State incoming(root+"/incoming-choice.db");
    location=charlie; timestamp=1014000;
    assert(sync_managed(cloud,incoming,book,bravo,1015)==SyncAction::Conflict);
    revision=incoming.sync("user",hash).revision;
    assert(sync_managed(cloud,incoming,book,bravo,1016,ProgressChoice::Readest,revision)==SyncAction::ApplyRemote);
    assert(sync_managed(cloud,incoming,book,bravo,1017)==SyncAction::ApplyRemote); // Subsequent Open honors the choice.
    settings="{\"fontSize\":24}";
    assert(sync_managed(cloud,incoming,book,bravo,1018)==SyncAction::Conflict); // Changed remote config invalidates it.
    // A remote-only backward move requires a choice, survives restart after
    // confirmation, and never uploads or advances the applied baseline here.
    location=alpha;
    {
        State backward(root+"/backward-choice.db");
        SavedSync base; base.positions.has_baseline=true;
        base.positions.last_local=bravo; base.positions.last_remote=bravo;
        backward.save_sync("user",hash,base);
        assert(sync_managed(cloud,backward,book,bravo,1019)==SyncAction::BackwardUnsupported);
        revision=backward.sync("user",hash).revision;
        assert(sync_managed(cloud,backward,book,bravo,1020,ProgressChoice::Readest,revision)==SyncAction::ApplyRemote);
        assert(backward.sync("user",hash).positions.last_local==bravo);
    }
    State reopened(root+"/backward-choice.db");
    int unchanged_posts=posts;
    assert(sync_managed(cloud,reopened,book,bravo,1021)==SyncAction::ApplyRemote);
    assert(reopened.sync("user",hash).pending_remote==alpha && posts==unchanged_posts);
    location=charlie; // Changed cloud position invalidates the displayed choice.
    assert(sync_managed(cloud,reopened,book,bravo,1022,ProgressChoice::Readest,revision)==SyncAction::Conflict);
    assert(reopened.sync("user",hash).pending_remote.empty() && posts==unchanged_posts);
    for(bool cleared_remote : {true,false}) {
        State cleared(root+(cleared_remote?"/cleared-remote.db":"/cleared-local.db"));
        SavedSync base; base.positions.has_baseline=true;
        base.positions.last_local=bravo; base.positions.last_remote=bravo;
        cleared.save_sync("user",hash,base);
        location=cleared_remote?"":bravo;
        const auto local=cleared_remote?bravo:"";
        assert(sync_managed(cloud,cleared,book,local,1023)==SyncAction::Conflict);
        revision=cleared.sync("user",hash).revision;
        // Selecting the empty side remains invalid.
        assert(sync_managed(cloud,cleared,book,local,1024,
            cleared_remote?ProgressChoice::Readest:ProgressChoice::PocketBook,revision)==SyncAction::Unsupported);
        revision=cleared.sync("user",hash).revision;
        assert(sync_managed(cloud,cleared,book,local,1025,
            cleared_remote?ProgressChoice::PocketBook:ProgressChoice::Readest,revision)==
            (cleared_remote?SyncAction::Upload:SyncAction::ApplyRemote));
        assert(location==bravo);
    }
    bool failed=false;
    try { parse_progress(wire(),"wrong-user",hash); } catch(const std::runtime_error&) { failed=true; }
    assert(failed);
    failed=false;
    try { parse_progress("{\"configs\":[{},{}]}","user",hash); } catch(const std::runtime_error&) { failed=true; }
    assert(failed);
}
