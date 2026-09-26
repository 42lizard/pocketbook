#include "annotations.h"
#include "json_util.h"
#include <cassert>
#include <functional>
#include <iostream>

using namespace readest;
namespace {
void sql(sqlite3* db,const std::string& query) {assert(sqlite3_exec(db,query.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK);}
std::string scalar(sqlite3* db,const char* query) {
    sqlite3_stmt* s=nullptr;assert(sqlite3_prepare_v2(db,query,-1,&s,nullptr)==SQLITE_OK);
    assert(sqlite3_step(s)==SQLITE_ROW);auto* p=sqlite3_column_text(s,0);std::string result=p?reinterpret_cast<const char*>(p):"";sqlite3_finalize(s);return result;
}
void set(json_object* o,const char* key,const std::string& value) {json_object_object_add(o,key,json_object_new_string(value.c_str()));}
void set(json_object* o,const char* key,long long value) {json_object_object_add(o,key,json_object_new_int64(value));}
void fails(const std::function<void()>& f,const std::string& message) {
    try {f();} catch(const std::exception& e) {if(std::string(e.what()).find(message)==std::string::npos) {std::cerr<<"Expected "<<message<<", got "<<e.what()<<"\n";abort();}return;}
    std::cerr<<"Expected failure: "<<message<<"\n";abort();
}
}
int main(int argc,char** argv) {
    assert(argc==3);const std::string root=argv[1],epub=argv[2];
    auto integrity=inspect_epub(epub);ManagedBook book;book.book.hash=integrity.readest_hash;book.path=epub;book.size=integrity.size;book.sha256=integrity.sha256;
    VerifiedManagedBook verified(book);NativePosition native;native.book_path=epub;native.fast_hash="AABB";
    const auto database=root+"/annotations-native.db";sqlite3* db=nullptr;assert(sqlite3_open(database.c_str(),&db)==SQLITE_OK);
    sql(db,"CREATE TABLE TypeNames(OID INTEGER PRIMARY KEY,TypeName TEXT UNIQUE);"
        "CREATE TABLE TagNames(OID INTEGER PRIMARY KEY,TagName TEXT UNIQUE);"
        "CREATE TABLE Items(OID INTEGER PRIMARY KEY,ParentID INTEGER,TypeID INTEGER,State INTEGER DEFAULT 0,TimeAlt INTEGER,HashUUID TEXT);"
        "CREATE TABLE Tags(OID INTEGER PRIMARY KEY,ItemID INTEGER REFERENCES Items(OID),TagID INTEGER REFERENCES TagNames(OID),Val TEXT,TimeEdt INTEGER,UNIQUE(ItemID,TagID));"
        "INSERT INTO TypeNames VALUES(0,'type.book'),(4,'obj.book_mark');"
        "INSERT INTO TagNames VALUES(101,'bm.type'),(104,'bm.quotation'),(105,'bm.note'),(106,'bm.color'),(107,'bm.book_mark');"
        "INSERT INTO Items VALUES(1,NULL,0,0,0,'AABB');");
    std::map<std::string,std::string> server;
    int posts=0,gets=0;bool fail_post=false,lose_response=false,server_wins=false,race=false;std::string last_post;
    auto wire=[&] {std::string body="{\"notes\":[";for(const auto& [id,row]:server){(void)id;if(body.back()!= '[')body+=',';body+=row;}return body+"]}";};
    auto api=[&](const std::string& url,const std::string& method,const std::vector<std::string>&,const std::string& body,const std::string&,size_t) {
        if(url.find("grant_type=password")!=std::string::npos) return HttpResponse{200,R"({"access_token":"test","refresh_token":"test-refresh","expires_at":999999,"user":{"id":"user"}})",0};
        if(method=="GET") {assert(url=="https://api.test/api/sync?type=notes&since=0&book="+integrity.readest_hash);++gets;
            if(race) sql(db,"UPDATE Items SET TimeAlt=TimeAlt+1 WHERE OID=1+1");
            return HttpResponse{200,wire(),0};}
        assert(url=="https://api.test/api/sync");++posts;last_post=body;
        if(fail_post) return HttpResponse{503,"{}",0};
        auto json=parse_json(body);auto* list=member(json.get(),"notes");assert(json_object_array_length(list)==1);
        auto* in=json_object_array_get_idx(list,0);const auto id=string_member(in,"id");
        auto out=parse_json(server.count(id)?server.at(id):"{}");
        set(out.get(),"user_id",std::string("user"));set(out.get(),"book_hash",integrity.readest_hash);
        if(!server_wins) {
            for(const auto& [a,b]:std::map<std::string,std::string>{{"id","id"},{"type","type"},{"cfi","cfi"},{"text","text"},{"note","note"},{"color","color"},{"style","style"},
                    {"createdAt","created_at"},{"updatedAt","updated_at"},{"deletedAt","deleted_at"}}) {
                auto* v=member(in,a.c_str());json_object_object_add(out.get(),b.c_str(),v?json_object_get(v):nullptr);
            }
            server[id]=json_text(out.get());
        }
        if(lose_response) throw std::runtime_error("lost response");
        return HttpResponse{200,"{\"notes\":["+server.at(id)+"]}",0};
    };
    Cloud cloud(root+"/annotations-session.json","ca","public","https://auth.test","https://api.test",api);cloud.sign_in("test","test",1000);
    State state(root+"/annotations-state.db");std::atomic<bool> cancel{false};long long now=2000;
    auto sync=[&] {sync_annotations(cloud,state,verified,native,database,++now,"PB743G","U743g.6.11.1683",cancel);};
    const std::string cfi="epubcfi(/6/2!/4/4/1,:0,:16)",text="Marker ALPHA-01.";
    auto remote=[&](std::string id,std::string note) {
        auto row=parse_json("{}");set(row.get(),"id",id);set(row.get(),"user_id",std::string("user"));set(row.get(),"book_hash",integrity.readest_hash);
        set(row.get(),"type",std::string("annotation"));set(row.get(),"cfi",cfi);set(row.get(),"text",text);set(row.get(),"note",note);
        set(row.get(),"style",std::string("highlight"));set(row.get(),"color",std::string("yellow"));set(row.get(),"created_at",1000000LL);set(row.get(),"updated_at",now*1000);return json_text(row.get());
    };
    auto mutate=[&](const std::string& id,const char* key,const std::string& value) {auto row=parse_json(server.at(id));set(row.get(),key,value);set(row.get(),"updated_at",(++now)*1000);server[id]=json_text(row.get());};
    auto native_note=[&](const std::string& value) {sql(db,"UPDATE Tags SET Val='"+value+"' WHERE TagID=105 AND ItemID=2");};
    validate_annotation_range(epub,"epubcfi(/6/2[alpha]!/4/4[ALPHA-01]/1:0)","epubcfi(/6/2!/4/4/1:16)",text);
    fails([&]{validate_annotation_range(epub,"epubcfi(/6/2!/4/4/1:0)","epubcfi(/6/2!/4/4/1:17)",text+"X");},"does not match");
    fails([&]{validate_annotation_range(epub,"epubcfi(/6/2!/4/4/1:0)","epubcfi(/6/2!/4/4/1:999999)",text);},"exceeds");
    // First import, exact repeat, and reader normalization do not upload or duplicate.
    server["remote1"]=remote("remote1","remote note");sync();assert(posts==0 && scalar(db,"SELECT count(*) FROM Items")=="2");
    sync();assert(posts==0 && scalar(db,"SELECT count(*) FROM Items")=="2");
    sql(db,"UPDATE Tags SET Val=replace(replace(Val,'pbr:/webkit?##','pbr:/page?page=2&offs=4#'),'/1:0)','/1)') WHERE TagID IN (104,107)");
    sync();assert(posts==0);
    mutate("remote1","note","remote edited");sync();assert(scalar(db,"SELECT Val FROM Tags WHERE ItemID=2 AND TagID=105")=="{\"text\":\"remote edited\"}");
    // Native in-place edits upload to the known ID.
    native_note("{\"text\":\"local edited\"}");sync();assert(posts==1 && server.size()==1);
    sync();assert(posts==1);
    // Both sides changed: neither is overwritten.
    native_note("{\"text\":\"local conflict\"}");mutate("remote1","note","remote conflict");
    fails(sync,"both readers");assert(posts==1 && scalar(db,"SELECT Val FROM Tags WHERE ItemID=2 AND TagID=105").find("local conflict")!=std::string::npos);
    native_note("{\"text\":\"remote conflict\"}");sync();
    // Durable failed push survives reopening State, and reuses exactly the payload.
    native_note("{\"text\":\"retry me\"}");fail_post=true;fails(sync,"saved for retry");const auto pending=last_post;
    fail_post=false;
    {State reopened(root+"/annotations-state.db");sync_annotations(cloud,reopened,verified,native,database,++now,"PB743G","U743g.6.11.1683",cancel);}
    assert(last_post==pending);int successful=posts;sync();assert(posts==successful);
    // Ambiguous commit: the next pull acknowledges it without a second POST.
    native_note("{\"text\":\"already committed\"}");lose_response=true;fails(sync,"lost response");successful=posts;lose_response=false;sync();assert(posts==successful);
    // Server winner is not treated as an acknowledgement of our edit.
    native_note("{\"text\":\"rejected edit\"}");server_wins=true;fails(sync,"did not accept");server_wins=false;sync();
    // Native Edit creates a new UUID and tombstones the old UUID.
    sql(db,"UPDATE Items SET State=2 WHERE OID=2; INSERT INTO Items VALUES(3,1,4,0,2001,'NEW-NATIVE-UUID');"
        "INSERT INTO Tags(ItemID,TagID,Val,TimeEdt) SELECT 3,TagID,Val,TimeEdt FROM Tags WHERE ItemID=2;"
        "UPDATE Tags SET Val='{\"text\":\"replacement edit\"}' WHERE ItemID=3 AND TagID=105;");
    sync();assert(server.size()==2);assert(record_timestamp(parse_json(server.at("remote1")).get(),"deleted_at")>0);
    successful=posts;sync();assert(posts==successful);
    std::string replacement;for(const auto& [id,row]:server) if(id!="remote1") replacement=id;
    const auto before_delete=server.at(replacement);
    auto deleted=parse_json(server.at(replacement));set(deleted.get(),"deleted_at",(++now)*1000);set(deleted.get(),"updated_at",now*1000);server[replacement]=json_text(deleted.get());
    sync();assert(scalar(db,"SELECT State FROM Items WHERE OID=3")=="2");successful=posts;sync();assert(posts==successful);
    // A stale pull after a successful deletion must never resurrect the highlight.
    const auto tombstone=server.at(replacement);server[replacement]=before_delete;
    fails(sync,"Stale Readest");assert(scalar(db,"SELECT State FROM Items WHERE OID=3")=="2");server[replacement]=tombstone;
    // Wrong account, bad ranges and concurrent native changes fail before import.
    server["new"]=remote("new","new note");mutate("new","user_id","wrong");fails(sync,"account/book mismatch");mutate("new","user_id","user");
    mutate("new","cfi","epubcfi(/6/2!/4/4/1,:0,:999999)");fails(sync,"exceeds");mutate("new","cfi",cfi);
    race=true;fails(sync,"changed on PocketBook");race=false;sync();
    assert(scalar(db,"SELECT count(*) FROM Items WHERE State=0 AND ParentID=1")=="1");
    cancel=true;fails(sync,"cancelled");cancel=false;
    fails([&]{sync_annotations(cloud,state,verified,native,database,now,"other","other",cancel);},"firmware");
    assert(scalar(db,"PRAGMA integrity_check")=="ok");sqlite3_close(db);
    std::cout<<"Annotation create/edit/delete, durable retry, conflicts and exact ranges passed\n";
}
