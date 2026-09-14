#include "progress.h"
#include "controller.h"
#include "platform.h"
#include <QCoreApplication>
#include <QTimer>
#include <QDir>
#include <QSaveFile>
#include <QUrl>
#include <QVariantList>
#include "probe.h"
#include "public_config.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <cstring>
#include <ctime>
#include <functional>
#include <fcntl.h>
#include <memory>
#include <thread>
#include <unistd.h>

namespace {
using namespace readest;
const std::string root=platform::dataRoot().toStdString();
const std::string books_root=QDir::cleanPath(QString::fromStdString(root)+"/../../Books/Readest").toStdString();
const std::string database=platform::nativeDatabase().toStdString();
const std::string ca=root+"/ca-certificates.crt";
std::unique_ptr<Cloud> cloud;
std::unique_ptr<State> state;
std::vector<ManagedBook> library;
std::vector<size_t> visible;
std::thread worker;
std::atomic<bool> done(false),cancel(false);
bool busy=false, exiting=false, initialized=false, signed_in=false, detail=false, choosing_open=false;
AppController* controller=nullptr;
QTimer* poll_timer=nullptr;
bool landscape=false;
std::string cover_user;
std::map<std::string,double> local_percentages;
bool reader_opened=false;

std::string status="Starting…", filter, open_path, busy_message;
std::string model,firmware;
char email[256]={},password[1024]={};
size_t selected_book=0,page=0;
int selected=0, availability_filter=0;
unsigned sequence=0;
SyncAction last_action=SyncAction::None;
long long conflict_revision=0;
std::vector<std::pair<std::string,std::function<void()>>> buttons;
void draw(); void poll(); void home(); void open_selected(bool open,ProgressChoice choice=ProgressChoice::Automatic);

void refresh_local_percentages() {
    local_percentages.clear();
    std::vector<std::string> paths;
    for(const auto& book:library)
        if(!book.path.empty() && !missing_download(book.path)) paths.push_back(book.path);
    if(paths.empty()) return;
    const auto snapshot=root+"/percentages-"+std::to_string(getpid())+"-"+std::to_string(++sequence)+".db";
    try {
        backup_native_database(database,snapshot);
        local_percentages=native_percentages(snapshot,paths);
    } catch(const std::exception&) { /* Unavailable page counts display as unknown. */ }
    unlink(snapshot.c_str());
}
QString percentage_label(double value) {
    return value<0?QStringLiteral("—"):QString::number(value,'f',1)+"%";
}
QString remote_percentage_label(const ManagedBook& book) {
    const auto saved=state->sync(cover_user,book.book.hash);
    return percentage_label(reading_percentage(book.book.raw,saved.remote_config));
}
QString local_percentage_label(const ManagedBook& book) {
    const auto found=local_percentages.find(book.path);
    return percentage_label(found==local_percentages.end()?-1:found->second);
}

void restore_session() {
    status="Wi-Fi connects when needed. Downloaded books work offline.";
    try { cloud->load_session(); }
    catch(const std::exception&) {
        // Keep the file until successful sign-in atomically replaces it.
        status="Saved sign-in could not be loaded. Please sign in again.";
    }
}
void refresh_cache() {
    signed_in=cloud && cloud->session().signed_in();
    cover_user=signed_in?cloud->session().user_id:"";
    library=signed_in?state->books(cloud->session().user_id):std::vector<ManagedBook>();
}
using Clock=std::chrono::steady_clock;
std::atomic<bool> connection_pending(false);
std::atomic<int> connection_result(0);
bool connecting=false, online_active=false;
Clock::time_point connection_deadline, next_ping;
std::function<void()> connected_task;
std::string online_message;
int network_connected(int result) {
    // The SDK callback only publishes a result; UI work stays in poll().
    connection_result.store(result);
    connection_pending.store(false);
    return 0;
}
void launch_worker(std::function<void()> task) {
    try {
        worker=std::thread([task] {
            set_http_cancellation(&cancel);
            try { task(); }
            catch(const std::exception& e) { status=e.what(); open_path.clear(); }
            catch(...) { status="Operation failed."; open_path.clear(); }
            if(!cancel.load()) refresh_local_percentages();
            set_http_cancellation(nullptr); done.store(true);
        });
    } catch(const std::exception& e) { status=e.what(); done=true; }
    poll_timer->start(200);
}
void start(const std::string& message,std::function<void()> task,bool online=false) {
    if(busy) return;
    // NetConnectAsync has no request ID/cancel API. Never overlap an outstanding
    // callback after a timeout with a new connection attempt.
    if(online && connection_pending.load()) {
        status="The previous Wi-Fi connection is still finishing. Try again shortly."; draw(); return;
    }
    busy=true; done=false; cancel=false; busy_message=message; open_path.clear();
    online_active=online;
    if(online) {
        connecting=true; connected_task=std::move(task); online_message=message;
        busy_message="Connecting to Wi-Fi";
        connection_deadline=Clock::now()+std::chrono::seconds(60);
        next_ping=Clock::now()+std::chrono::seconds(30);
        draw(); platform::pingNetwork(); connection_pending=true;
        platform::connectNetwork(network_connected);
        poll_timer->start(200);
    } else { draw(); launch_worker(std::move(task)); }
}
bool tiled() { return initialized && signed_in && !detail; }
size_t page_size() { return landscape?4:6; }
size_t page_books() { const auto first=page*page_size(); return first<visible.size()?std::min(page_size(),visible.size()-first):0; }
std::string availability(const ManagedBook& book) {
    switch(book_availability(book)) {
        case Availability::OnDevice: return "On device";
        case Availability::Downloadable: return "Available to download";
        case Availability::ProgressOnly: return "Progress only";
        case Availability::Unknown: return "Not checked";
        case Availability::Unavailable: return "EPUB unavailable";
        case Availability::Multiple: return "Multiple EPUBs";
        case Availability::Removed: return "Removed from cloud";
    }
    return "Not checked";
}
size_t scan_device_books() {
    const auto count=discover_device_books(*state,cloud->session().user_id,
        platform::bookRoots(),books_root,[] { return cancel.load(); });
    refresh_cache();
    return count;
}
// Publish an immutable UI snapshot. Never read worker-owned data while busy.
void draw() {
    QVariantMap view{{"busy",busy}};
    view["title"]="Readest Sync";
    if(busy) view["status"]=QString::fromStdString(exiting?"Stopping…":busy_message+"…");
    else {
        view["detail"]=detail; view["signedIn"]=signed_in; view["initialized"]=initialized;
        view["status"]=QString::fromStdString(status);
        view["page"]=static_cast<int>(page+1);
        view["pages"]=static_cast<int>(std::max<size_t>(1,(visible.size()+page_size()-1)/page_size()));
        view["count"]=static_cast<int>(visible.size());
        view["filter"]=QString::fromStdString(filter);
        view["availabilityFilter"]=availability_filter;
        view["canPrevious"]=page>0;
        view["canNext"]=(page+1)*page_size()<visible.size();
        QVariantList actions,books;
        for(size_t i=0;i<buttons.size();++i)
            actions.append(QVariantMap{{"text",QString::fromStdString(buttons[i].first)},{"index",static_cast<int>(i)}});
        view["actions"]=actions;
        if(tiled()) for(size_t i=page*page_size();i<std::min(visible.size(),page*page_size()+page_size());++i) {
            const auto& b=library[visible[i]];
            auto image=cover_path(root,cover_user,b.book.hash,b.files);
            if(!valid_cover(image) && !b.path.empty() && !missing_download(b.path)) {
                const auto local=image+".local.png";
                if(!valid_cover(local)) {
                    const auto thumbnail=platform::localCover(QString::fromStdString(b.path),QSize(400,600));
                    if(!thumbnail.isNull()) {
                        QSaveFile output(QString::fromStdString(local));
                        if(output.open(QIODevice::WriteOnly) && thumbnail.save(&output,"PNG")) output.commit();
                    }
                }
                if(valid_cover(local)) image=local;
            }
            if(!valid_cover(image)) image.clear();
            // The provider validates and decodes bytes with Qt, never LoadPNGStretch.
            books.append(QVariantMap{{"title",QString::fromStdString(b.book.title.empty()?"Untitled":b.book.title)},
                {"author",QString::fromStdString(b.book.author)},{"availability",QString::fromStdString(availability(b))},
                {"cover",QString::fromStdString(image)}, {"localPath",QString::fromStdString(b.path)},
                {"localProgress",local_percentage_label(b)},{"readestProgress",remote_percentage_label(b)},
                {"index",static_cast<int>(2+i-page*page_size())}});
        }
        view["books"]=books;
        if(detail && selected_book<library.size()) {
            const auto& b=library[selected_book];
            view["title"]=QString::fromStdString(b.book.title);
            std::string hint=availability(b)+". "+b.book.author;
            if(b.epubs==0 && b.path.empty()) hint+="\nOnly position data is available. Upload the EPUB in Readest, then check again.";
            hint+="\nPocketBook: "+local_percentage_label(b).toStdString()+" · Readest: "+remote_percentage_label(b).toStdString();
            hint+="\nPercentages use each reader’s page counts. — means unavailable.";
            view["hint"]=QString::fromStdString(hint);
        }
    }
    controller->publish(std::move(view));
}
void quit() {
    if(busy) { exiting=true; cancel=true; draw(); }
    else QCoreApplication::quit();
}
void show_detail();
void poll() {
    if(!busy) return;
    if(connecting) {
        if(cancel.load() || Clock::now()>=connection_deadline) {
            status=cancel.load()?"Cancelled.":"Wi-Fi connection timed out. Try again or use Read offline.";
            connected_task=nullptr; connecting=false; done=true;
        } else if(!connection_pending.load()) {
            connecting=false;
            const int result=connection_result.load();
            if(result==0) {
                busy_message=online_message; draw();
                auto task=std::move(connected_task); connected_task=nullptr;
                launch_worker(std::move(task));
            } else {
                connected_task=nullptr;
                status="Wi-Fi connection failed (network "+std::to_string(result)+"). Try again or use Read offline.";
                done=true;
            }
        }
    }
    if(online_active && !done.load() && !cancel.load() && Clock::now()>=next_ping) {
        platform::pingNetwork(); next_ping=Clock::now()+std::chrono::seconds(30);
    }
    if(!done.load()) return;
    if(worker.joinable()) worker.join();
    online_active=false; busy=false; poll_timer->stop();
    if(exiting) { QCoreApplication::quit(); return; }
    if(detail && selected_book<library.size()) show_detail(); else home();
    if(!open_path.empty()) {
        const auto path=open_path; open_path.clear();
        reader_opened=true;
        if(!platform::openBook(QString::fromStdString(path))) { reader_opened=false; status="The native reader could not open this book."; draw(); }
    }
}
void refresh_library() {
    const auto focus=detail && selected_book<library.size()?library[selected_book].book.hash:std::string();
    start("Refreshing library and covers",[focus] {
        std::string metadata_error;
        try {
          long long since=state->cursor(cloud->session().user_id);
          for(int pages=0;;++pages) {
            if(cancel.load()) throw std::runtime_error("Cancelled.");
            if(pages>=1000) throw std::runtime_error("Library refresh limit reached; refresh again to continue.");
            auto next=fetch_library_page(*cloud,since,100,time(nullptr));
            state->apply_page(cloud->session().user_id,since,next);
            since=next.cursor;
            if(!next.more) break;
          }
        } catch(const std::exception& e) {
            if(cancel.load()) throw;
            metadata_error=e.what();
        }
        scan_device_books();
        if(!focus.empty()) for(size_t i=0;i<library.size();++i) if(library[i].book.hash==focus) selected_book=i;
        try {
            auto files=fetch_book_files(*cloud,time(nullptr));
            state->save_book_files(cloud->session().user_id,files); refresh_cache();
        } catch(const std::exception& e) {
            throw std::runtime_error(std::string("Availability check failed: ")+e.what()+
                (metadata_error.empty()?"":" Library refresh also failed: "+metadata_error));
        }
        int unavailable=0, covers=0, absent=0;
        for(const auto& book:library) {
            if(cancel.load()) throw std::runtime_error("Cancelled.");
            if(book.book.deleted) continue;
            try {
                if(cache_cover(*cloud,root,book.book.hash,book.files,ca,time(nullptr))) ++covers;
                else ++absent;
            }
            catch(const std::exception&) { if(cancel.load()) throw; ++unavailable; }
        }
        if(!metadata_error.empty()) status="File availability updated. Library metadata refresh failed: "+metadata_error;
        else status="Library refreshed. Covers: "+std::to_string(covers)+"; not in cloud: "+std::to_string(absent)+
            (unavailable?"; failed: "+std::to_string(unavailable):".");
    },true);
}
void sign_in() {
    if(!email[0] || !password[0]) { status="Enter both your email and password first."; draw(); return; }
    const std::string address=email,secret=password;
    std::memset(password,0,sizeof(password));
    start("Signing in",[address,secret] {
        cloud->sign_in(address,secret,time(nullptr));
        recover_downloads(*state,cloud->session().user_id,books_root);
        scan_device_books(); status="Signed in. Choose Refresh library.";
    },true);
}
void home() {
    detail=false; selected=0; buttons.clear();
    if(!initialized) { buttons.push_back({"Exit",quit}); draw(); return; }
    if(!signed_in) { buttons.push_back({"Sign in",sign_in}); buttons.push_back({"Exit",quit}); draw(); return; }
    buttons.push_back({"Refresh library",refresh_library});
    buttons.push_back({"Search",[] {}});
    visible.clear();
    auto lower=[](std::string s) { for(auto& c:s) if(c>='A'&&c<='Z') c+=32; return s; };
    for(size_t i=0;i<library.size();++i) {
        const auto kind=book_availability(library[i]);
        if(availability_filter==1 && kind!=Availability::Downloadable) continue;
        if(availability_filter==2 && kind!=Availability::OnDevice) continue;
        if(availability_filter==3 && kind!=Availability::ProgressOnly) continue;
        if(lower(library[i].book.title+" "+library[i].book.author).find(lower(filter))!=std::string::npos) visible.push_back(i);
    }
    if(page*page_size()>=visible.size()) page=0;
    for(size_t i=page*page_size();i<std::min(visible.size(),page*page_size()+page_size());++i) {
        size_t index=visible[i];
        buttons.push_back({library[index].book.title,[index] {
            selected_book=index; choosing_open=false; last_action=SyncAction::None; status=availability(library[index])+". "+library[index].book.author; show_detail();
        }});
    }
    buttons.push_back({"Previous",[] { if(page>0) --page; home(); }});
    buttons.push_back({"Next",[] { if((page+1)*page_size()<visible.size()) ++page; home(); }});
    buttons.push_back({"Sign out",[] { start("Signing out",[] { cloud->sign_out(); refresh_cache(); status="Signed out. Downloaded files are retained."; }); }});
    buttons.push_back({"Exit",quit}); draw();
}
std::string action_message(SyncAction action) {
    switch(action) {
        case SyncAction::Upload: return "PocketBook position uploaded to Readest.";
        case SyncAction::ApplyRemote: return "Readest position downloaded. Choose Open at Readest position to apply it.";
        case SyncAction::Conflict: return "Both positions differ. Choose which position to use.";
        case SyncAction::Unsupported: return "This position format is unsupported. Reading progress was not replaced.";
        case SyncAction::BackwardUnsupported: return "The saved positions differ. Choose where to open this book.";
        default: return "Reading positions are synchronized.";
    }
}
NativePosition capture(const std::string& path) {
    const auto snapshot=root+"/capture-"+std::to_string(getpid())+"-"+std::to_string(++sequence)+".db";
    try {
        backup_native_database(database,snapshot);
        auto result=native_position(snapshot,path); unlink(snapshot.c_str()); return result;
    } catch(...) { unlink(snapshot.c_str()); throw; }
}
void verify(const ManagedBook& book) {
    auto bytes=inspect_epub(book.path);
    if(bytes.readest_hash!=book.book.hash || bytes.sha256!=book.sha256 || bytes.size!=book.size)
        throw std::runtime_error("The local EPUB changed. Sync is stopped to protect reading progress.");
}
void open_selected(bool open,ProgressChoice choice) {
    choosing_open=open;
    const auto book=library.at(selected_book); const auto revision=conflict_revision;
    start(open?"Opening book":"Synchronizing",[book,open,choice,revision] {
        verify(book);
        auto native=capture(book.path);
        bool synced=true;
        try {
            last_action=sync_managed(*cloud,*state,book,native.cfi,time(nullptr),choice,revision);
            status=action_message(last_action);
        } catch(const std::exception& e) {
            if(!open) throw;
            synced=false; status=std::string("Opening saved local position. Sync unavailable: ")+e.what();
        }
        auto saved=state->sync(cloud->session().user_id,book.book.hash);
        conflict_revision=saved.revision;
        if(synced && (last_action==SyncAction::Conflict || last_action==SyncAction::BackwardUnsupported)) {
            const auto remote=readest_start_cfi(saved.positions.remote);
            if(!native.cfi.empty() && !remote.empty()) {
                const int order=compare_cfi(native.cfi,remote);
                if(order!=0) status+=order>0?" Readest is earlier; PocketBook is farther ahead.":" PocketBook is earlier; Readest is farther ahead.";
            }
        }
        if(!open) return;
        if(synced && (last_action==SyncAction::Conflict || last_action==SyncAction::Unsupported || last_action==SyncAction::BackwardUnsupported)) return;
        if(synced && !saved.pending_remote.empty()) {
            if(!native.indexed || !native.has_settings) {
                status="Open once, close with Back, then Sync. If positions differ, choose Use Readest position, then Open.";
            } else {
                if(native.cfi!=saved.positions.local) throw std::runtime_error("Local position changed. Sync again before opening.");
                const auto audit=root+"/native-"+std::to_string(time(nullptr))+"-"+std::to_string(getpid())+"-"+std::to_string(++sequence);
                if(cancel.load()) return;
                apply_native_position(database,audit,native,saved.pending_remote,model,firmware);
                saved.positions.local=saved.pending_remote;
                saved.positions.last_local=saved.pending_remote; saved.positions.last_remote=saved.positions.remote;
                saved.positions.has_baseline=true; saved.pending_remote.clear();
                state->save_sync(cloud->session().user_id,book.book.hash,saved);
                status="Readest position applied.";
            }
        }
        if(cancel.load()) return;
        open_path=book.path;
    },true);
}
void open_local(const ManagedBook& book) {
    start("Opening PocketBook position",[book] {
        verify(book); last_action=SyncAction::None;
        open_path=book.path; status="Opening the saved PocketBook position.";
    });
}
void show_detail() {
    detail=true; selected=0; buttons.clear();
    auto book=library.at(selected_book);
    if(missing_download(book.path)) { book.path.clear(); last_action=SyncAction::None; }
    const bool choose_position=last_action==SyncAction::BackwardUnsupported ||
        (last_action==SyncAction::Conflict && choosing_open);
    if(choose_position && !book.path.empty()) {
        buttons.push_back({"Open at PocketBook position",[book] { open_local(book); }});
        buttons.push_back({"Open at Readest position",[] { open_selected(true,ProgressChoice::Readest); }});
    } else if(book.path.empty() && (book.epubs==0 || book.epubs>1 || book.book.deleted || availability(book)=="EPUB unavailable")) {
        buttons.push_back({"Check availability",refresh_library});
    } else if(book.path.empty()) {
        buttons.push_back({"Download EPUB",[book] { start("Downloading",[book] {
            scan_device_books();
            for(size_t i=0;i<library.size();++i) if(library[i].book.hash==book.book.hash && !library[i].path.empty()) {
                selected_book=i; status="Found a matching EPUB on device. Choose Open to read."; return;
            }
            auto downloaded=download_book(*cloud,book.book,books_root,ca,time(nullptr));
            state->register_download(cloud->session().user_id,book.book.hash,downloaded);
            refresh_cache();
            for(size_t i=0;i<library.size();++i) if(library[i].book.hash==book.book.hash) selected_book=i;
            status="Downloaded and verified. Choose Open to read.";
        },true); }});
    } else {
        const auto saved=state->sync(cover_user,book.book.hash);
        buttons.push_back({saved.pending_remote.empty()?"Open":"Open at Readest position",[] { open_selected(true); }});
        buttons.push_back({"Sync now",[] { open_selected(false); }});
        buttons.push_back({"Read offline",[book] { open_local(book); }});
    }
    if(last_action==SyncAction::Conflict && !choose_position) {
        buttons.push_back({"Use PocketBook position",[] { open_selected(false,ProgressChoice::PocketBook); }});
        buttons.push_back({"Use Readest position",[] { open_selected(false,ProgressChoice::Readest); }});
    }
    buttons.push_back({"Back to library",home}); draw();
}
} // namespace

