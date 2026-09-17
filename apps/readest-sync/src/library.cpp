#include "library.h"
#include "json_util.h"
#include <algorithm>
#include <ctime>
#include <set>
#include <cctype>

namespace readest {
long long record_timestamp(json_object* row, const char* key) {
    auto* value = member(row, key);
    if (!value) return 0;
    if (json_object_get_type(value) == json_type_int) {
        auto n = json_object_get_int64(value);
        if (n < 0) throw std::runtime_error("Invalid library timestamp");
        return n;
    }
    const auto s = string_member(row, key);
    // Accept ISO/Postgres numeric offsets; interpret independently of device timezone.
    auto normalized=s;
    if(normalized.size()>10 && normalized[10]==' ') normalized[10]='T';
    struct tm utc = {};
    char* end = strptime(normalized.c_str(), "%Y-%m-%dT%H:%M:%S", &utc);
    if (!end || end - normalized.c_str() != 19) throw std::runtime_error("Invalid library timestamp");
    size_t at = 19;
    int ms = 0, digits = 0;
    if (at < s.size() && s[at] == '.') {
        ++at;
        while (at < s.size() && s[at] >= '0' && s[at] <= '9') {
            if (digits < 3) ms = ms * 10 + s[at] - '0';
            ++digits; ++at;
        }
        if (!digits || digits > 6) throw std::runtime_error("Invalid library timestamp fraction");
        while (digits++ < 3) ms *= 10;
    }
    int offset_minutes=0;
    const auto zone=s.substr(at);
    if(zone!="Z" && zone!="z") {
        auto digit=[](char c){ return c>='0' && c<='9'; };
        if((zone.size()!=3 && zone.size()!=5 && zone.size()!=6) ||
            (zone[0]!='+' && zone[0]!='-') || !digit(zone[1]) || !digit(zone[2]))
            throw std::runtime_error("Invalid library timestamp timezone");
        const int hours=(zone[1]-'0')*10+zone[2]-'0';
        int minutes=0;
        if(zone.size()>3) {
            const size_t m=zone.size()==6?4:3;
            if((zone.size()==6 && zone[3]!=':') || !digit(zone[m]) || !digit(zone[m+1]))
                throw std::runtime_error("Invalid library timestamp timezone");
            minutes=(zone[m]-'0')*10+zone[m+1]-'0';
        }
        if(hours>23 || minutes>59) throw std::runtime_error("Invalid library timestamp timezone");
        offset_minutes=(hours*60+minutes)*(zone[0]=='-'?-1:1);
    }
    char checked[32];
    const auto seconds = timegm(&utc);
    strftime(checked, sizeof(checked), "%Y-%m-%dT%H:%M:%S", &utc);
    if (seconds < 0 || normalized.substr(0, 19) != checked)
        throw std::runtime_error("Out-of-range library timestamp");
    const auto utc_seconds=static_cast<long long>(seconds)-offset_minutes*60LL;
    if(utc_seconds<0) throw std::runtime_error("Out-of-range library timestamp");
    return utc_seconds*1000+ms;
}
namespace {
std::string optional_string(json_object* o, const char* key) {
    return member(o, key) ? string_member(o, key) : "";
}
bool hash_valid(const std::string& hash) {
    if (hash.size() != 32) return false;
    return hash.find_first_not_of("0123456789abcdef") == std::string::npos;
}
}

LibraryPage parse_library_page(const std::string& response, const std::string& user,
                               long long since, size_t limit) {
    if (user.empty() || since < 0 || limit > 1000)
        throw std::runtime_error("Invalid library request");
    auto json = parse_json(response);
    auto* rows = member(json.get(), "books");
    if (!rows || json_object_get_type(rows) != json_type_array)
        throw std::runtime_error("Invalid library response");
    LibraryPage page;
    page.cursor = since;
    const size_t count = json_object_array_length(rows);
    std::set<std::string> seen;
    for (size_t i = 0; i < count; ++i) {
        auto* row = json_object_array_get_idx(rows, i);
        if (string_member(row, "user_id") != user)
            throw std::runtime_error("Library account identity mismatch");
        LibraryBook book;
        book.hash = string_member(row, "book_hash");
        if (!hash_valid(book.hash) || !seen.insert(book.hash).second)
            throw std::runtime_error("Invalid or duplicate library identity");
        book.deleted = record_timestamp(row, "deleted_at") > 0;
        book.title = optional_string(row, "title");
        book.author = optional_string(row, "author");
        book.format = optional_string(row, "format");
        // Never let a fast client clock advance past server-stamped changes.
        book.cursor = member(row, "synced_at") ? record_timestamp(row, "synced_at") :
            std::max(record_timestamp(row, "updated_at"), record_timestamp(row, "deleted_at"));
        page.cursor = std::max(page.cursor, book.cursor);
        book.raw = json_text(row);
        page.books.push_back(book); // Keep tombstones to update cached metadata.
    }
    page.more = limit != 0 && count >= limit;
    // Prevent infinite paging with a stale/unsupported cursor contract. Do not
    // invent +1: that can skip records sharing a timestamp boundary.
    if (page.more && page.cursor <= since)
        throw std::runtime_error("Library cursor did not advance; retry full library refresh");
    return page;
}

LibraryPage fetch_library_page(Cloud& cloud, long long since, size_t limit, long long now,
                               const std::string& hash) {
    if (since < 0 || limit == 0 || limit > 1000) throw std::runtime_error("Invalid library request");
    if (!hash.empty() && !hash_valid(hash)) throw std::runtime_error("Invalid library book hash");
    for (;;) {
        auto response = cloud.get("/api/sync?type=books&since=" + std::to_string(since) +
                                  "&limit=" + std::to_string(limit) +
                                  (hash.empty() ? "" : "&book=" + hash), now);
        if (response.status != 200)
            throw std::runtime_error("Library request failed (HTTP " + std::to_string(response.status) + ")");
        auto page=parse_library_page(response.body,cloud.session().user_id,since,0);
        page.more=page.books.size()>=limit;
        // Readest completes the trailing exact synced_at tie in its limited response.
        // Keep the whole response, including rows beyond the requested limit.
        if(!page.more || page.cursor>since) return page;
        // Fractional timestamps round down to the same millisecond cursor. Look past
        // those repeated rows at the SAME cursor, never request the entire delta or
        // round the cursor up (which could skip other rows in this millisecond).
        if(limit==1000 || page.books.size()>=1000)
            throw std::runtime_error("Library timestamp boundary exceeds the supported page size; cursor was not advanced");
        limit=std::min<size_t>(1000,std::max(limit*2,page.books.size()+1));
    }
}
std::map<std::string,BookFiles> fetch_book_files(Cloud& cloud,long long now,const std::string& hash) {
    if(!hash.empty() && !hash_valid(hash)) throw std::runtime_error("Invalid storage book hash");
    std::map<std::string,BookFiles> result;
    std::map<std::string,std::string> seen;
    for(int page=1;page<=100;++page) {
        auto response=cloud.get("/api/storage/list?page="+std::to_string(page)+"&pageSize=1000"+
                                (hash.empty()?"":"&bookHash="+hash),now);
        if(response.status!=200) throw std::runtime_error("Could not check downloadable books (HTTP "+std::to_string(response.status)+"). Refresh to retry.");
        auto json=parse_json(response.body); auto* files=member(json.get(),"files");
        const auto pages=integer_member(json.get(),"totalPages");
        if(!files || json_object_get_type(files)!=json_type_array ||
            integer_member(json.get(),"page")!=page || pages<0 || pages>100)
            throw std::runtime_error("Invalid book storage listing");
        for(size_t i=0;i<static_cast<size_t>(json_object_array_length(files));++i) {
            auto* file=json_object_array_get_idx(files,i);
            if(!member(file,"book_hash")) continue; // Non-book replicas.
            const auto hash=string_member(file,"book_hash"), key=string_member(file,"file_key");
            const auto size=integer_member(file,"file_size");
            if(!hash_valid(hash) || key.empty() || key.size()>4096 || key.find('\0')!=std::string::npos || size<0)
                throw std::runtime_error("Invalid stored book identity");
            const auto descriptor=hash+":"+std::to_string(size);
            auto previous=seen.find(key);
            if(previous!=seen.end()) {
                if(previous->second!=descriptor) throw std::runtime_error("Storage changed during refresh; retry.");
                continue; // Related files can appear on multiple pages.
            }
            seen[key]=descriptor;
            auto& book=result[hash];
            auto lower=key; for(auto& c:lower) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if(lower.size()>=5 && lower.substr(lower.size()-5)==".epub") {
                if(size<=0 || size>256LL*1024*1024) book.epubs=-2;
                else if(book.epubs>=0) ++book.epubs;
            }
            if(lower.size()>=10 && lower.substr(lower.size()-10)=="/cover.png" && size>0 && size<=2*1024*1024) {
                long long stamp=0;
                try { stamp=record_timestamp(file,"updated_at"); }
                catch(const std::exception&) { continue; } // Optional cover metadata cannot block EPUB availability.
                if(book.cover_key.empty() || stamp>book.cover_stamp) {
                    book.cover_key=key; book.cover_size=size; book.cover_stamp=stamp;
                }
            }
        }
        if(page>=pages) return result;
    }
    throw std::runtime_error("Book storage listing is too large");
}
} // namespace readest
