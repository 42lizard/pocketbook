#include "cover_loader.h"
#include <QCoreApplication>
#include <cassert>
#include <deque>
#include <iostream>
using namespace readest;

LibraryEntry book(const std::string& hash,Availability availability=Availability::Downloadable) {
    LibraryEntry entry; entry.id={"account",hash}; entry.availability=availability; return entry;
}
struct Harness {
    std::deque<std::function<void()>> deferred;
    std::vector<BookId> local, requested, published;
    VisibleCoverLoader::Completion completion;
    bool accept=true;
    std::string local_path="local.png";
    int starts=0,cancels=0,idles=0;
    VisibleCoverLoader loader{
        {[this](std::function<void()> work) { deferred.push_back(std::move(work)); },
         [this](const LibraryEntry& entry) { local.push_back(entry.id); return local_path; },
         [this](const std::vector<BookId>& ids,VisibleCoverLoader::Completion done) {
             ++starts; if(!accept) return false;
             assert(!completion); requested=ids; completion=std::move(done); return true;
         },
         [this] { ++cancels; }},
        [this](const BookId& id,const std::string&) { published.push_back(id); },
        [this] { ++idles; }};
    void drain() {
        int count=0;
        while(!deferred.empty()) { assert(++count<100); auto work=std::move(deferred.front()); deferred.pop_front(); work(); }
    }
    void finish(OperationResult result={}) { auto done=std::move(completion); completion={}; assert(done); done(std::move(result)); }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    {
        Harness h; h.loader.resetSession("account");
        h.loader.show({book("old",Availability::OnDevice)});
        h.loader.show({book("visible",Availability::OnDevice)});
        h.drain();
        assert(h.local.size()==1 && h.local[0].hash=="visible");
        assert(h.published.size()==1 && h.published[0].hash=="visible");
        assert(h.starts==0); // Startup never enables networking.
    }
    {
        Harness h; h.loader.resetSession("account"); h.loader.refreshed();
        h.loader.show({book("1"),book("2"),book("3"),book("4"),book("5"),book("6"),book("7")}); h.drain();
        assert(h.requested.size()==6 && h.starts==1);
        OperationResult result; result.cover_updates.push_back({book("1").id,"remote.png"});
        h.finish(result); h.drain();
        assert(h.requested.size()==1 && h.requested[0].hash=="7" && h.starts==2);
        h.finish(); h.drain();
        assert(h.starts==2 && h.published.size()==1); // Missing covers do not loop.
        h.loader.show({book("1"),book("2")}); h.drain();
        assert(h.starts==2); // Navigation reuses successes and remembers failures.
        h.loader.refreshed(); h.drain(); assert(h.starts==3);
        assert(h.requested.size()==1 && h.requested[0].hash=="2"); h.finish();
    }
    {
        Harness h; h.loader.resetSession("account"); h.loader.refreshed();
        h.loader.show({book("success"),book("retry")}); h.drain();
        h.loader.show({}); assert(h.cancels==1); // Foreground/details suspend demand.
        OperationResult result; result.outcome=Outcome::Cancelled;
        result.cover_updates.push_back({book("success").id,"remote.png"});
        h.finish(result); h.drain();
        assert(h.starts==1 && h.published.empty());
        h.loader.show({book("success"),book("retry")}); h.drain();
        assert(h.published.size()==1 && h.published[0].hash=="success");
        assert(h.starts==2 && h.requested.size()==1 && h.requested[0].hash=="retry");
        h.finish(); h.drain();
    }
    {
        Harness h; h.loader.resetSession("account"); h.loader.refreshed();
        h.loader.show({book("same")}); h.drain();
        h.loader.resetSession(""); h.loader.resetSession("account");
        h.loader.refreshed(); h.loader.show({book("same")});
        OperationResult old; old.cover_updates.push_back({book("same").id,"old-session.png"});
        h.finish(old); h.drain();
        assert(h.published.empty() && h.starts==2); // Even the same account is a new session.
        h.finish();
    }
    {
        Harness h; h.loader.resetSession("account"); h.loader.refreshed();
        h.loader.show({book("missing"),book("unfinished")}); h.drain(); h.loader.show({});
        OperationResult result; result.outcome=Outcome::Cancelled;
        result.cover_attempts.push_back(book("missing").id);
        h.finish(result); h.loader.show({book("missing"),book("unfinished")}); h.drain();
        assert(h.requested.size()==1 && h.requested[0].hash=="unfinished"); h.finish();
    }
    {
        Harness h; h.loader.resetSession("account"); h.loader.refreshed(); h.accept=false;
        h.loader.show({book("retry")}); h.drain();
        assert(h.starts==1 && !h.loader.active()); h.drain(); assert(h.starts==1);
        h.accept=true; h.loader.show({book("retry")}); h.drain();
        assert(h.starts==2 && h.loader.active());
        OperationResult failed; failed.outcome=Outcome::Failed; h.finish(failed); h.drain();
        h.loader.show({book("retry")}); h.drain(); assert(h.starts==2);
        h.loader.refreshed(); h.drain(); assert(h.starts==3); h.finish();
    }
    {
        Harness h; h.loader.resetSession("account"); h.loader.refreshed();
        h.loader.show({book("old")}); h.drain();
        h.loader.show({book("new")}); h.drain(); assert(h.starts==1);
        OperationResult old; old.cover_updates.push_back({book("old").id,"old.png"});
        h.finish(old); assert(h.published.empty()); h.drain();
        assert(h.starts==2 && h.requested[0].hash=="new"); h.finish();
        h.loader.show({book("old")}); h.drain(); assert(h.starts==2 && h.published.size()==1);
        // A refreshed metadata snapshot may refer to a different cover revision.
        h.loader.refreshed(); h.loader.show({book("old")}); h.drain();
        assert(h.starts==3); h.finish();
    }
    {
        Harness h; h.loader.resetSession("account"); h.loader.refreshed();
        h.loader.show({book("same")}); h.drain();
        auto other=book("same"); other.id.account="other";
        h.loader.resetSession("other"); h.loader.refreshed(); h.loader.show({other});
        OperationResult old; old.cover_updates.push_back({book("same").id,"old.png"});
        h.finish(old); h.drain(); assert(h.published.empty());
        assert(h.requested.size()==1 && h.requested[0].account=="other");
        OperationResult current; current.cover_updates.push_back({other.id,"new.png"});
        h.finish(current); assert(h.published.size()==1 && h.published[0].account=="other");
    }
    {
        Harness h; h.local_path.clear(); h.loader.resetSession("account"); h.loader.refreshed();
        h.loader.show({book("fallback",Availability::OnDevice)}); h.drain();
        assert(h.local.size()==1 && h.starts==1); h.finish();
        h.loader.show({book("fallback",Availability::OnDevice)}); h.drain();
        assert(h.local.size()==1 && h.starts==1);
    }
    {
        std::function<void()> queued;
        VisibleCoverLoader::Completion late;
        int calls=0,cancels=0;
        auto loader=std::make_unique<VisibleCoverLoader>(VisibleCoverLoader::Adapters{
            [&](auto work) { queued=std::move(work); },
            [&](const LibraryEntry&) { ++calls; return std::string(); },
            [&](const std::vector<BookId>&,auto done) { late=std::move(done); return true; },
            [&] { ++cancels; }},
            [&](const BookId&,const std::string&) { ++calls; },[&] { ++calls; });
        loader->resetSession("account"); loader->refreshed(); loader->show({book("late")});
        auto start=std::move(queued); start();
        loader->show({book("local",Availability::OnDevice)}); loader.reset();
        OperationResult result; result.cover_updates.push_back({book("late").id,"late.png"}); late(result);
        assert(calls==0 && cancels>=1);
        loader=std::make_unique<VisibleCoverLoader>(VisibleCoverLoader::Adapters{
            [&](auto work) { queued=std::move(work); },
            [&](const LibraryEntry&) { ++calls; return std::string(); },{},{}},
            [&](const BookId&,const std::string&) { ++calls; },[] {});
        loader->resetSession("account"); loader->show({book("local",Availability::OnDevice)});
        loader.reset(); queued(); assert(calls==0);
    }
    std::cout<<"Visible cover lifecycle checks passed.\n";
}
