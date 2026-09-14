#include "application.h"
#include <ctime>
#include <unistd.h>

namespace readest {
namespace {
void verify(const ManagedBook& book) {
    const auto bytes=inspect_epub(book.path);
    if(bytes.readest_hash!=book.book.hash || bytes.sha256!=book.sha256 || bytes.size!=book.size)
        throw std::runtime_error("The local EPUB changed. Sync is stopped to protect reading progress.");
}
}
ApplicationService::ApplicationService(ApplicationConfig config):config_(std::move(config)) {}
void ApplicationService::check_cancel(const std::atomic<bool>& cancel) const {
    if(cancel.load()) throw std::runtime_error("Cancelled.");
}
size_t ApplicationService::scan(const std::atomic<bool>& cancel) {
    return discover_device_books(*state_,cloud_->session().user_id,config_.book_roots,config_.books_root,
        [&cancel] { return cancel.load(); });
}
ManagedBook ApplicationService::resolve(const BookId& id) {
    if(!cloud_ || !cloud_->session().signed_in() || id.account!=cloud_->session().user_id)
        throw std::runtime_error("This book belongs to a different signed-in account.");
    for(const auto& book:state_->books(id.account)) if(book.book.hash==id.hash) return book;
    throw std::runtime_error("This book is no longer in the library.");
}
NativePosition ApplicationService::capture(const std::string& path) {
    const auto file=config_.root+"/capture-"+std::to_string(getpid())+"-"+std::to_string(++sequence_)+".db";
    try {
        backup_native_database(config_.database,file);
        auto result=native_position(file,path); unlink(file.c_str()); return result;
    } catch(...) { unlink(file.c_str()); throw; }
}
LibrarySnapshot ApplicationService::snapshot() {
    LibrarySnapshot result; result.initialized=bool(state_ && cloud_);
    result.signed_in=cloud_ && cloud_->session().signed_in();
    if(!result.signed_in) return result;
    result.account=cloud_->session().user_id;
    const auto syncs=state_->syncs(result.account);
    std::vector<std::string> paths;
    for(const auto& book:state_->books(result.account)) {
        LibraryEntry entry; entry.id={result.account,book.book.hash}; entry.book=book;
        entry.availability=book_availability(book);
        const auto saved=syncs.find(book.book.hash); if(saved!=syncs.end()) entry.sync=saved->second;
        entry.remote_percentage=reading_percentage(book.book.raw,entry.sync.remote_config);
        const auto image=cover_path(config_.root,result.account,book.book.hash,book.files);
        if(valid_cover(image)) entry.cover=image;
        else if(valid_cover(image+".local.png")) entry.cover=image+".local.png";
        if(!book.path.empty()) paths.push_back(book.path);
        result.books.push_back(std::move(entry));
    }
    if(!paths.empty()) {
        const auto file=config_.root+"/percentages-"+std::to_string(getpid())+"-"+std::to_string(++sequence_)+".db";
        try {
            backup_native_database(config_.database,file);
            const auto percentages=native_percentages(file,paths);
            for(auto& entry:result.books) {
                const auto found=percentages.find(entry.book.path);
                if(found!=percentages.end()) entry.local_percentage=found->second;
            }
        } catch(const std::exception&) { /* Missing native counts display as unknown. */ }
        unlink(file.c_str());
    }
    return result;
}
void ApplicationService::synchronize(const Request& request, OperationResult& result, const std::atomic<bool>& cancel) {
    const auto book=resolve(request.book); verify(book);
    const auto native=capture(book.path);
    const bool open=request.command==Command::Open;
    bool synced=true;
    try {
        result.sync_action=sync_managed(*cloud_,*state_,book,native.cfi,time(nullptr),request.choice,request.revision);
        result.outcome=Outcome::Synced;
    } catch(const std::exception& error) {
        if(!open || cancel.load()) throw;
        synced=false; result.outcome=Outcome::SyncUnavailable; result.error=error.what();
    }
    auto saved=state_->sync(request.book.account,request.book.hash); result.revision=saved.revision;
    if(synced && (result.sync_action==SyncAction::Conflict || result.sync_action==SyncAction::BackwardUnsupported)) {
        const auto remote=readest_start_cfi(saved.positions.remote);
        if(!native.cfi.empty() && !remote.empty()) result.position_order=compare_cfi(native.cfi,remote);
    }
    if(!open) return;
    if(synced && (result.sync_action==SyncAction::Conflict || result.sync_action==SyncAction::Unsupported ||
                  result.sync_action==SyncAction::BackwardUnsupported)) return;
    if(synced && !saved.pending_remote.empty()) {
        if(!native.indexed || !native.has_settings) result.outcome=Outcome::NeedsNativeSettings;
        else {
            if(native.cfi!=saved.positions.local) throw std::runtime_error("Local position changed. Sync again before opening.");
            const auto audit=config_.root+"/native-"+std::to_string(time(nullptr))+"-"+std::to_string(getpid())+"-"+std::to_string(++sequence_);
            check_cancel(cancel);
            apply_native_position(config_.database,audit,native,saved.pending_remote,config_.model,config_.firmware);
            // Finish recording an applied position even if cancellation arrives during the write.
            saved.positions.local=saved.pending_remote;
            saved.positions.last_local=saved.pending_remote; saved.positions.last_remote=saved.positions.remote;
            saved.positions.has_baseline=true; saved.pending_remote.clear();
            state_->save_sync(request.book.account,request.book.hash,saved);
            result.revision=saved.revision+1; result.outcome=Outcome::Applied;
        }
    }
    check_cancel(cancel); result.open_path=book.path;
}
OperationResult ApplicationService::execute(const Request& request,const std::atomic<bool>& cancel) {
    OperationResult result;
    try {
        check_cancel(cancel);
        if(request.command==Command::Initialize) {
            make_directory(config_.root);
            make_directory(config_.books_root.substr(0,config_.books_root.rfind('/'))); make_directory(config_.books_root);
            state_.reset(new State(config_.root+"/state.db"));
            cloud_.reset(new Cloud(config_.root+"/session.json",config_.ca,config_.public_key,
                config_.auth_origin,config_.api_origin,config_.transport?config_.transport:https_request));
            try { cloud_->load_session(); } catch(const std::exception&) { result.outcome=Outcome::SessionInvalid; }
            if(cloud_->session().signed_in()) {
                const auto warnings=recover_downloads(*state_,cloud_->session().user_id,config_.books_root);
                if(!warnings.empty()) result.recovery_warning=warnings.front();
                // Startup serves cached metadata; discovery is explicit or part of Refresh.
            }
        } else {
            if(!cloud_ || !state_) throw std::runtime_error("Initialize the app first.");
            if(request.command==Command::SignIn) {
                cloud_->sign_in(request.email,request.password,time(nullptr));
                const auto warnings=recover_downloads(*state_,cloud_->session().user_id,config_.books_root);
                if(!warnings.empty()) result.recovery_warning=warnings.front();
                result.outcome=Outcome::SignedIn;
            } else if(request.command==Command::SignOut) {
                cloud_->sign_out(); result.outcome=Outcome::SignedOut;
            } else {
                if(!cloud_->session().signed_in()) throw std::runtime_error("Sign in first.");
                switch(request.command) {
                case Command::Refresh: {
                    try {
                        long long since=state_->cursor(cloud_->session().user_id);
                        for(int pages=0;;++pages) {
                            check_cancel(cancel);
                            if(pages>=1000) throw std::runtime_error("Library refresh limit reached; refresh again to continue.");
                            auto next=fetch_library_page(*cloud_,since,100,time(nullptr));
                            state_->apply_page(cloud_->session().user_id,since,next); since=next.cursor;
                            if(!next.more) break;
                        }
                    } catch(const std::exception& e) { check_cancel(cancel); result.metadata_error=e.what(); }
                    scan(cancel);
                    state_->save_book_files(cloud_->session().user_id,fetch_book_files(*cloud_,time(nullptr)));
                    result.outcome=Outcome::Refreshed; break;
                }
                case Command::Scan: result.matched=scan(cancel); result.outcome=Outcome::Scanned; break;
                case Command::Download: {
                    resolve(request.book); scan(cancel);
                    const auto book=resolve(request.book);
                    if(!book.path.empty()) { verify(book); result.outcome=Outcome::Reused; break; }
                    const auto stored=download_book(*cloud_,book.book,config_.books_root,config_.ca,time(nullptr));
                    state_->register_download(request.book.account,request.book.hash,stored);
                    result.outcome=Outcome::Downloaded; break;
                }
                case Command::Sync: case Command::Open: synchronize(request,result,cancel); break;
                case Command::ReadOffline: {
                    const auto book=resolve(request.book); verify(book); check_cancel(cancel);
                    result.open_path=book.path; result.outcome=Outcome::LocalOpen; break;
                }
                case Command::Covers: {
                    if(request.books.size()>6) throw std::runtime_error("Too many visible covers");
                    const auto books=state_->books(cloud_->session().user_id);
                    for(const auto& id:request.books) {
                        check_cancel(cancel);
                        if(id.account!=cloud_->session().user_id) continue;
                        for(const auto& book:books) if(book.book.hash==id.hash && !book.book.deleted) {
                            try {
                                if(cache_cover(*cloud_,config_.root,id.hash,book.files,config_.ca,time(nullptr)))
                                    result.cover_updates.push_back({id,cover_path(config_.root,id.account,id.hash,book.files)});
                            } catch(const std::exception&) { check_cancel(cancel); }
                            break;
                        }
                    }
                    break;
                }
                case Command::Resume: break;
                default: break;
                }
            }
        }
    } catch(const std::exception& error) {
        result.error=error.what(); result.open_path.clear();
        result.outcome=cancel.load()?Outcome::Cancelled:Outcome::Failed;
    }
    // Recovery/read errors never escape the worker completion boundary.
    if(request.command!=Command::Covers && !cancel.load()) try { result.library=snapshot(); }
    catch(const std::exception& error) { result.outcome=Outcome::Failed; result.error=error.what(); result.open_path.clear(); }
    return result;
}
}
