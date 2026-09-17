#include "state.h"
#include "json_util.h"
#include <cerrno>
#include <algorithm>
#include <dirent.h>
#include <fcntl.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace readest {
namespace {
void sql(sqlite3* db, const char* text) {
    if (sqlite3_exec(db, text, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error("Cannot update app state database");
}
struct Query {
    sqlite3_stmt* stmt = nullptr;
    Query(sqlite3* db, const char* text) {
        if (sqlite3_prepare_v2(db, text, -1, &stmt, nullptr) != SQLITE_OK)
            throw std::runtime_error("Cannot prepare app state query");
    }
    ~Query() { sqlite3_finalize(stmt); }
    void bind(int i, const std::string& s) {
        if (sqlite3_bind_text(stmt, i, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::runtime_error("Cannot bind app state");
    }
    void bind(int i, long long n) {
        if (sqlite3_bind_int64(stmt, i, n) != SQLITE_OK) throw std::runtime_error("Cannot bind app state");
    }
    bool row() {
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) throw std::runtime_error("Cannot read or save app state");
        return rc == SQLITE_ROW;
    }
    std::string text(int i) {
        auto* s = sqlite3_column_text(stmt, i);
        return s ? std::string(reinterpret_cast<const char*>(s), sqlite3_column_bytes(stmt, i)) : "";
    }
    long long number(int i) { return sqlite3_column_int64(stmt, i); }
};
void identity(const std::string& user, const std::string& hash) {
    if (user.empty() || user.size() > 256 || user.find('\0') != std::string::npos ||
        hash.size() != 32 || hash.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Invalid saved book identity");
}
std::string metadata(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) throw std::runtime_error("Cannot read downloaded book metadata");
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 1 || st.st_size > 16384) {
        close(fd); throw std::runtime_error("Invalid downloaded book metadata");
    }
    std::string result(static_cast<size_t>(st.st_size), '\0'); size_t at = 0;
    while (at < result.size()) {
        auto n = read(fd, &result[at], result.size() - at);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); throw std::runtime_error("Cannot read downloaded book metadata"); }
        at += static_cast<size_t>(n);
    }
    close(fd); return result;
}
}
bool missing_download(const std::string& path) {
    struct stat st;
    // Permission errors, symlinks and non-regular files are not missing files.
    return !path.empty() && lstat(path.c_str(),&st)!=0 && errno==ENOENT;
}
Availability book_availability(const ManagedBook& book) {
    if (!book.path.empty() && !missing_download(book.path)) return Availability::OnDevice;
    if (book.book.deleted) return Availability::Removed;
    auto format = book.book.format;
    for (auto& c : format) if (c >= 'a' && c <= 'z') c -= 32;
    if ((!format.empty() && format != "EPUB") || book.epubs == -2) return Availability::Unavailable;
    if (book.epubs < 0) return Availability::Unknown;
    if (book.epubs == 0) return Availability::ProgressOnly;
    if (book.epubs > 1) return Availability::Multiple;
    return Availability::Downloadable;
}
size_t discover_device_books(State& state, const std::string& user,
    const std::vector<std::string>& roots, const std::string& managed_root,
    const std::function<bool()>& cancelled, ScanMetrics* metrics) {
    std::set<std::string> needed,known;
    for (const auto& book : state.books(user)) {
        if (book.path.empty() && !book.book.deleted) needed.insert(book.book.hash);
        else if(!book.path.empty()) known.insert(book.path);
    }
    if(needed.empty()) { if(metrics) *metrics={}; return 0; }
    const auto cached=state.scan_cache(); ScanCache updates;
    std::vector<StoredBook> matches;
    ScanMetrics measured;
    size_t matched = 0;
    std::vector<std::string> pending(roots.rbegin(), roots.rend());
    while (!pending.empty() && !needed.empty()) {
        if (cancelled()) throw std::runtime_error("Cancelled.");
        const auto path = pending.back(); pending.pop_back();
        if (path == managed_root) continue; // Managed recovery enforces account ownership.
        struct stat st;
        if (lstat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            DIR* dir = opendir(path.c_str());
            if (!dir) continue;
            std::vector<std::string> children;
            while (auto* entry = readdir(dir)) {
                const std::string name = entry->d_name;
                if (name[0] != '.' && name != "system") children.push_back(path + "/" + name);
            }
            closedir(dir);
            std::sort(children.rbegin(), children.rend());
            pending.insert(pending.end(), children.begin(), children.end());
            continue;
        }
        if (!S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 256LL * 1024 * 1024 || path.size() < 5) continue;
        auto extension = path.substr(path.size() - 5);
        for (auto& c : extension) if (c >= 'A' && c <= 'Z') c += 32;
        if (extension != ".epub") continue;
        if(known.count(path)) continue;
        ++measured.candidates;
        const auto stamp=epub_file_stamp(st);
        std::string hash;
        const auto previous=cached.find(path);
        if(previous!=cached.end() && previous->second.first==stamp) { hash=previous->second.second; ++measured.cache_hits; }
        else {
            ++measured.fingerprints;
            try { hash=epub_fingerprint(path); } catch(const std::runtime_error&) { continue; }
            updates[path]={stamp,hash};
        }
        if(!needed.count(hash)) continue;
        StoredBook book; book.path=path; ++measured.validations;
        try { book.integrity=inspect_epub(path); }
        catch(const std::runtime_error&) { updates[path]={stamp,""}; continue; }
        if(book.integrity.readest_hash!=hash) { updates[path]={stamp,book.integrity.readest_hash}; continue; }
        if(cancelled()) throw std::runtime_error("Cancelled.");
        matches.push_back(book); needed.erase(hash); ++matched;
    }
    if(cancelled()) throw std::runtime_error("Cancelled.");
    state.register_downloads(user,matches);
    state.save_scan_cache(updates);
    if(metrics) *metrics=measured;
    return matched;
}
State::State(const std::string& path) {
    struct stat st;
    if (path.empty() || path.find('\0') != std::string::npos ||
        (lstat(path.c_str(), &st) == 0 && !S_ISREG(st.st_mode)))
        throw std::runtime_error("Unsafe app state path");
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        sqlite3_close(db_); db_ = nullptr; throw std::runtime_error("Cannot open app state database");
    }
    try {
        sqlite3_busy_timeout(db_, 3000);
        sql(db_, "PRAGMA synchronous=FULL; PRAGMA journal_mode=DELETE; PRAGMA foreign_keys=ON;");
        Query version(db_, "PRAGMA user_version"); version.row();
        if (version.number(0) > 1) throw std::runtime_error("App state belongs to a newer version");
        sql(db_, "BEGIN IMMEDIATE;"
            "CREATE TABLE IF NOT EXISTS accounts(user TEXT PRIMARY KEY,cursor INTEGER NOT NULL);"
            "CREATE TABLE IF NOT EXISTS library(user TEXT NOT NULL,hash TEXT NOT NULL,title TEXT NOT NULL DEFAULT '',author TEXT NOT NULL DEFAULT '',format TEXT NOT NULL DEFAULT 'EPUB',raw TEXT NOT NULL DEFAULT '{}',deleted INTEGER NOT NULL DEFAULT 0,cursor INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(user,hash));"
            "CREATE TABLE IF NOT EXISTS downloads(user TEXT NOT NULL,hash TEXT NOT NULL,path TEXT NOT NULL,sha256 TEXT NOT NULL,size INTEGER NOT NULL,PRIMARY KEY(user,hash));"
            "CREATE TABLE IF NOT EXISTS sync(user TEXT NOT NULL,hash TEXT NOT NULL,local TEXT NOT NULL,remote TEXT NOT NULL,last_local TEXT NOT NULL,last_remote TEXT NOT NULL,baseline INTEGER NOT NULL,pending TEXT NOT NULL,config TEXT NOT NULL,revision INTEGER NOT NULL,PRIMARY KEY(user,hash));"
            "CREATE TABLE IF NOT EXISTS book_files(user TEXT NOT NULL,hash TEXT NOT NULL,epubs INTEGER NOT NULL,cover_key TEXT NOT NULL,cover_size INTEGER NOT NULL,cover_stamp INTEGER NOT NULL,PRIMARY KEY(user,hash));"
            "CREATE TABLE IF NOT EXISTS scan_cache(path TEXT PRIMARY KEY,stamp TEXT NOT NULL,hash TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS local_copies(path TEXT PRIMARY KEY,stamp TEXT,hash TEXT,title TEXT,author TEXT,position TEXT,size INTEGER);"
            "CREATE TABLE IF NOT EXISTS copy_choices(user TEXT,hash TEXT,path TEXT,PRIMARY KEY(user,hash));"
            "CREATE TABLE IF NOT EXISTS local_integrity(path TEXT PRIMARY KEY,stamp TEXT,sha256 TEXT);"
            "CREATE TABLE IF NOT EXISTS uploads(user TEXT,hash TEXT,stage INTEGER,sha256 TEXT,PRIMARY KEY(user,hash));"
            "PRAGMA user_version=1; COMMIT;");
    } catch (...) { sqlite3_close(db_); db_ = nullptr; throw; }
}
State::~State() { sqlite3_close(db_); }
long long State::cursor(const std::string& user) {
    Query q(db_, "SELECT cursor FROM accounts WHERE user=?"); q.bind(1,user);
    return q.row() ? q.number(0) : 0;
}
void State::apply_page(const std::string& user, long long since, const LibraryPage& page) {
    if (user.empty() || since < 0 || page.cursor < since) throw std::runtime_error("Invalid library page state");
    sql(db_, "BEGIN IMMEDIATE");
    try {
        if (cursor(user) != since) throw std::runtime_error("Library state changed during refresh");
        for (const auto& book : page.books) {
            identity(user, book.hash);
            Query q(db_, "INSERT OR REPLACE INTO library(user,hash,title,author,format,raw,deleted,cursor) VALUES(?,?,?,?,?,?,?,?)");
            q.bind(1,user); q.bind(2,book.hash); q.bind(3,book.title); q.bind(4,book.author);
            q.bind(5,book.format); q.bind(6,book.raw); q.bind(7,book.deleted ? 1LL : 0LL); q.bind(8,book.cursor); q.row();
        }
        Query q(db_, "INSERT OR REPLACE INTO accounts(user,cursor) VALUES(?,?)");
        q.bind(1,user); q.bind(2,page.cursor); q.row(); sql(db_, "COMMIT");
    } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
}
std::vector<ManagedBook> State::books(const std::string& user) {
    Query q(db_, "SELECT l.hash,l.title,l.author,l.format,l.raw,l.deleted,l.cursor,d.path,d.sha256,d.size,COALESCE(f.epubs,-1),f.cover_key,f.cover_size,f.cover_stamp FROM library l LEFT JOIN downloads d ON l.user=d.user AND l.hash=d.hash LEFT JOIN book_files f ON f.user=l.user AND f.hash=l.hash WHERE l.user=? AND (l.deleted=0 OR d.path IS NOT NULL) ORDER BY l.title COLLATE NOCASE,l.hash");
    q.bind(1,user); std::vector<ManagedBook> result;
    while (q.row()) {
        ManagedBook b; b.book.hash=q.text(0); b.book.title=q.text(1); b.book.author=q.text(2);
        b.book.format=q.text(3); b.book.raw=q.text(4); b.book.deleted=q.number(5)!=0; b.book.cursor=q.number(6);
        b.path=q.text(7); b.sha256=q.text(8); b.size=q.number(9); b.epubs=static_cast<int>(q.number(10));
        b.files.epubs=b.epubs; b.files.cover_key=q.text(11); b.files.cover_size=q.number(12); b.files.cover_stamp=q.number(13);
        if(missing_download(b.path)) b.path.clear();
        result.push_back(b);
    }
    std::map<std::string,size_t> by_hash;
    for(size_t i=0;i<result.size();++i) by_hash[result[i].book.hash]=i;
    for(const auto& copy:local_copies()) {
        if(missing_download(copy.path)) continue;
        auto found=by_hash.find(copy.hash);
        if(found==by_hash.end()) {
            ManagedBook b; b.local_only=true; b.book.hash=copy.hash; b.book.title=copy.title;
            b.book.author=copy.author; b.book.format="EPUB";
            by_hash[copy.hash]=result.size(); result.push_back(b);
        }
        result[by_hash[copy.hash]].copies.push_back(copy);
    }
    std::map<std::string,std::string> choices;
    Query selected(db_,"SELECT hash,path FROM copy_choices WHERE user=?"); selected.bind(1,user);
    while(selected.row()) choices[selected.text(0)]=selected.text(1);
    for(auto& b:result) if(!b.copies.empty()) {
        const LocalCopy* chosen=nullptr;
        for(const auto& copy:b.copies) if(copy.path==choices[b.book.hash]) chosen=&copy;
        if(!chosen && b.copies.size()>1) {
            const auto& position=b.copies.front().position;
            for(const auto& copy:b.copies) if(copy.position!=position) b.needs_copy_choice=true;
        }
        if(!chosen && !b.path.empty()) for(const auto& copy:b.copies) if(copy.path==b.path) chosen=&copy;
        if(!chosen) chosen=&b.copies.front();
        if(b.path!=chosen->path) { b.sha256=chosen->sha256; b.path=chosen->path; }
        else if(b.sha256.empty()) b.sha256=chosen->sha256;
        b.size=chosen->size;
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b) { return std::tie(a.book.title,a.book.hash)<std::tie(b.book.title,b.book.hash); });
    return result;
}
void State::save_book_files(const std::string& user,const std::map<std::string,BookFiles>& files) {
    auto library=books(user);
    sql(db_,"BEGIN IMMEDIATE");
    try {
        for(const auto& book:library) {
            const auto found=files.find(book.book.hash);
            const BookFiles info=found==files.end()?BookFiles():found->second;
            Query q(db_,"INSERT OR REPLACE INTO book_files VALUES(?,?,?,?,?,?)");
            q.bind(1,user); q.bind(2,book.book.hash); q.bind(3,info.epubs);
            q.bind(4,info.cover_key); q.bind(5,info.cover_size); q.bind(6,info.cover_stamp); q.row();
        }
        sql(db_,"COMMIT");
    } catch(...) { sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr); throw; }
}
void State::register_download(const std::string& user, const std::string& hash, const StoredBook& book) {
    identity(user,hash);
    if (book.integrity.readest_hash != hash || book.integrity.sha256.size()!=64 ||
        book.integrity.sha256.find_first_not_of("0123456789abcdef")!=std::string::npos ||
        book.path.empty() || book.path.find('\0')!=std::string::npos || book.integrity.size<=0)
        throw std::runtime_error("Invalid downloaded book state");
    const bool own=sqlite3_get_autocommit(db_);
    if(own) sql(db_, "BEGIN IMMEDIATE");
    try {
        Query existing(db_, "SELECT path,sha256 FROM downloads WHERE user=? AND hash=?");
        existing.bind(1,user); existing.bind(2,hash);
        if (existing.row()) {
            if(existing.text(1)!=book.integrity.sha256)
                throw std::runtime_error("Replacement EPUB differs from the original download");
            if(existing.text(0)!=book.path) {
                if(!missing_download(existing.text(0)))
                    throw std::runtime_error("Book already has a managed local copy");
                const auto actual=inspect_epub(book.path);
                if(actual.readest_hash!=hash || actual.sha256!=book.integrity.sha256 || actual.size!=book.integrity.size)
                    throw std::runtime_error("Replacement EPUB integrity mismatch");
                Query q(db_, "UPDATE downloads SET path=?,sha256=?,size=? WHERE user=? AND hash=?");
                q.bind(1,book.path); q.bind(2,book.integrity.sha256); q.bind(3,book.integrity.size);
                q.bind(4,user); q.bind(5,hash); q.row();
                // A new native file identity must establish its own baseline.
                Query reset(db_, "DELETE FROM sync WHERE user=? AND hash=?");
                reset.bind(1,user); reset.bind(2,hash); reset.row();
            }
        } else {
            Query q(db_, "INSERT INTO downloads VALUES(?,?,?,?,?)");
            q.bind(1,user); q.bind(2,hash); q.bind(3,book.path); q.bind(4,book.integrity.sha256); q.bind(5,book.integrity.size); q.row();
            Query l(db_, "INSERT OR IGNORE INTO library(user,hash,title) VALUES(?,?,?)");
            l.bind(1,user); l.bind(2,hash); l.bind(3,hash); l.row();
        }
        if(own) sql(db_, "COMMIT");
    } catch (...) { if(own) sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
}
std::vector<LocalCopy> State::local_copies() {
    std::vector<LocalCopy> result; Query q(db_,"SELECT l.path,l.stamp,l.hash,l.title,l.author,l.position,l.size,i.sha256 FROM local_copies l LEFT JOIN local_integrity i ON i.path=l.path AND i.stamp=l.stamp ORDER BY l.path");
    while(q.row()) {
        LocalCopy copy;copy.path=q.text(0);copy.stamp=q.text(1);copy.hash=q.text(2);copy.title=q.text(3);
        copy.author=q.text(4);copy.position=q.text(5);copy.size=q.number(6);copy.sha256=q.text(7);result.push_back(copy);
    }
    return result;
}
void State::replace_local_copies(const std::vector<LocalCopy>& copies) {
    sql(db_,"BEGIN IMMEDIATE");
    try {
        sql(db_,"DELETE FROM local_copies");
        for(const auto& copy:copies) {
            Query q(db_,"INSERT OR REPLACE INTO local_copies VALUES(?,?,?,?,?,?,?)");
            q.bind(1,copy.path);q.bind(2,copy.stamp);q.bind(3,copy.hash);q.bind(4,copy.title);
            q.bind(5,copy.author);q.bind(6,copy.position);q.bind(7,copy.size);q.row();
        }
        sql(db_,"COMMIT");
    } catch(...) {sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
void State::select_copy(const std::string& user,const std::string& hash,const std::string& path) {
    bool valid=false;
    for(const auto& copy:local_copies()) if(copy.hash==hash && copy.path==path && !missing_download(path)) valid=true;
    if(!valid) throw std::runtime_error("This local copy is no longer available");
    sql(db_,"BEGIN IMMEDIATE");
    try {
        Query q(db_,"INSERT OR REPLACE INTO copy_choices VALUES(?,?,?)"); q.bind(1,user);q.bind(2,hash);q.bind(3,path);q.row();
        Query reset(db_,"DELETE FROM sync WHERE user=? AND hash=?"); reset.bind(1,user);reset.bind(2,hash);reset.row();
        sql(db_,"COMMIT");
    } catch(...) {sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
void State::remember_integrity(const ManagedBook& book) {
    for(const auto& copy:book.copies) if(copy.path==book.path) {
        Query q(db_,"INSERT OR REPLACE INTO local_integrity VALUES(?,?,?)");
        q.bind(1,copy.path);q.bind(2,copy.stamp);q.bind(3,book.sha256);q.row();return;
    }
}
PendingUpload State::upload(const std::string& user,const std::string& hash) {
    identity(user,hash); Query q(db_,"SELECT stage,sha256 FROM uploads WHERE user=? AND hash=?");q.bind(1,user);q.bind(2,hash);
    if(!q.row()) return {};
    if(q.number(0)<1 || q.number(0)>3) throw std::runtime_error("Invalid saved upload stage");
    return {static_cast<UploadStage>(q.number(0)),q.text(1)};
}
void State::save_upload(const std::string& user,const std::string& hash,const PendingUpload& upload) {
    identity(user,hash);
    if(upload.stage==UploadStage::None) {
        Query q(db_,"DELETE FROM uploads WHERE user=? AND hash=?"); q.bind(1,user);q.bind(2,hash);q.row();
    } else {
        Query q(db_,"INSERT OR REPLACE INTO uploads VALUES(?,?,?,?)");q.bind(1,user);q.bind(2,hash);
        q.bind(3,static_cast<long long>(upload.stage));q.bind(4,upload.sha256);q.row();
    }
}
ScanCache State::scan_cache() {
    ScanCache cache; Query q(db_,"SELECT path,stamp,hash FROM scan_cache");
    while(q.row()) cache[q.text(0)]={q.text(1),q.text(2)};
    return cache;
}
void State::save_scan_cache(const ScanCache& updates) {
    if(updates.empty()) return;
    sql(db_,"BEGIN IMMEDIATE");
    try {
        for(const auto& item:updates) {
            Query q(db_,"INSERT OR REPLACE INTO scan_cache VALUES(?,?,?)");
            q.bind(1,item.first); q.bind(2,item.second.first); q.bind(3,item.second.second); q.row();
        }
        sql(db_,"COMMIT");
    } catch(...) { sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr); throw; }
}
void State::register_downloads(const std::string& user,const std::vector<StoredBook>& books) {
    if(books.empty()) return;
    sql(db_,"BEGIN IMMEDIATE");
    try {
        for(const auto& book:books) register_download(user,book.integrity.readest_hash,book);
        sql(db_,"COMMIT");
    } catch(...) { sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr); throw; }
}
std::map<std::string,SavedSync> State::syncs(const std::string& user) {
    std::map<std::string,SavedSync> result;
    Query q(db_,"SELECT local,remote,last_local,last_remote,baseline,pending,config,revision,hash FROM sync WHERE user=?");
    q.bind(1,user);
    while(q.row()) {
        SavedSync s;
        s.positions.local=q.text(0); s.positions.remote=q.text(1); s.positions.last_local=q.text(2); s.positions.last_remote=q.text(3);
        s.positions.has_baseline=q.number(4)!=0; s.pending_remote=q.text(5); s.remote_config=q.text(6); s.revision=q.number(7);
        result[q.text(8)]=std::move(s);
    }
    return result;
}
SavedSync State::sync(const std::string& user, const std::string& hash) {
    identity(user,hash); SavedSync s;
    Query q(db_, "SELECT local,remote,last_local,last_remote,baseline,pending,config,revision FROM sync WHERE user=? AND hash=?");
    q.bind(1,user); q.bind(2,hash);
    if(q.row()) {
        s.positions.local=q.text(0); s.positions.remote=q.text(1); s.positions.last_local=q.text(2); s.positions.last_remote=q.text(3);
        s.positions.has_baseline=q.number(4)!=0; s.pending_remote=q.text(5); s.remote_config=q.text(6); s.revision=q.number(7);
    }
    return s;
}
long long State::save_sync(const std::string& user, const std::string& hash, const SavedSync& next) {
    identity(user,hash);
    sql(db_, "BEGIN IMMEDIATE");
    try {
        if (sync(user,hash).revision!=next.revision || next.revision<0 || next.revision>=0x7fffffffffffffffLL)
            throw std::runtime_error("Reading state changed during synchronization");
        Query q(db_, "INSERT OR REPLACE INTO sync VALUES(?,?,?,?,?,?,?,?,?,?)");
        q.bind(1,user); q.bind(2,hash); q.bind(3,next.positions.local); q.bind(4,next.positions.remote);
        q.bind(5,next.positions.last_local); q.bind(6,next.positions.last_remote); q.bind(7,next.positions.has_baseline?1LL:0LL);
        q.bind(8,next.pending_remote); q.bind(9,next.remote_config); q.bind(10,next.revision+1); q.row();
        sql(db_, "COMMIT");
        return next.revision+1;
    } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
}
std::vector<std::string> recover_downloads(State& state, const std::string& user, const std::string& root) {
    DIR* raw = opendir(root.c_str());
    if (!raw) throw std::runtime_error("Cannot scan downloaded books");
    auto close_directory = [](DIR* d) { closedir(d); };
    std::unique_ptr<DIR, decltype(close_directory)> directory(raw, close_directory);
    std::set<std::string> known,known_dirs;
    for (const auto& b : state.books(user)) if (!b.path.empty()) {
        known.insert(b.path); known_dirs.insert(b.path.substr(0,b.path.rfind('/')));
    }
    std::vector<std::string> warnings;
    while (auto* e = readdir(raw)) {
        const std::string name=e->d_name;
        if (name.size()!=39 || name[32]!='-' || name.substr(0,32).find_first_not_of("0123456789abcdef")!=std::string::npos) continue;
        const auto dir=root+"/"+name; struct stat st;
        if(known_dirs.count(dir)) continue;
        if (lstat(dir.c_str(),&st)!=0 || !S_ISDIR(st.st_mode)) continue;
        try {
            if(lstat((dir+"/readest.json").c_str(),&st)!=0) continue; // Partial attempt, not a book.
            auto json=parse_json(metadata(dir+"/readest.json"));
            if(string_member(json.get(),"userId")!=user) continue;
            auto hash=string_member(json.get(),"bookHash"), filename=string_member(json.get(),"filename");
            if(hash!=name.substr(0,32) || filename.empty() || filename.find_first_of("/\\")!=std::string::npos || filename.find('\0')!=std::string::npos || filename=="..")
                throw std::runtime_error("Invalid recovered book path");
            StoredBook book; book.path=dir+"/"+filename;
            if(known.count(book.path) || missing_download(book.path)) continue;
            book.integrity=inspect_epub(book.path);
            if(book.integrity.readest_hash!=hash || book.integrity.sha256!=string_member(json.get(),"sha256") || book.integrity.size!=integer_member(json.get(),"size"))
                throw std::runtime_error("Recovered book integrity mismatch");
            state.register_download(user,hash,book); known.insert(book.path);
        } catch(const std::exception& ex) { warnings.push_back(name+": "+ex.what()); }
    }
    return warnings;
}
} // namespace readest
