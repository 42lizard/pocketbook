#pragma once
#include "application.h"
#include <QObject>
#include <functional>
#include <map>
#include <set>

// All methods and adapter callbacks run on the UI thread. Only start()'s
// acquisition task uses the shared worker; its completion returns to this thread.
// defer() and an accepted start() deliver callbacks asynchronously, never inline.
// A rejected start() must not deliver a completion.
class VisibleCoverLoader : public QObject {
public:
    using Completion=std::function<void(readest::OperationResult)>;
    struct Adapters {
        std::function<void(std::function<void()>)> defer;
        std::function<std::string(const readest::LibraryEntry&)> local;
        std::function<bool(const std::vector<readest::BookId>&,Completion)> start;
        std::function<void()> cancel;
    };
    using Update=std::function<void(const readest::BookId&,const std::string&)>;
    VisibleCoverLoader(Adapters adapters,Update update,std::function<void()> idle);
    ~VisibleCoverLoader() override;
    bool active() const { return active_; }
    // Reset even when signing back into the same account. Empty account signs out.
    void resetSession(std::string account);
    // A successful explicit refresh allows new attempts and enables remote work.
    void refreshed();
    // Replace visible demand; empty demand suspends work (foreground or details).
    void show(std::vector<readest::LibraryEntry> visible);
private:
    Adapters adapters_;
    Update update_;
    std::function<void()> idle_;
    std::string account_;
    using Key=std::pair<std::string,std::string>;
    static Key key(const readest::BookId& id) { return {id.account,id.hash}; }
    std::vector<readest::LibraryEntry> visible_;
    std::set<Key> local_attempts_,remote_attempts_;
    std::map<Key,std::string> ready_;
    unsigned generation_=0,session_=0;
    bool remote_enabled_=false,active_=false,cancelling_=false;
    void invalidate();
    void schedule();
    void pump();
    void publish(const readest::BookId& id,const std::string& path);
};
