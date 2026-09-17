#include "upload.h"
#include "json_util.h"
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace readest {
namespace {
struct File {
    int fd;
    explicit File(const std::string& path):fd(open(path.c_str(),O_RDONLY|O_NOFOLLOW)) {
        if(fd<0) throw std::runtime_error("Cannot open EPUB for upload");
    }
    ~File() { close(fd); }
};
void check(const std::atomic<bool>& cancel) { if(cancel.load()) throw std::runtime_error("Cancelled."); }
void put(json_object* object,const char* name,const std::string& value) {
    json_object_object_add(object,name,json_object_new_string_len(value.data(),value.size()));
}
bool refresh_book(Cloud& cloud,State& state,const std::string& hash,long long now) {
    auto response=cloud.get("/api/sync?type=books&since=0&book="+hash,now);
    if(response.status!=200) throw std::runtime_error("Cannot check Readest before uploading");
    auto page=parse_library_page(response.body,cloud.session().user_id,0,1);
    if(page.books.size()>1 || (!page.books.empty() && page.books[0].hash!=hash))
        throw std::runtime_error("Unexpected Readest book identity");
    if(page.books.empty()) return false;
    if(page.books[0].deleted) throw std::runtime_error("This book was removed in Readest. Restore it there before uploading.");
    const auto cursor=state.cursor(cloud.session().user_id);page.cursor=cursor;
    state.apply_page(cloud.session().user_id,cursor,page); return true;
}
void send_file(Cloud& cloud,const HttpTransport& transport,const std::string& ca,const std::string& hash,
               const std::string& name,int fd,size_t size,long long now,const std::atomic<bool>& cancel) {
    if(!transport.upload) throw std::runtime_error("This connection does not support EPUB uploads");
    check(cancel); Json body(json_object_new_object(),json_object_put);
    put(body.get(),"fileName","Readest/Books/"+hash+"/"+name);put(body.get(),"bookHash",hash);
    json_object_object_add(body.get(),"fileSize",json_object_new_int64(size));
    auto response=cloud.post("/api/storage/upload",json_text(body.get()),now);
    if(response.status==403) throw std::runtime_error("Readest refused the upload. Check your storage quota and account.");
    if(response.status!=200) throw std::runtime_error("Cannot prepare Readest upload");
    auto reservation=parse_json(response.body);
    check(cancel);
    response=transport.upload(string_member(reservation.get(),"uploadUrl"),fd,ca,size,cancel);
    if(response.status<200 || response.status>=300) throw std::runtime_error("EPUB transfer failed; choose Retry upload");
}
}
UploadResult upload_book(Cloud& cloud,State& state,const VerifiedManagedBook& verified,
    const UploadPosition& position,const HttpTransport& transport,
    const std::string& ca,const std::string& root,const std::atomic<bool>& cancel) {
    const auto& book=verified.book(); const auto hash=book.book.hash,user=cloud.session().user_id;
    const auto now=static_cast<long long>(time(nullptr));
    auto pending=state.upload(user,hash); UploadResult result;
    if(pending.stage!=UploadStage::None && pending.sha256!=book.sha256)
        throw std::runtime_error("EPUB bytes changed since upload began. Restore the original file before retrying.");
    bool exists=refresh_book(cloud,state,hash,now);
    if(exists && pending.stage==UploadStage::Started) {
        pending.stage=UploadStage::Published;state.save_upload(user,hash,pending);
    }
    if(!exists || pending.stage==UploadStage::Started || pending.stage==UploadStage::BytesSent) {
        if(pending.stage==UploadStage::None) {
            pending={UploadStage::Started,book.sha256};state.save_upload(user,hash,pending);
        }
        if(pending.stage==UploadStage::Started) {
            File file(book.path); struct stat before,after;
            if(fstat(file.fd,&before) || before.st_size!=book.size) throw std::runtime_error("EPUB changed before upload");
            send_file(cloud,transport,ca,hash,hash+".epub",file.fd,book.size,now,cancel);
            if(fstat(file.fd,&after) || before.st_size!=after.st_size || before.st_mtime!=after.st_mtime || before.st_ctime!=after.st_ctime)
                throw std::runtime_error("EPUB changed during upload");
            pending.stage=UploadStage::BytesSent;state.save_upload(user,hash,pending);
            // Covers are optional and bounded. Their failure must not resend the EPUB.
            try {
                const auto meta=epub_metadata(book.path,true);
                if(!meta.cover.empty()) {
                    const auto path=root+"/upload-cover-"+hash;
                    const int fd=open(path.c_str(),O_RDWR|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
                    if(fd<0) throw std::runtime_error("Cannot stage cover");
                    try {
                        size_t at=0;while(at<meta.cover.size()) {
                            auto count=write(fd,meta.cover.data()+at,meta.cover.size()-at);
                            if(count<=0) throw std::runtime_error("Cannot stage cover");at+=count;
                        }
                        send_file(cloud,transport,ca,hash,meta.cover_type=="image/png"?"cover.png":"cover.jpg",fd,meta.cover.size(),now,cancel);
                        close(fd);unlink(path.c_str());
                    } catch(...) {close(fd);unlink(path.c_str());throw;}
                }
            } catch(const std::exception&) {check(cancel); result.warning="Cover could not be uploaded.";}
        }
        check(cancel);
        exists=refresh_book(cloud,state,hash,now);
        if(!exists) {
            Json row(json_object_new_object(),json_object_put);
            put(row.get(),"hash",hash);put(row.get(),"bookHash",hash);put(row.get(),"format","EPUB");
            put(row.get(),"title",book.book.title);put(row.get(),"author",book.book.author);
            for(const auto* key:{"createdAt","updatedAt","uploadedAt"}) json_object_object_add(row.get(),key,json_object_new_int64(now*1000));
            auto response=cloud.post("/api/sync","{\"books\":["+json_text(row.get())+"],\"configs\":[],\"notes\":[]}",now);
            if(response.status!=200 || !refresh_book(cloud,state,hash,now))
                throw std::runtime_error("EPUB uploaded; library publication pending. Choose Retry upload.");
        }
    }
    pending={UploadStage::Published,book.sha256};state.save_upload(user,hash,pending);
    check(cancel);
    if(!position.error.empty()) {
        result.warning+=" Book uploaded; PocketBook position could not be transferred: "+position.error;
        result.pending=!position.unsupported;
        if(position.unsupported) state.save_upload(user,hash,{});
        return result;
    }
    try {
        result.action=sync_managed(cloud,state,verified,position.native.cfi,now,ProgressChoice::Automatic,0,position.native.progress);
        result.pending=result.action==SyncAction::Conflict || result.action==SyncAction::BackwardUnsupported || result.action==SyncAction::Unsupported || result.action==SyncAction::ApplyRemote;
        if(!result.pending) state.save_upload(user,hash,{});
    } catch(const std::exception& e) {check(cancel);result.pending=true;result.warning+=e.what();}
    return result;
}
}
