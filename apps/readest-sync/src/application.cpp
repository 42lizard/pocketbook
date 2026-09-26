#include "application.h"
#include "upload.h"
#include <stdexcept>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <cstdio>
#include <set>

namespace readest {
ApplicationService::ApplicationService(ApplicationConfig config):config_(std::move(config)) {
    if(!config_.transport.request && !config_.transport.download) config_.transport=https_transport();
    if(!config_.transport.request || !config_.transport.download)
        throw std::invalid_argument("Both request and download transports are required.");
}
void ApplicationService::trace(const char* phase) const {
    // Best-effort, bounded diagnostics. Only fixed phase names and process
    // metadata: never credentials, requests, filenames or book contents.
    const int fd=open((config_.root+"/operations.log").c_str(),O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW,0600);
    if(fd<0) return;
    struct stat st;
    if(fstat(fd,&st)==0 && S_ISREG(st.st_mode) && (st.st_size<65536 || ftruncate(fd,0)==0)) {
        struct rusage usage={}; getrusage(RUSAGE_SELF,&usage);
        long peak=usage.ru_maxrss;
#ifdef __APPLE__
        peak/=1024;
#endif
        char line[192];
        const int size=snprintf(line,sizeof(line),"%lld pid=%ld peak_kib=%ld %s\n",
            static_cast<long long>(time(nullptr)),static_cast<long>(getpid()),peak,phase);
        if(size>0 && static_cast<size_t>(size)<sizeof(line)) { const auto written=write(fd,line,size); (void)written; }
    }
    close(fd);
}
void ApplicationService::check_cancel(const std::atomic<bool>& cancel) const {
    if(cancel.load()) throw std::runtime_error("Cancelled.");
}
size_t ApplicationService::scan(const std::atomic<bool>& cancel) {
    trace("scan.begin");
    std::vector<LocalCopy> copies;
    std::map<std::string,LocalCopy> cached;
    for(const auto& copy:state_->local_copies()) cached[copy.path]=copy;
    std::set<std::string> seen;
    for(const auto& native:native_books(config_.database)) {
        check_cancel(cancel);
        if(!seen.insert(native.path).second) continue;
        struct stat st;
        if(lstat(native.path.c_str(),&st) || !S_ISREG(st.st_mode) || st.st_size<=0 || st.st_size>256LL*1024*1024) continue;
        const auto stamp=epub_file_stamp(st);
        LocalCopy copy;
        const auto found=cached.find(native.path);
        if(found!=cached.end() && found->second.stamp==stamp) copy=found->second;
        else {
            copy.path=native.path;copy.stamp=stamp;copy.size=st.st_size;
            try { copy.hash=epub_fingerprint(copy.path); }
            catch(const std::exception&) { continue; }
            copy.title=copy.path.substr(copy.path.rfind('/')+1);
            try { const auto metadata=epub_metadata(copy.path); if(!metadata.title.empty()) copy.title=metadata.title; copy.author=metadata.author; }
            catch(const std::exception&) { /* Filename is a usable metadata fallback. */ }
        }
        copy.position=native.position;copies.push_back(std::move(copy));
    }
    check_cancel(cancel); state_->replace_local_copies(copies);
    const auto matches=copies.size();
    trace("scan.end"); return matches;
}
ManagedBook ApplicationService::resolve(const BookId& id) {
    if(!cloud_ || id.account!=cloud_->session().user_id)
        throw std::runtime_error("This book belongs to a different signed-in account.");
    for(const auto& book:state_->books(id.account)) if(book.book.hash==id.hash) {
        if(book.needs_copy_choice) throw std::runtime_error("Choose a local copy before opening or synchronizing this book.");
        return book;
    }
    throw std::runtime_error("This book is no longer in the library.");
}
LibrarySnapshot ApplicationService::snapshot() {
    trace("snapshot.begin");
    LibrarySnapshot result; result.initialized=bool(state_ && cloud_);
    if(!result.initialized) return result;
    result.signed_in=cloud_ && cloud_->session().signed_in();
    result.account=cloud_->session().user_id;
    const auto syncs=state_->syncs(result.account);
    std::vector<std::string> paths;
    for(const auto& book:state_->books(result.account)) {
        LibraryEntry entry; entry.id={result.account,book.book.hash}; entry.book=book;
        entry.availability=book_availability(book);
        entry.upload_pending=result.signed_in && state_->upload(result.account,book.book.hash).stage!=UploadStage::None;
        const auto saved=syncs.find(book.book.hash); if(saved!=syncs.end()) entry.sync=saved->second;
        entry.remote_percentage=book.book.deleted?-1:reading_percentage(book.book.raw,entry.sync.remote_config);
        const auto image=cover_path(config_.root,result.account,book.book.hash,book.files);
        if(valid_cover(image)) entry.cover=image;
        else if(valid_cover(image+".local.png")) entry.cover=image+".local.png";
        if(!book.path.empty()) paths.push_back(book.path);
        for(const auto& copy:book.copies) paths.push_back(copy.path);
        result.books.push_back(std::move(entry));
    }
    if(!paths.empty()) {
        try {
            const auto percentages=native_percentages(config_.database,paths);
            for(auto& entry:result.books) {
                const auto found=percentages.find(entry.book.path);
                if(found!=percentages.end()) entry.local_percentage=found->second;
                for(auto& copy:entry.book.copies) {
                    const auto value=percentages.find(copy.path);if(value!=percentages.end()) copy.percentage=value->second;
                }
            }
        } catch(const std::exception&) { /* Missing native counts display as unknown. */ }
    }
    trace("snapshot.end"); return result;
}
void ApplicationService::synchronize(const Request& request, OperationResult& result, const std::atomic<bool>& cancel) {
    const VerifiedManagedBook verified(resolve(request.book));
    const auto& book=verified.book();
    if(book.book.deleted) throw std::runtime_error("Removed from Readest. Re-upload this book or read offline.");
    if(book.local_only) throw std::runtime_error("Upload this book to Readest before synchronizing.");
    state_->remember_integrity(book);
    const auto native=native_position(config_.database,book.path);
    const bool open=request.command==Command::Open;
    const auto audit=config_.root+"/native-"+std::to_string(time(nullptr))+"-"+std::to_string(getpid())+"-"+std::to_string(++sequence_);
    const auto transition=transition_progress(*cloud_,*state_,verified,native,time(nullptr),request.choice,request.revision,
        open,{config_.database,audit,config_.model,config_.firmware},cancel);
    result.sync_action=transition.action; result.revision=transition.revision;
    result.position_order=transition.position_order; result.error=transition.error; result.progress_warning=transition.warning;
    result.outcome=Outcome::Synced;
    if(transition.outcome!=ResumeOutcome::SyncUnavailable &&
       (result.sync_action==SyncAction::None || result.sync_action==SyncAction::Upload || result.sync_action==SyncAction::EstablishBaseline ||
        transition.outcome==ResumeOutcome::Applied ||
        (request.choice==ProgressChoice::Readest && result.sync_action==SyncAction::ApplyRemote)))
        state_->save_upload(request.book.account,request.book.hash,{});
    switch(transition.outcome) {
    case ResumeOutcome::NotRequested: case ResumeOutcome::Blocked: return;
    case ResumeOutcome::NoPending: break;
    case ResumeOutcome::NeedsNativeSettings: result.outcome=Outcome::NeedsNativeSettings; break;
    case ResumeOutcome::Applied: result.outcome=Outcome::Applied; break;
    case ResumeOutcome::SyncUnavailable: result.outcome=Outcome::SyncUnavailable; break;
    case ResumeOutcome::AppliedUnrecorded: result.outcome=Outcome::AppliedUnrecorded; return;
    case ResumeOutcome::CommitUncertain: result.outcome=Outcome::NativeCommitUncertain; return;
    }
    check_cancel(cancel); result.open_path=book.path;
}
OperationResult ApplicationService::execute(const Request& request,const std::atomic<bool>& cancel) {
    // Cloud survives operations; its callback borrows only the current token.
    struct Scope {
        const std::atomic<bool>*& slot;
        ~Scope() { slot=nullptr; }
    } scope{operation_cancel_};
    operation_cancel_=&cancel;
    OperationResult result;
    try {
        check_cancel(cancel);
        if(request.command==Command::Initialize) {
            make_directory(config_.root);
            trace("initialize.begin");
            make_directory(config_.books_root.substr(0,config_.books_root.rfind('/'))); make_directory(config_.books_root);
            state_.reset(new State(config_.root+"/state.db"));
            cloud_.reset(new Cloud(config_.root+"/session.json",config_.ca,config_.public_key,
                config_.auth_origin,config_.api_origin,
                [this](const std::string& url,const std::string& method,const std::vector<std::string>& headers,
                    const std::string& body,const std::string& ca,size_t cap) {
                    return config_.transport.request(url,method,headers,body,ca,cap,*operation_cancel_);
                }));
            try { cloud_->load_session(); } catch(const std::exception&) { result.outcome=Outcome::SessionInvalid; }
            if(cloud_->session().signed_in()) {
                const auto warnings=recover_downloads(*state_,cloud_->session().user_id,config_.books_root);
                if(!warnings.empty()) result.recovery_warning=warnings.front();
                // Cloud metadata remains cached at startup.
            }
            try { scan(cancel); } catch(const std::exception& e) { check_cancel(cancel); result.recovery_warning=e.what(); }
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
                if(!cloud_->session().signed_in() && request.command!=Command::Scan && request.command!=Command::ReadOffline && request.command!=Command::Resume && request.command!=Command::SelectCopy)
                    throw std::runtime_error("Sign in first.");
                switch(request.command) {
                case Command::Refresh: {
                    trace("refresh.begin");
                    try {
                        long long since=state_->cursor(cloud_->session().user_id);
                        for(int pages=0;;++pages) {
                            check_cancel(cancel);
                            if(pages>=1000) throw std::runtime_error("Library refresh limit reached; refresh again to continue.");
                            auto next=fetch_library_page(*cloud_,since,100,time(nullptr));
                            state_->apply_page(cloud_->session().user_id,since,next); since=next.cursor;
                            trace("refresh.page.saved");
                            if(!next.more) break;
                        }
                    } catch(const std::exception& e) { check_cancel(cancel); result.metadata_error=e.what(); }
                    scan(cancel);
                    trace("availability.begin");
                    state_->save_book_files(cloud_->session().user_id,fetch_book_files(*cloud_,time(nullptr)));
                    trace("availability.end");
                    result.outcome=Outcome::Refreshed; break;
                }
                case Command::Scan: result.matched=scan(cancel); result.outcome=Outcome::Scanned; break;
                case Command::Download: {
                    resolve(request.book); scan(cancel);
                    // Preserve the existing last-chance reuse check for a requested
                    // cloud download. Native-only inventory never invokes this crawler.
                    if(resolve(request.book).path.empty())
                        discover_device_books(*state_,request.book.account,config_.book_roots,config_.books_root,[&cancel] {return cancel.load();});
                    const auto book=resolve(request.book);
                    if(!book.path.empty()) { (void)VerifiedManagedBook(book); result.outcome=Outcome::Reused; break; }
                    const auto stored=download_book(*cloud_,book.book,config_.books_root,config_.ca,time(nullptr),config_.transport.bind_download(cancel));
                    state_->register_download(request.book.account,request.book.hash,stored);
                    result.outcome=Outcome::Downloaded; break;
                }
                case Command::UploadCover: {
                    const VerifiedManagedBook verified(resolve(request.book));
                    if(verified.book().local_only || verified.book().book.deleted)
                        throw std::runtime_error("Upload the book to Readest first.");
                    if(!upload_cover(*cloud_,verified,config_.transport,config_.ca,config_.root,cancel))
                        throw std::runtime_error("This EPUB has no supported embedded cover.");
                    result.outcome=Outcome::CoverUploaded;break;
                }
                case Command::Upload: {
                    const VerifiedManagedBook verified(resolve(request.book));
                    state_->remember_integrity(verified.book());
                    UploadPosition position;
                    try {position.native=native_position(config_.database,verified.book().path);}
                    catch(const UnsupportedNativePosition& e) {position.error=e.what();position.unsupported=true;}
                    catch(const std::exception& e) {position.error=e.what();}
                    const auto uploaded=upload_book(*cloud_,*state_,verified,position,config_.transport,config_.ca,config_.root,cancel);
                    result.sync_action=uploaded.action;result.progress_warning=uploaded.warning;
                    result.revision=state_->sync(request.book.account,request.book.hash).revision;
                    result.outcome=uploaded.pending?Outcome::UploadPending:Outcome::Uploaded;break;
                }
                case Command::SelectCopy: {
                    if(request.book.account!=cloud_->session().user_id) throw std::runtime_error("Account changed; select the book again");
                    state_->select_copy(request.book.account,request.book.hash,request.local_path);
                    result.outcome=Outcome::CopySelected;break;
                }
                case Command::Sync: case Command::Open: synchronize(request,result,cancel); break;
                case Command::ReadOffline: {
                    const VerifiedManagedBook verified(resolve(request.book)); const auto& book=verified.book();
                    state_->remember_integrity(book); check_cancel(cancel);
                    result.open_path=book.path; result.outcome=Outcome::LocalOpen; break;
                }
                case Command::Covers: {
                    trace("covers.begin");
                    if(request.books.size()>6) throw std::runtime_error("Too many visible covers");
                    const auto books=state_->books(cloud_->session().user_id);
                    for(const auto& id:request.books) {
                        check_cancel(cancel);
                        if(id.account!=cloud_->session().user_id) continue;
                        for(const auto& book:books) if(book.book.hash==id.hash && !book.book.deleted) {
                            try {
                                if(cache_cover(*cloud_,config_.root,id.hash,book.files,config_.ca,time(nullptr),config_.transport.bind_download(cancel)))
                                    result.cover_updates.push_back({id,cover_path(config_.root,id.account,id.hash,book.files)});
                            } catch(const std::exception&) { check_cancel(cancel); }
                            result.cover_attempts.push_back(id);
                            break;
                        }
                    }
                    break;
                }
                case Command::Resume: scan(cancel); break;
                default: break;
                }
            }
        }
    } catch(const std::exception& error) {
        trace("operation.error");
        result.error=error.what(); result.open_path.clear();
        result.outcome=cancel.load()?Outcome::Cancelled:Outcome::Failed;
    }
    // Recovery/read errors never escape the worker completion boundary.
    if(request.command!=Command::Covers && !cancel.load()) try { result.library=snapshot(); }
    catch(const std::exception& error) {
        if(result.outcome==Outcome::AppliedUnrecorded || result.outcome==Outcome::NativeCommitUncertain)
            result.error+=" Library snapshot failed: "+std::string(error.what());
        else if(result.outcome==Outcome::Applied) result.progress_warning+=" Library snapshot failed: "+std::string(error.what());
        else { result.outcome=Outcome::Failed; result.error=error.what(); }
        result.open_path.clear();
    }
    trace("operation.end");
    return result;
}
}