AppController::AppController(QObject* parent):QObject(parent) {
    controller=this; poll_timer=new QTimer(this);
    QObject::connect(poll_timer,&QTimer::timeout,this,[]{ poll(); });
    draw();
}
AppController::~AppController() {
    cancel=true; poll_timer->stop(); connected_task=nullptr;
    if(worker.joinable()) worker.join();
    std::memset(password,0,sizeof(password));
    controller=nullptr; poll_timer=nullptr;
}
void AppController::initialize() {
    if(busy || initialized) return;
    model=platform::model().toStdString(); firmware=platform::firmware().toStdString();
    start("Starting",[] {
        make_directory(root); make_directory(root+"/../../Books"); make_directory(books_root);
        state.reset(new State(root+"/state.db"));
        cloud.reset(new Cloud(root+"/session.json",ca,public_anon_key)); restore_session();
        if(cloud->session().signed_in()) {
            auto warnings=recover_downloads(*state,cloud->session().user_id,books_root);
            if(!warnings.empty()) status="Some downloads need attention: "+warnings.front();
            scan_device_books();
        }
        refresh_cache(); initialized=true;
    });
}
void AppController::activate(int index) {
    if(busy || index<0 || static_cast<size_t>(index)>=buttons.size()) return;
    auto action=buttons[index].second; action();
}
void AppController::signIn(const QString& address,const QString& secret) {
    if(busy || !initialized || signed_in) return;
    const auto a=address.trimmed().toUtf8(),s=secret.toUtf8();
    if(a.size()>=static_cast<int>(sizeof(email)) || s.size()>=static_cast<int>(sizeof(password))) {
        status="Email or password is too long."; draw(); return;
    }
    std::snprintf(email,sizeof(email),"%s",a.constData());
    std::snprintf(password,sizeof(password),"%s",s.constData()); sign_in();
}
void AppController::search(const QString& text) { if(!busy) { filter=text.toStdString(); page=0; home(); } }
void AppController::setAvailabilityFilter(int value) {
    if(!busy && tiled() && value>=0 && value<=3) { availability_filter=value; page=0; home(); }
}
void AppController::scanDevice() {
    if(busy || !tiled()) return;
    start("Looking for existing EPUBs",[] {
        const auto count=scan_device_books();
        status="Device scan complete. Matched "+std::to_string(count)+" existing EPUBs.";
    });
}
void AppController::turnPage(int direction) {
    if(busy || !tiled()) return;
    if(direction<0 && page>0) --page;
    if(direction>0 && (page+1)*page_size()<visible.size()) ++page;
    home();
}
void AppController::setLandscape(bool value) { if(landscape!=value) { landscape=value; if(!busy && tiled()) home(); } }
void AppController::back() { if(busy) quit(); else if(detail) home(); else quit(); }
void AppController::close() { quit(); }
void AppController::resume() {
    if(!busy && initialized && signed_in && reader_opened) {
        reader_opened=false;
        start("Updating PocketBook progress",[] {});
    }
}
