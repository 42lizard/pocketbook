#include "probe.h"

#include <json-c/json.h>
#include <sqlite3.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <fcntl.h>
#include <ctime>
#include <memory>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <cmath>
#include <algorithm>

namespace readest {
namespace {
using Json = std::unique_ptr<json_object, decltype(&json_object_put)>;
Json object() { return Json(json_object_new_object(), json_object_put); }
void put(json_object* obj, const char* key, const std::string& value) {
    json_object_object_add(obj, key, json_object_new_string_len(value.data(), value.size()));
}

class Database {
public:
    sqlite3* db = nullptr;
    explicit Database(const std::string& path, bool writable = false) {
        if (sqlite3_open_v2(path.c_str(), &db, writable ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            std::string error = db ? sqlite3_errmsg(db) : "SQLite allocation failed";
            sqlite3_close(db);
            db = nullptr;
            throw std::runtime_error(error);
        }
        sqlite3_busy_timeout(db, 1000);
    }
    ~Database() { sqlite3_close(db); }
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
};

template<class Visit>
void query_each(Database& db, const char* sql, const std::vector<std::string>& params, Visit visit) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db.db, sql, -1, &raw, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db.db));
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw, sqlite3_finalize);
    for (size_t i = 0; i < params.size(); ++i)
        if (sqlite3_bind_text(raw, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db.db));
    int rc;
    while ((rc = sqlite3_step(raw)) == SQLITE_ROW) {
        auto row = object();
        for (int col = 0; col < sqlite3_column_count(raw); ++col) {
            const char* name = sqlite3_column_name(raw, col);
            if (sqlite3_column_type(raw, col) == SQLITE_NULL)
                json_object_object_add(row.get(), name, nullptr);
            else {
                const char* text = reinterpret_cast<const char*>(sqlite3_column_text(raw, col));
                int size = sqlite3_column_bytes(raw, col);
                if (size > 65536) throw std::runtime_error("Oversized database value");
                put(row.get(), name, std::string(text, size));
            }
        }
        visit(row.get());
    }
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db.db));
}

Json query(Database& db, const char* sql, const std::vector<std::string>& params = {}) {
    Json rows(json_object_new_array(), json_object_put);
    query_each(db, sql, params, [&](json_object* row) {
        if (json_object_array_length(rows.get()) >= 128)
            throw std::runtime_error("Too many matching rows; refusing ambiguous state");
        json_object_array_add(rows.get(), json_object_get(row));
    });
    return rows;
}

std::string field(json_object* row, const char* key) {
    json_object* value = nullptr;
    if (!json_object_object_get_ex(row, key, &value) || !value) return "";
    return json_object_get_string(value);
}

// Queries use identical column order and string/null encoding on both sides.
bool same_rows(json_object* a, json_object* b) {
    return std::string(json_object_to_json_string_ext(a, JSON_C_TO_STRING_PLAIN)) ==
           json_object_to_json_string_ext(b, JSON_C_TO_STRING_PLAIN);
}

void schema(Database& db, json_object* report) {
    auto rows = query(db, "SELECT name, sql FROM sqlite_master WHERE type='table' AND name IN "
                         "('files','folders','storages','books_settings','books_fast_hashes',"
                         "'Items','Tags','TagNames') ORDER BY name");
    json_object_object_add(report, "schema", rows.release());
}

struct FileStamp {
    bool exists;
    off_t size;
    time_t modified;
    ino_t inode;
    bool operator==(const FileStamp& other) const {
        return exists == other.exists && (!exists ||
            (size == other.size && modified == other.modified && inode == other.inode));
    }
};
FileStamp stamp(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        if (errno == ENOENT) return {false, 0, 0, 0};
        throw std::runtime_error("Cannot stat " + path + ": " + strerror(errno));
    }
    if (!S_ISREG(st.st_mode)) throw std::runtime_error("Not a regular file: " + path);
    return {true, st.st_size, st.st_mtime, st.st_ino};
}

