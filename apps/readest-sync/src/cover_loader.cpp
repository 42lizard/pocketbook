#include "cover_loader.h"
#include <QPointer>
#include <algorithm>
using namespace readest;
VisibleCoverLoader::VisibleCoverLoader(Adapters adapters,Update update,std::function<void()> idle)
    :adapters_(std::move(adapters)),update_(std::move(update)),idle_(std::move(idle)) {}
VisibleCoverLoader::~VisibleCoverLoader() { if(active_) adapters_.cancel(); }
void VisibleCoverLoader::invalidate() {
    ++generation_;
    if(active_ && !cancelling_) { cancelling_=true; adapters_.cancel(); }
}
void VisibleCoverLoader::resetSession(std::string account) {
    invalidate(); ++session_; account_=std::move(account); visible_.clear();
    remote_enabled_=false; local_attempts_.clear(); remote_attempts_.clear(); ready_.clear();
}
void VisibleCoverLoader::refreshed() {
    invalidate(); ++session_; remote_enabled_=true; local_attempts_.clear(); remote_attempts_.clear(); ready_.clear(); schedule();
}
void VisibleCoverLoader::show(std::vector<LibraryEntry> visible) {
    invalidate(); visible_=std::move(visible); schedule();
}
void VisibleCoverLoader::schedule() {
    if(active_ || visible_.empty()) return;
    const auto generation=generation_;
    QPointer<VisibleCoverLoader> self(this);
    adapters_.defer([self,generation] { if(self && generation==self->generation_) self->pump(); });
}
void VisibleCoverLoader::publish(const BookId& id,const std::string& path) {
    for(auto& entry:visible_) if(entry.id==id && entry.cover!=path) {
        entry.cover=path; update_(id,path); return;
    }
}
void VisibleCoverLoader::pump() {
    if(active_ || account_.empty()) return;
    for(const auto& entry:visible_) {
        if(entry.id.account!=account_ || !entry.cover.empty()) continue;
        if(const auto found=ready_.find(key(entry.id)); found!=ready_.end()) {
            publish(entry.id,found->second); schedule(); return;
        }
        if(entry.availability==Availability::OnDevice && local_attempts_.insert(key(entry.id)).second) {
            const auto path=adapters_.local(entry);
            if(!path.empty()) { ready_[key(entry.id)]=path; publish(entry.id,path); }
            schedule(); return; // Yield between native UI-thread extractions.
        }
    }
    if(!remote_enabled_) return;
    std::vector<BookId> ids;
    for(const auto& entry:visible_) {
        if(entry.id.account==account_ && entry.cover.empty() && !entry.book.book.deleted && !remote_attempts_.count(key(entry.id)))
            ids.push_back(entry.id);
        if(ids.size()==6) break;
    }
    if(ids.empty()) return;
    QPointer<VisibleCoverLoader> self(this);
    const auto generation=generation_;
    const auto session=session_;
    active_=true; cancelling_=false;
    if(adapters_.start(ids,[self,generation,session,ids](OperationResult result) {
        if(!self) return;
        self->active_=false;
        self->cancelling_=false;
        if(session==self->session_) {
            for(const auto& [id,path]:result.cover_updates) {
                self->ready_[key(id)]=path;
                if(generation==self->generation_) self->publish(id,path);
            }
            if(result.outcome==Outcome::Cancelled) for(const auto& id:ids) {
                if(!self->ready_.count(key(id)) &&
                   std::find(result.cover_attempts.begin(),result.cover_attempts.end(),id)==result.cover_attempts.end())
                    self->remote_attempts_.erase(key(id));
            }
        }
        self->idle_(); self->schedule();
    })) {
        for(const auto& id:ids) remote_attempts_.insert(key(id));
    } else active_=false; // Retry only after another demand/lifecycle event.
}
