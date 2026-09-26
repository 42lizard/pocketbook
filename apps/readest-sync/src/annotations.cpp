#include "annotations.h"
#include "json_util.h"
#include <openssl/evp.h>
#include <algorithm>
#include <map>
#include <set>
#include <memory>
#include <cctype>

namespace readest {
namespace {
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void check(const std::atomic<bool>& cancel) { require(!cancel.load(),"Annotation sync cancelled"); }
Json object() { return Json(json_object_new_object(),json_object_put); }
void put(json_object* o,const char* k,const std::string& s) { json_object_object_add(o,k,json_object_new_string_len(s.data(),s.size())); }
void put(json_object* o,const char* k,long long n) { json_object_object_add(o,k,json_object_new_int64(n)); }
std::string optional(json_object* o,const char* k) { return member(o,k)?string_member(o,k):""; }
std::string hash(const std::string& s) {
    unsigned char bytes[EVP_MAX_MD_SIZE]; unsigned size=0;
    require(EVP_Digest(s.data(),s.size(),bytes,&size,EVP_sha256(),nullptr)==1,"Cannot hash annotation identity");
    static const char hex[]="0123456789abcdef"; std::string out;
    for(unsigned i=0;i<size;++i) {out+=hex[bytes[i]>>4];out+=hex[bytes[i]&15];} return out;
}
std::string normalized_text(const std::string& s) {
    std::string out;
    for(unsigned char c:s) {
        if(c==' ' || c=='\n' || c=='\r' || c=='\t' || c=='\f') {if(!out.empty() && out.back()!=' ') out+=' ';}
        else out+=static_cast<char>(c);
    }
    if(!out.empty() && out.back()==' ') out.pop_back(); return out;
}
std::string point(std::string s) {
    const auto start=s.find("epubcfi(");
    if(start!=std::string::npos) s=s.substr(start);
    require(!point_cfi(s).empty(),"Unsupported native annotation location");
    // Assertions and explicit zero offsets are not changes to a resolved range.
    std::string out;
    for(size_t i=0;i<s.size();++i) {
        if(s[i]=='[') {while(++i<s.size() && s[i]!=']') if(s[i]=='^') ++i;}
        else out+=s[i];
    }
    if(out.size()>3 && out.substr(out.size()-3)==":0)") out.erase(out.size()-3,2);
    return out;
}
struct Note {
    std::string id,begin,end,text,note,color="yellow",style="highlight",meta;
    long long created=0,updated=0,deleted=0;
};
std::string fingerprint(const Note& n) {
    if(n.deleted) return "deleted";
    auto o=object(); put(o.get(),"begin",point(n.begin)); put(o.get(),"end",point(n.end));
    put(o.get(),"text",normalized_text(n.text)); put(o.get(),"note",n.note);
    put(o.get(),"color",n.color); put(o.get(),"style",n.style); return hash(json_text(o.get()));
}
std::string fingerprint(const std::map<std::string,Note>& notes,const std::string& id) {
    auto it=notes.find(id); return it==notes.end()?"deleted":fingerprint(it->second);
}
std::string range(const Note& n) {
    auto a=point(n.begin), b=point(n.end); const auto bang=a.find('!');
    require(a.substr(0,bang+1)==b.substr(0,b.find('!')+1),"Annotation spans different chapters");
    auto slash=a.find('/',bang+2);
    require(slash!=std::string::npos && b.compare(0,slash,a.substr(0,slash))==0,"Unsupported annotation common path");
    auto out=a.substr(0,slash)+","+a.substr(slash,a.size()-slash-1)+","+b.substr(slash);
    require(!readest_range_cfi(out).first.empty(),"Cannot encode annotation range"); return out;
}
void validate(const Note& n,const std::string& epub) {
    if(n.deleted) return;
    require(n.style=="highlight","Unsupported annotation style; nothing overwritten");
    require(n.color=="yellow" || n.color=="#00bcd4","Unsupported annotation color; nothing overwritten");
    require(n.text.size()<=65536 && n.note.size()<=65536 && n.note.find('\0')==std::string::npos,"Invalid annotation text");
    validate_annotation_range(epub,n.begin,n.end,n.text);
}
struct DB {
    sqlite3* db=nullptr;
    DB(const std::string& path,bool write) {
        if(sqlite3_open_v2(path.c_str(),&db,write?SQLITE_OPEN_READWRITE:SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK) {
            sqlite3_close(db); db=nullptr; throw std::runtime_error("Cannot open native annotations database");
        }
        sqlite3_busy_timeout(db,3000);
    }
    ~DB(){sqlite3_close(db);}
    void exec(const char* sql) {require(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)==SQLITE_OK,"Native annotation transaction failed");}
};
struct Query {
    sqlite3_stmt* s=nullptr;
    Query(DB& db,const char* sql,const std::vector<std::string>& args={}) {
        require(sqlite3_prepare_v2(db.db,sql,-1,&s,nullptr)==SQLITE_OK,"Unsupported native annotation schema");
        for(size_t i=0;i<args.size();++i) if(sqlite3_bind_text(s,i+1,args[i].data(),args[i].size(),SQLITE_TRANSIENT)!=SQLITE_OK) {
            sqlite3_finalize(s);s=nullptr;throw std::runtime_error("Cannot bind annotation query");
        }
    }
    ~Query(){sqlite3_finalize(s);}
    bool row() {int rc=sqlite3_step(s);require(rc==SQLITE_ROW || rc==SQLITE_DONE,"Native annotation query failed");return rc==SQLITE_ROW;}
    std::string text(int i) {auto* p=sqlite3_column_text(s,i);int n=sqlite3_column_bytes(s,i);require(n<=131072,"Oversized native annotation");return p?std::string(reinterpret_cast<const char*>(p),n):"";}
};
struct Snapshot { std::string book,raw;std::map<std::string,Note> notes; };
Snapshot snapshot(DB& db,const std::string& fast_hash) {
    require(!fast_hash.empty(),"Native annotation book identity missing");
    Snapshot out;
    Query book(db,"SELECT OID FROM Items WHERE TypeID=(SELECT OID FROM TypeNames WHERE TypeName='type.book') AND upper(HashUUID)=upper(?)",{fast_hash});
    require(book.row(),"Open this book once in the native reader before syncing annotations");out.book=book.text(0);
    require(!book.row(),"Ambiguous native annotation book identity");
    Query rows(db,"SELECT i.OID,i.HashUUID,i.State,i.TimeAlt,n.TagName,t.Val,t.TimeEdt FROM Items i "
        "LEFT JOIN Tags t ON t.ItemID=i.OID LEFT JOIN TagNames n ON n.OID=t.TagID "
        "WHERE i.ParentID=? ORDER BY i.OID,n.TagName",{out.book});
    std::map<std::string,std::map<std::string,std::string>> tags;
    std::map<std::string,Note> all;std::map<std::string,std::string> native_ids;
    Json raw(json_object_new_array(),json_object_put);
    size_t count=0;
    while(rows.row()) {
        require(++count<=50000,"Too many native annotations");
        Json row(json_object_new_array(),json_object_put);
        for(int i=0;i<7;++i) json_object_array_add(row.get(),json_object_new_string(rows.text(i).c_str()));
        json_object_array_add(raw.get(),row.release());
        const auto id=rows.text(1);require(!id.empty(),"Native annotation has no UUID");
        auto [identity,inserted]=native_ids.emplace(id,rows.text(0));
        require(inserted || identity->second==rows.text(0),"Duplicate native annotation UUID");
        auto& n=all[id];n.id=id;
        require(rows.text(2)=="0" || rows.text(2)=="2","Unknown native annotation state");
        auto seconds=rows.text(3);
        require(!seconds.empty() && seconds.size()<=12 && seconds.find_first_not_of("0123456789")==std::string::npos,"Invalid native annotation timestamp");
        n.updated=std::stoll(seconds)*1000; n.deleted=rows.text(2)=="2"?std::max(1LL,n.updated):0;
        tags[id][rows.text(4)]=rows.text(5);
    }
    out.raw=hash(out.book+json_text(raw.get()));
    for(auto& [id,n]:all) {
        const auto& t=tags.at(id); auto type=t.find("bm.type");
        if(type==t.end() || (type->second!="note" && type->second!="highlight")) {
            require(!t.count("bm.quotation"),"Unsupported native annotation type; nothing overwritten");continue;
        }
        if(n.deleted) {out.notes[id]=n;continue;}
        auto quote=parse_json(t.at("bm.quotation")); auto bookmark=parse_json(t.at("bm.book_mark"));
        auto location=[](std::string s) {auto at=s.find("epubcfi(");require(at!=std::string::npos,"Unsupported native annotation anchor");return s.substr(at);};
        n.begin=location(string_member(quote.get(),"begin"));n.end=location(string_member(quote.get(),"end"));
        n.text=string_member(quote.get(),"text");
        n.created=integer_member(bookmark.get(),"created");
        require(n.created>0 && n.created<=999999999999LL,"Invalid native annotation creation time");n.created*=1000;
        auto note=t.find("bm.note");if(note!=t.end()) {auto o=parse_json(note->second);n.note=optional(o.get(),"text");}
        auto color=t.find("bm.color");require(color!=t.end(),"Native annotation color missing");n.color=color->second=="cian"?"#00bcd4":color->second;
        auto subtype=t.find("bm.subtype");require(subtype==t.end() || subtype->second.empty(),"Unsupported native annotation subtype");
        out.notes[id]=n;
    }
    return out;
}
Snapshot read_native(const std::string& database,const std::string& fast_hash) {
    DB db(database,false); db.exec("BEGIN");auto result=snapshot(db,fast_hash);db.exec("COMMIT");return result;
}
std::map<std::string,Note> remote_notes(const std::string& body,const std::string& user,const std::string& book,const std::string& epub) {
    auto json=parse_json(body);auto* rows=member(json.get(),"notes");
    require(rows && json_object_get_type(rows)==json_type_array && json_object_array_length(rows)<=5000,"Invalid Readest annotations response");
    std::map<std::string,Note> notes;
    for(size_t i=0;i<json_object_array_length(rows);++i) {
        auto* row=json_object_array_get_idx(rows,i);
        require(string_member(row,"user_id")==user && string_member(row,"book_hash")==book,"Readest annotation account/book mismatch");
        if(string_member(row,"type")!="annotation") continue;
        Note n;n.id=string_member(row,"id");
        require(!n.id.empty() && n.id.size()<=128 && n.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==std::string::npos,"Invalid Readest annotation ID");
        n.created=record_timestamp(row,"created_at");n.updated=record_timestamp(row,"updated_at");n.deleted=record_timestamp(row,"deleted_at");
        require(n.updated>0,"Missing Readest annotation timestamp");
        if(!n.deleted) {
            const auto cfi=optional(row,"cfi");auto endpoints=readest_range_cfi(cfi);
            if(endpoints.first.empty() && cfi.empty()) endpoints={xpointer_cfi(epub,string_member(row,"xpointer0")),xpointer_cfi(epub,string_member(row,"xpointer1"))};
            require(!endpoints.first.empty(),"Unsupported Readest annotation range");n.begin=endpoints.first;n.end=endpoints.second;
            n.text=string_member(row,"text");n.note=optional(row,"note");n.color=string_member(row,"color");n.style=string_member(row,"style");
            n.meta=optional(row,"meta_hash");
            validate(n,epub);
        }
        require(notes.emplace(n.id,n).second,"Duplicate Readest annotation ID");
    }
    return notes;
}
std::string payload(const Note& n,const std::string& book) {
    auto o=object();put(o.get(),"id",n.id);put(o.get(),"type",std::string("annotation"));put(o.get(),"bookHash",book);
    put(o.get(),"createdAt",n.created);put(o.get(),"updatedAt",n.updated);
    if(!n.meta.empty()) put(o.get(),"metaHash",n.meta);
    if(n.deleted) put(o.get(),"deletedAt",n.deleted);
    else {
        put(o.get(),"cfi",range(n));put(o.get(),"text",n.text);put(o.get(),"note",n.note);
        put(o.get(),"style",n.style);put(o.get(),"color",n.color);
    }
    return json_text(o.get());
}
void apply_native(const std::string& database,const std::string& fast_hash,const Snapshot& expected,
                  const std::string& uuid,const Note& note,const std::atomic<bool>& cancel) {
    check(cancel);DB db(database,true); db.exec("PRAGMA foreign_keys=ON; PRAGMA synchronous=FULL; BEGIN IMMEDIATE");
    try {
        require(snapshot(db,fast_hash).raw==expected.raw,"Annotations changed on PocketBook during sync; retry");
        Query triggers(db,"SELECT 1 FROM sqlite_master WHERE type='trigger' AND tbl_name IN ('Items','Tags','TagNames','TypeNames')");
        require(!triggers.row(),"Unrecognized native annotation triggers");
        std::string item;
        Query existing(db,"SELECT OID FROM Items WHERE ParentID=? AND HashUUID=?",{expected.book,uuid});
        if(existing.row()) {item=existing.text(0);require(!existing.row(),"Ambiguous native annotation UUID");}
        check(cancel);
        auto stamp=std::to_string(std::max(note.updated,note.deleted)/1000);
        if(note.deleted) {
            if(!item.empty()) Query(db,"UPDATE Items SET State=2,TimeAlt=? WHERE OID=?",{stamp,item}).row();
        } else {
            if(item.empty()) {
                Query type(db,"SELECT OID FROM TypeNames WHERE TypeName='obj.book_mark'");require(type.row(),"Missing native annotation type");
                Query(db,"INSERT INTO Items(ParentID,TypeID,State,TimeAlt,HashUUID) VALUES(?,?,0,?,?)",{expected.book,type.text(0),stamp,uuid}).row();
                item=std::to_string(sqlite3_last_insert_rowid(db.db));
            } else Query(db,"UPDATE Items SET State=0,TimeAlt=? WHERE OID=?",{stamp,item}).row();
            auto quote=object(),anchor=object(),comment=object();
            put(quote.get(),"begin","pbr:/webkit?##"+note.begin);put(quote.get(),"end","pbr:/webkit?##"+note.end);put(quote.get(),"text",note.text);
            put(anchor.get(),"anchor","pbr:/webkit?##"+note.begin);put(anchor.get(),"created",note.created/1000);put(comment.get(),"text",note.note);
            for(const auto& [name,value]:std::map<std::string,std::string>{{"bm.type",note.note.empty()?"highlight":"note"},
                    {"bm.color",note.color=="#00bcd4"?"cian":note.color},{"bm.note",json_text(comment.get())},
                    {"bm.book_mark",json_text(anchor.get())},{"bm.quotation",json_text(quote.get())}}) {
                Query tag(db,"SELECT OID FROM TagNames WHERE TagName=?",{name});require(tag.row(),"Missing native annotation tag");
                Query(db,"INSERT OR REPLACE INTO Tags(ItemID,TagID,Val,TimeEdt) VALUES(?,?,?,?)",{item,tag.text(0),value,stamp}).row();
            }
        }
        // Do not report success or advance the baseline until COMMIT succeeds.
        db.exec("COMMIT");
    } catch(...) {sqlite3_exec(db.db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
} // namespace

void sync_annotations(Cloud& cloud,State& state,const VerifiedManagedBook& verified,
    const NativePosition& native,const std::string& database,long long now,
    const std::string& model,const std::string& firmware,const std::atomic<bool>& cancel) {
    require(model=="PB743G" && firmware=="U743g.6.11.1683","Annotation sync is not verified on this firmware");
    require(now>0 && now<0x7fffffffffffffffLL/1000-1,"Invalid annotation sync clock");
    const auto& book=verified.book();const auto user=cloud.session().user_id;
    require(!user.empty() && !book.local_only && !book.book.deleted && native.book_path==book.path,"Invalid annotation sync book");
    check(cancel);
    auto local=read_native(database,native.fast_hash);
    for(const auto& [id,n]:local.notes) { (void)id;validate(n,book.path); }
    const auto response=cloud.get("/api/sync?type=notes&since=0&book="+book.book.hash,now);
    require(response.status==200,"Cannot fetch Readest annotations");
    auto remote=remote_notes(response.body,user,book.book.hash,book.path);
    struct Entry {std::string local,base_local="deleted",base_remote="deleted",pending,pending_local;long long remote_stamp=0;};
    std::map<std::string,Entry> entries;std::set<std::string> used;
    auto saved=parse_json(state.annotations(user,book.book.hash,book.path,book.sha256));auto* records=member(saved.get(),"entries");
    require(records && json_object_get_type(records)==json_type_array && json_object_array_length(records)<=5000,"Invalid saved annotation journal");
    for(size_t i=0;i<json_object_array_length(records);++i) {
        auto* r=json_object_array_get_idx(records,i);Entry e;
        e.local=string_member(r,"local");e.base_local=string_member(r,"baseLocal");e.base_remote=string_member(r,"baseRemote");
        e.remote_stamp=member(r,"remoteStamp")?integer_member(r,"remoteStamp"):0;
        e.pending=optional(r,"pending");e.pending_local=optional(r,"pendingLocal");
        require(used.insert(e.local).second && entries.emplace(string_member(r,"remote"),e).second,"Duplicate saved annotation mapping");
    }
    auto save=[&] {
        auto data=object();Json list(json_object_new_array(),json_object_put);
        for(const auto& [id,e]:entries) {
            auto row=object();put(row.get(),"remote",id);put(row.get(),"local",e.local);
            put(row.get(),"baseLocal",e.base_local);put(row.get(),"baseRemote",e.base_remote);
            put(row.get(),"remoteStamp",e.remote_stamp);
            put(row.get(),"pending",e.pending);put(row.get(),"pendingLocal",e.pending_local);
            json_object_array_add(list.get(),row.release());
        }
        json_object_object_add(data.get(),"entries",list.release());
        state.save_annotations(user,book.book.hash,book.path,book.sha256,json_text(data.get()));
    };
    for(const auto& [id,n]:remote) {
        if(entries.count(id) || n.deleted) continue;
        Entry e;
        // Adopt a previously imported identical record, including the device trial.
        for(const auto& [uuid,l]:local.notes) if(!l.deleted && !used.count(uuid) && fingerprint(l)==fingerprint(n)) {
            require(e.local.empty(),"Ambiguous identical local annotations");e.local=uuid;
        }
        if(e.local.empty()) {
            auto h=hash(user+":"+book.book.hash+":"+id).substr(0,32);
            e.local=h.substr(0,8)+"-"+h.substr(8,4)+"-"+h.substr(12,4)+"-"+h.substr(16,4)+"-"+h.substr(20);
            std::transform(e.local.begin(),e.local.end(),e.local.begin(),[](unsigned char c){return std::toupper(c);});
            require(!local.notes.count(e.local),"Native annotation identity collision");
        } else {e.base_local=e.base_remote=fingerprint(n);e.remote_stamp=std::max(n.updated,n.deleted);}
        require(used.insert(e.local).second,"Annotation mapping collision");entries.emplace(id,e);
    }
    for(const auto& [uuid,n]:local.notes) {
        if(n.deleted || used.count(uuid)) continue;
        const auto id=hash(user+":"+book.book.hash+":"+book.path+":"+uuid).substr(0,32);
        require(!entries.count(id) && !remote.count(id),"Readest annotation identity collision");
        Entry e;e.local=uuid;entries[id]=e;used.insert(uuid);
    }
    // Mappings are durable before either side is changed.
    save();
    for(auto& [id,e]:entries) {
        check(cancel);
        auto observed=remote.find(id);
        require(!e.remote_stamp || observed!=remote.end(),"Readest annotation disappeared without a deletion marker");
        const auto remote_stamp=observed==remote.end()?0:std::max(observed->second.updated,observed->second.deleted);
        require(remote_stamp>=e.remote_stamp,"Stale Readest annotation response; retry");
        auto lf=fingerprint(local.notes,e.local),rf=fingerprint(remote,id);
        if(lf==rf) {
            e.base_local=lf;e.base_remote=rf;e.remote_stamp=remote_stamp;e.pending.clear();e.pending_local.clear();save();continue;
        }
        if(!e.pending.empty()) {
            // An ambiguous HTTP failure may already have committed remotely.
            if(rf==e.pending_local) {
                e.base_local=e.pending_local;e.base_remote=rf;e.remote_stamp=remote_stamp;e.pending.clear();e.pending_local.clear();save();
            } else require(rf==e.base_remote,"Annotation changed in Readest during a pending upload; resolve the conflicting edits");
        }
        if(e.pending.empty()) {
            const bool lc=lf!=e.base_local,rc=rf!=e.base_remote;
            require(!(lc && rc),"Annotation changed on both readers; resolve the conflicting notes before syncing");
            if(!lc && !rc) continue;
            if(rc) {
                auto it=remote.find(id);require(it!=remote.end(),"Readest annotation disappeared without a deletion marker");
                apply_native(database,native.fast_hash,local,e.local,it->second,cancel);
                local=read_native(database,native.fast_hash);
                require(fingerprint(local.notes,e.local)==rf,"Native annotation write could not be verified");
                e.base_local=rf;e.base_remote=rf;e.remote_stamp=remote_stamp;save();continue;
            }
            Note n;
            auto found=local.notes.find(e.local);
            if(found!=local.notes.end()) n=found->second;
            else n.deleted=now*1000;
            n.id=id;
            const auto old=remote.find(id);
            if(old!=remote.end()) {n.meta=old->second.meta;n.created=old->second.created;}
            const auto stamp=old==remote.end()?0:std::max(old->second.updated,old->second.deleted);
            require(stamp<0x7fffffffffffffffLL-1,"Invalid remote annotation timestamp");
            n.updated=std::max(now*1000,stamp+1);
            if(n.created<=0) n.created=n.updated;
            if(lf=="deleted") n.deleted=n.updated;
            e.pending=payload(n,book.book.hash);e.pending_local=lf;save();
        }
        check(cancel);
        require(read_native(database,native.fast_hash).raw==local.raw,"Annotations changed on PocketBook during sync; retry");
        // Recheck the remote record before a write; the API has no conditional update.
        auto fresh=cloud.get("/api/sync?type=notes&since=0&book="+book.book.hash,now);
        require(fresh.status==200,"Cannot recheck Readest annotations; saved for retry");
        auto latest=remote_notes(fresh.body,user,book.book.hash,book.path);
        require(fingerprint(latest,id)==rf,"Annotation changed in Readest before upload; retry");
        check(cancel);
        // Retry the same body and ID. Server-authoritative data must acknowledge it.
        auto posted=cloud.post("/api/sync","{\"books\":[],\"configs\":[],\"notes\":["+e.pending+"]}",now);
        require(posted.status==200,"Cannot upload annotations; saved for retry");
        auto accepted=remote_notes(posted.body,user,book.book.hash,book.path);
        auto winner=accepted.find(id);
        require(winner!=accepted.end() && fingerprint(winner->second)==e.pending_local,"Readest did not accept the annotation; saved for reconciliation");
        remote[id]=winner->second;e.base_local=e.pending_local;e.base_remote=e.pending_local;
        e.remote_stamp=std::max(winner->second.updated,winner->second.deleted);
        e.pending.clear();e.pending_local.clear();save();
        local=read_native(database,native.fast_hash);
        require(fingerprint(local.notes,e.local)==e.base_local,"Newer PocketBook annotation edits remain; sync again");
    }
}
} // namespace readest