void copy_file(const std::string& src, const std::string& dst) {
    std::ifstream input(src, std::ios::binary);
    std::ofstream output(dst, std::ios::binary | std::ios::trunc);
    if (!input || !output) throw std::runtime_error("Cannot copy database snapshot");
    char bytes[65536];
    while (input.read(bytes, sizeof(bytes)) || input.gcount())
        output.write(bytes, input.gcount());
    output.close();
    if (input.bad() || !output) throw std::runtime_error("Database snapshot copy failed");
}
} // namespace

NativeState inspect(const std::string& explorer, const std::string& books,
                    const std::string& book_path) {
    auto report = object();
    put(report.get(), "book_path", book_path);
    std::set<std::string> candidates;
    std::string hash;
    bool ambiguous = false;
    size_t slash = book_path.find_last_of('/');
    std::string directory = book_path.substr(0, slash);
    std::string filename = book_path.substr(slash + 1);
    auto library = object();
    try {
        Database db(explorer);
        schema(db, library.get());
        auto matches = query(db,
            "SELECT DISTINCT f.book_id, hex(f.fast_hash) AS fast_hash FROM files f "
            "JOIN folders d ON d.id=f.folder_id AND d.storageid=f.storageid "
            "WHERE (d.name=? AND f.filename=?) OR f.filename=?",
            {directory, filename, book_path});
        if (json_object_array_length(matches.get()) == 1) {
            auto row = json_object_array_get_idx(matches.get(), 0);
            hash = field(row, "fast_hash");
            auto positions = query(db,
                "SELECT profileid, position, position_ts, cpage, npage, completed "
                "FROM books_settings WHERE bookid=? ORDER BY profileid",
                {field(row, "book_id")});
            ambiguous = json_object_array_length(positions.get()) > 1;
            if (json_object_array_length(positions.get()) == 1) {
                auto cfi = point_cfi(field(json_object_array_get_idx(positions.get(), 0), "position"));
                if (!cfi.empty()) candidates.insert(cfi);
            }
            json_object_object_add(library.get(), "positions", positions.release());
        } else if (json_object_array_length(matches.get()) > 1) {
            ambiguous = true;
        }
        json_object_object_add(library.get(), "matches", matches.release());
    } catch (const std::exception& e) {
        put(library.get(), "error", e.what());
    }
    json_object_object_add(report.get(), "explorer", library.release());
    auto metadata = object();
    try {
        Database db(books);
        schema(db, metadata.get());
        if (!hash.empty()) {
            auto tags = query(db,
                "SELECT i.OID AS item_id, n.TagName AS name, t.Val AS value, t.TimeEdt AS edited "
                "FROM Items i JOIN Tags t ON t.ItemID=i.OID JOIN TagNames n ON n.OID=t.TagID "
                "WHERE upper(i.HashUUID)=? AND n.TagName IN "
                "('doc.last_read_position','doc.read_progress','doc.time_opened')",
                {hash});
            int position_count = 0;
            const size_t tag_count = json_object_array_length(tags.get());
            for (size_t i = 0; i < tag_count; ++i) {
                auto row = json_object_array_get_idx(tags.get(), i);
                if (field(row, "name") == "doc.last_read_position") {
                    ++position_count;
                    auto cfi = point_cfi(field(row, "value"));
                    if (!cfi.empty()) candidates.insert(cfi);
                }
            }
            if (position_count > 1) ambiguous = true;
            json_object_object_add(metadata.get(), "tags", tags.release());
        }
    } catch (const std::exception& e) {
        put(metadata.get(), "error", e.what());
    }
    json_object_object_add(report.get(), "metadata", metadata.release());
    NativeState result;
    if (ambiguous || candidates.size() > 1)
        result.status = "Multiple profiles or conflicting saved positions; inspect report.";
    else if (candidates.empty())
        result.status = "No supported saved position. Open the probe, close it, then capture.";
    else {
        result.cfi = *candidates.begin();
        result.status = "Point CFI captured. Passage equivalence still needs a device test.";
    }
    put(report.get(), "cfi", result.cfi);
    put(report.get(), "status", result.status);
    result.report = json_object_to_json_string_ext(report.get(), JSON_C_TO_STRING_PRETTY);
    return result;
}

std::string read_text(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path);
    std::string result;
    char c;
    while (input.get(c)) {
        if (result.size() >= 8192) throw std::runtime_error("Text file exceeds 8192 bytes");
        result += c;
    }
    if (input.bad()) throw std::runtime_error("Read failed: " + path);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    return result;
}

void write_text(const std::string& path, const std::string& value) {
    const std::string temporary = path + ".tmp";
    FILE* file = fopen(temporary.c_str(), "wb");
    if (!file) throw std::runtime_error("Cannot write " + temporary);
    bool ok = fwrite(value.data(), 1, value.size(), file) == value.size();
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    if (!ok || rename(temporary.c_str(), path.c_str()) != 0) {
        unlink(temporary.c_str());
        throw std::runtime_error("Could not save " + path);
    }
}

void make_directory(const std::string& path) {
    if (mkdir(path.c_str(), 0700) != 0 && errno != EEXIST)
        throw std::runtime_error("Cannot create " + path);
    struct stat st;
    if (lstat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        throw std::runtime_error("Not a directory: " + path);
}

void snapshot_database(const std::string& source, const std::string& destination) {
    if (source == destination) throw std::runtime_error("Snapshot must not overwrite source");
    const std::vector<std::string> suffixes = {"", "-wal", "-journal"};
    std::vector<FileStamp> before;
    for (const auto& suffix : suffixes) before.push_back(stamp(source + suffix));
    if (!before[0].exists) throw std::runtime_error("Firmware database missing: " + source);
    // Never reuse a shared-memory file left by a previous local snapshot.
    if (unlink((destination + "-shm").c_str()) != 0 && errno != ENOENT)
        throw std::runtime_error("Cannot clear local snapshot shared memory");
    for (size_t i = 0; i < suffixes.size(); ++i) {
        const auto dst = destination + suffixes[i];
        if (before[i].exists) copy_file(source + suffixes[i], dst);
        else if (unlink(dst.c_str()) != 0 && errno != ENOENT)
            throw std::runtime_error("Cannot clear stale local snapshot sidecar");
    }
    for (size_t i = 0; i < suffixes.size(); ++i)
        if (!(before[i] == stamp(source + suffixes[i])))
            throw std::runtime_error("Reader database changed during capture; close the reader and retry");
    // A filesystem copy is a diagnostic snapshot, not a transactional live backup.
    Database copy(destination);
    auto check = query(copy, "PRAGMA quick_check");
    if (json_object_array_length(check.get()) != 1 ||
        field(json_object_array_get_idx(check.get(), 0), "quick_check") != "ok")
        throw std::runtime_error("Snapshot failed SQLite integrity check; retry capture");
}

namespace {
const char* native_identify = "SELECT DISTINCT f.book_id, hex(f.fast_hash) AS hash FROM files f "
    "JOIN folders d ON d.id=f.folder_id AND d.storageid=f.storageid "
    "WHERE f.filename=? AND d.name=?";
std::vector<std::string> path_parameters(const std::string& path) {
    auto slash=path.find_last_of('/');
    if(slash==std::string::npos || slash+1==path.size() || path.find('\0')!=std::string::npos)
        throw std::runtime_error("Invalid native book path");
    return {path.substr(slash+1),path.substr(0,slash)};
}
}
std::vector<NativeBook> native_books(const std::string& snapshot) {
    Database db(snapshot); std::vector<NativeBook> result;
    query_each(db,"SELECT d.name,f.filename,COALESCE(s.position,'') AS position FROM files f "
        "JOIN folders d ON d.id=f.folder_id AND d.storageid=f.storageid "
        "LEFT JOIN books_settings s ON s.bookid=f.book_id ORDER BY d.name,f.filename",{},[&](json_object* row) {
        const auto folder=field(row,"name"),name=field(row,"filename");
        if(folder.empty() || folder[0]!='/' || folder.find('\0')!=std::string::npos || name.find_first_of("/\\")!=std::string::npos || name.find('\0')!=std::string::npos) return;
        auto suffix=name.size()>5?name.substr(name.size()-5):std::string();
        for(auto& c:suffix) if(c>='A' && c<='Z') c+=32;
        if(suffix==".epub") result.push_back({folder+"/"+name,field(row,"position")});
    });
    return result;
}
NativePosition native_position(const std::string& snapshot, const std::string& book_path) {
    Database db(snapshot);
    NativePosition result; result.book_path=book_path;
    auto ids=query(db,native_identify,path_parameters(book_path));
    if(json_object_array_length(ids.get())==0) return result;
    if(json_object_array_length(ids.get())!=1) throw std::runtime_error("Ambiguous native book identity");
    result.indexed=true;
    auto* id=json_object_array_get_idx(ids.get(),0);
    result.book_id=field(id,"book_id"); result.fast_hash=field(id,"hash");
    if(result.fast_hash.size()!=32 || result.fast_hash.find_first_not_of("0123456789ABCDEF")!=std::string::npos)
        throw UnsupportedNativePosition("Unsupported native book fingerprint");
    auto rows=query(db,"SELECT * FROM books_settings WHERE bookid=?",{result.book_id});
    if(json_object_array_length(rows.get())==0) return result;
    if(json_object_array_length(rows.get())!=1) throw UnsupportedNativePosition("Multiple native reading profiles");
    auto* row=json_object_array_get_idx(rows.get(),0);
    result.has_settings=true; result.profile_id=field(row,"profileid");
    result.raw_position=field(row,"position"); result.timestamp=field(row,"position_ts");
    result.cfi=point_cfi(result.raw_position);
    try {
        const auto c=field(row,"cpage"),t=field(row,"npage");
        size_t ca=0,ta=0;
        const auto current=std::stoll(c,&ca),total=std::stoll(t,&ta);
        if(ca==c.size() && ta==t.size() && current>=0 && total>0 && current<=total)
            result.progress="["+std::to_string(current)+","+std::to_string(total)+"]";
    } catch(const std::exception&) { /* Unknown counts do not block position sync. */ }
    if(result.profile_id.empty() || (!result.raw_position.empty() && result.cfi.empty()))
        throw UnsupportedNativePosition("Unsupported native saved position");
    return result;
}
std::map<std::string,double> native_percentages(const std::string& snapshot,
                                               const std::vector<std::string>& paths) {
    Database db(snapshot);
    std::map<std::string,double> result;
    const std::set<std::string> wanted(paths.begin(),paths.end());
    query_each(db,
        "WITH identities AS (SELECT DISTINCT f.book_id,hex(f.fast_hash) AS hash,f.filename,d.name FROM files f "
        "JOIN folders d ON d.id=f.folder_id AND d.storageid=f.storageid), "
        "unique_paths AS (SELECT filename,name,MIN(book_id) AS book_id FROM identities GROUP BY filename,name HAVING COUNT(*)=1), "
        "unique_settings AS (SELECT bookid,MIN(cpage) AS cpage,MIN(npage) AS npage FROM books_settings GROUP BY bookid HAVING COUNT(*)=1) "
        "SELECT p.filename,p.name,s.cpage,s.npage FROM unique_paths p JOIN unique_settings s ON s.bookid=p.book_id", {}, [&](json_object* row) {
        const auto path=field(row,"name")+"/"+field(row,"filename");
        if(!wanted.count(path)) return;
        try {
            const auto c=field(row,"cpage"),t=field(row,"npage");
            size_t ca=0,ta=0;
            const double current=std::stod(c,&ca),total=std::stod(t,&ta);
            if(ca==c.size() && ta==t.size() && std::isfinite(current) && std::isfinite(total) && current>=0 && total>0)
                result[path]=std::min(current/total,1.0)*100;
        } catch(const std::exception&) { /* Unknown page count, not zero progress. */ }
    });
    return result;
}
void backup_native_database(const std::string& path, const std::string& destination) {
    struct stat st;
    if(lstat(path.c_str(),&st)!=0 || !S_ISREG(st.st_mode))
        throw std::runtime_error("Missing or unsafe native database");
    Database source(path);
    int fd=open(destination.c_str(),O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0600);
    if(fd<0) throw std::runtime_error("Native snapshot destination must be new");
    close(fd);
    Database backup(destination,true);
    sqlite3_backup* copy=sqlite3_backup_init(backup.db,"main",source.db,"main");
    if(!copy) throw std::runtime_error("Cannot start native snapshot");
    int step=sqlite3_backup_step(copy,-1), finish=sqlite3_backup_finish(copy);
    if(step!=SQLITE_DONE || finish!=SQLITE_OK)
        throw std::runtime_error("Cannot capture native state; close the book and retry");
    query(backup,"PRAGMA journal_mode=DELETE");
    auto check=query(backup,"PRAGMA quick_check");
    if(json_object_array_length(check.get())!=1 || field(json_object_array_get_idx(check.get(),0),"quick_check")!="ok")
        throw std::runtime_error("Native snapshot failed integrity check");
}
static NativeApplyResult apply_position(const std::string& database, const std::string& directory,
                           const std::string& book_path, const std::string& expected_hash,
                           const std::string& readest_range, const NativePosition* expected,const std::atomic<bool>* cancel=nullptr) {
    // No CREATE on the source, no guessed book id/profile, no whole-row REPLACE.
    struct stat st;
    if (lstat(database.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        throw std::runtime_error("Missing or unsafe native database");
    if (mkdir(directory.c_str(), 0700) != 0)
        throw std::runtime_error("Trial directory must be new");
    const std::string backup_path = directory + "/before.db";
    int fd = open(backup_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) throw std::runtime_error("Cannot create trial backup");
    close(fd);
    Database source(database, true);
    query(source, "PRAGMA synchronous=FULL");
    {
        Database backup(backup_path, true);
        sqlite3_backup* copy = sqlite3_backup_init(backup.db, "main", source.db, "main");
        if (!copy) throw std::runtime_error("Cannot start transactional backup");
        int step = sqlite3_backup_step(copy, -1);
        int finish = sqlite3_backup_finish(copy);
        if (step != SQLITE_DONE || finish != SQLITE_OK)
            throw std::runtime_error("Native database busy; backup failed, no position written");
        // Make the private backup standalone even when the source uses WAL.
        query(backup, "PRAGMA journal_mode=DELETE");
    }
    Database backup(backup_path);
    auto check = query(backup, "PRAGMA quick_check");
    if (json_object_array_length(check.get()) != 1 ||
        field(json_object_array_get_idx(check.get(), 0), "quick_check") != "ok")
        throw std::runtime_error("Trial backup failed integrity check");
    const auto params=path_parameters(book_path);
    auto ids = query(backup, native_identify, params);
    if (json_object_array_length(ids.get()) != 1 ||
        field(json_object_array_get_idx(ids.get(), 0), "hash") != expected_hash)
        throw std::runtime_error("Unexpected test book identity; no position written");
    auto id = field(json_object_array_get_idx(ids.get(), 0), "book_id");
    auto before = query(backup, "SELECT * FROM books_settings WHERE bookid=?", {id});
    if (json_object_array_length(before.get()) != 1)
        throw std::runtime_error("Missing or ambiguous test profile");
    auto row = json_object_array_get_idx(before.get(), 0);
    auto profile = field(row, "profileid");
    if (profile.empty() || (!field(row,"position").empty() && point_cfi(field(row, "position")).empty()))
        throw std::runtime_error("Unsupported current position or timestamp");
    if(expected && (expected->book_id!=id || expected->profile_id!=profile ||
        expected->raw_position!=field(row,"position") || expected->timestamp!=field(row,"position_ts")))
        throw std::runtime_error("Native position changed since synchronization; retry");
    const std::string start = readest_start_cfi(readest_range);
    if (start.empty()) throw std::runtime_error("Unsupported Readest target");
    const std::string target = "pbr:/webkit?##" + start;
    if (point_cfi(field(row, "position")) == point_cfi(target))
        throw std::runtime_error("Native reader is already at the requested position");
    const std::string timestamp = std::to_string(time(nullptr));
    write_text(directory + "/before.json", json_object_to_json_string_ext(before.get(), JSON_C_TO_STRING_PRETTY));
    write_text(directory + "/target.txt", target + "\n" + timestamp + "\n");
    write_text(directory + "/readest-source-cfi.txt", readest_range + "\n");
    query(source, "BEGIN IMMEDIATE");
    bool committing=false;
    try {
        auto current_ids = query(source, native_identify, params);
        auto current = query(source, "SELECT * FROM books_settings WHERE bookid=?", {id});
        if (!same_rows(ids.get(), current_ids.get()) || !same_rows(before.get(), current.get()))
            throw std::runtime_error("Reader state changed since backup; retry after closing book");
        // Permit only the observed completion triggers, which cannot fire here.
        auto triggers = query(source, "SELECT sql FROM sqlite_master WHERE type='trigger' AND tbl_name='books_settings'");
        for (size_t i = 0; i < static_cast<size_t>(json_object_array_length(triggers.get())); ++i) {
            auto sql = field(json_object_array_get_idx(triggers.get(), i), "sql");
            if (sql != "CREATE TRIGGER completed_ts_update AFTER UPDATE ON books_settings WHEN NEW.completed <> OLD.completed BEGIN     UPDATE books_settings SET completed_ts = strftime('%s', 'now') WHERE bookid = NEW.bookid  AND profileid = NEW.profileid; END" &&
                sql != "CREATE TRIGGER completed_ts_insert AFTER INSERT ON books_settings  BEGIN     UPDATE books_settings SET completed_ts = strftime('%s', 'now') WHERE bookid = NEW.bookid  AND profileid = NEW.profileid; END")
                throw std::runtime_error("Unrecognized database trigger; no position written");
        }
        int changes = sqlite3_total_changes(source.db);
        if(cancel && cancel->load()) throw std::runtime_error("Cancelled before native position update.");
        query(source, "UPDATE books_settings SET position=?,position_ts=? WHERE bookid=? AND profileid=? AND CAST(COALESCE(position,'') AS TEXT)=? AND CAST(COALESCE(position_ts,'') AS TEXT)=?",
              {target, timestamp, id, profile, field(row, "position"), field(row, "position_ts")});
        if (sqlite3_changes(source.db) != 1 || sqlite3_total_changes(source.db) != changes + 1)
            throw std::runtime_error("Unexpected affected row count");
        put(row, "position", target);
        put(row, "position_ts", timestamp);
        auto after = query(source, "SELECT * FROM books_settings WHERE bookid=?", {id});
        if (!same_rows(before.get(), after.get()))
            throw std::runtime_error("Unexpected settings changes; rolling back");
        committing=true;
        query(source, "COMMIT");
    } catch (const std::exception& error) {
        const bool ended=sqlite3_get_autocommit(source.db)!=0;
        const int rollback=sqlite3_exec(source.db, "ROLLBACK", nullptr, nullptr, nullptr);
        if((committing && ended) || (!ended && rollback!=SQLITE_OK))
            return {NativeApplyStatus::Uncertain,"Cannot confirm native position commit or rollback. Backup: "+directory+". "+error.what()};
        throw;
    }
    try { write_text(directory + "/committed.txt", "Position committed; verify visible passage.\n"); }
    catch (const std::exception&) {
        return {NativeApplyStatus::Committed,"PocketBook position changed, but the final audit log could not be saved. Backup: "+directory};
    }
    return {};
}
void apply_readest_trial(const std::string& database, const std::string& directory) {
    const auto result=apply_position(database,directory,"/mnt/ext1/Books/Readest/readest-sync-probe.epub",
                   "C7D0E520B24461A476557A4C5381DC2E",
                   "epubcfi(/6/4[bravo]!/4,/2,/12[BRAVO-05]/1:179)",nullptr);
    if(!result.warning.empty()) throw std::runtime_error(result.warning);
}
NativeApplyResult apply_native_position(const std::string& database, const std::string& directory,
                           const NativePosition& expected, const std::string& cfi,
                           const std::string& model, const std::string& firmware,const std::atomic<bool>* cancel) {
    if(model!="PB743G" || firmware!="U743g.6.11.1683")
        throw std::runtime_error("Native position application is not verified on this firmware");
    if(!expected.indexed || !expected.has_settings || expected.fast_hash.size()!=32 ||
        expected.fast_hash.find_first_not_of("0123456789ABCDEF")!=std::string::npos ||
        readest_start_cfi(cfi).empty())
        throw std::runtime_error("Open and close the downloaded book once before applying Readest progress");
    return apply_position(database,directory,expected.book_path,expected.fast_hash,cfi,&expected,cancel);
}
} // namespace readest
