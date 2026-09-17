#include "upload.h"
#include "json_util.h"
#include <ctime>
#include <cerrno>
#include <algorithm>
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
LibraryBook refresh_book(Cloud& cloud,State& state,const std::string& hash,long long now) {
    long long since=0;
    for (;;) {
        // Treat the server's book filter as an optimization: deployed versions
        // may return other books. Search bounded pages, persisting only our row.
        const auto page=fetch_library_page(cloud,since,100,now,hash);
        for(const auto& book:page.books) if(book.hash==hash) {
            const auto cursor=state.cursor(cloud.session().user_id);
            LibraryPage selected;selected.books.push_back(book);selected.cursor=cursor;
            state.apply_page(cloud.session().user_id,cursor,selected);return book;
        }
        if(!page.more) return {};
        since=page.cursor;
    }
}
void send_file(Cloud& cloud,const HttpTransport& transport,const std::string& ca,const std::string& hash,
               const std::string& name,int fd,size_t size,long long now,const std::atomic<bool>& cancel) {
    if(!transport.upload) throw std::runtime_error("This connection does not support EPUB uploads");
    check(cancel); Json body(json_object_new_object(),json_object_put);
    put(body.get(),"fileName","Readest/Books/"+hash+"/"+name);put(body.get(),"bookHash",hash);
    json_object_object_add(body.get(),"fileSize",json_object_new_int64(size));
    auto response=cloud.post("/api/storage/upload",json_text(body.get()),now);
    if(response.status==403) throw std::runtime_error("Readest refused the upload (HTTP 403). Check your storage quota and account.");
    if(response.status!=200) throw std::runtime_error("Cannot prepare Readest upload (HTTP "+std::to_string(response.status)+")");
    auto reservation=parse_json(response.body);
    check(cancel);
    response=transport.upload(string_member(reservation.get(),"uploadUrl"),fd,ca,size,cancel);
    if(response.status<200 || response.status>=300) throw std::runtime_error("File transfer failed (HTTP "+std::to_string(response.status)+"). Retry the upload.");
}
}
bool upload_cover(Cloud& cloud,const VerifiedManagedBook& verified,const HttpTransport& transport,
    const std::string& ca,const std::string& root,const std::atomic<bool>& cancel) {
    check(cancel);
    const auto& book=verified.book();
    const auto meta=epub_metadata(book.path,true);
    if(meta.cover.empty()) return false;
    const auto path=root+"/upload-cover-"+book.book.hash;
    const int fd=open(path.c_str(),O_RDWR|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if(fd<0) throw std::runtime_error("Cannot stage cover on device");
    try {
        size_t at=0;
        while(at<meta.cover.size()) {
            auto count=write(fd,meta.cover.data()+at,meta.cover.size()-at);
            if(count<0 && errno==EINTR) continue;
            if(count<=0) throw std::runtime_error("Cannot write staged cover on device");
            at+=count;
        }
        // Readest uses cover.png for both JPEG and PNG content.
        send_file(cloud,transport,ca,book.book.hash,"cover.png",fd,meta.cover.size(),time(nullptr),cancel);
        close(fd);unlink(path.c_str());return true;
    } catch(...) {close(fd);unlink(path.c_str());throw;}
}
UploadResult upload_book(Cloud& cloud,State& state,const VerifiedManagedBook& verified,
    const UploadPosition& position,const HttpTransport& transport,
    const std::string& ca,const std::string& root,const std::atomic<bool>& cancel) {
    const auto& book=verified.book(); const auto hash=book.book.hash,user=cloud.session().user_id;
    const auto now=static_cast<long long>(time(nullptr));
    auto pending=state.upload(user,hash); UploadResult result;
    if(pending.stage!=UploadStage::None && pending.sha256!=book.sha256)
        throw std::runtime_error("EPUB bytes changed since upload began. Restore the original file before retrying.");
    auto remote=refresh_book(cloud,state,hash,now);
    const auto initial_remote=remote;
    const auto files=fetch_book_files(cloud,now,hash);
    const auto found=files.find(hash);
    const bool has_epub=found!=files.end() && found->second.epubs>0;
    const bool publish=remote.hash.empty() || remote.deleted || !has_epub;
    if(publish || pending.stage==UploadStage::Started || pending.stage==UploadStage::BytesSent) {
        if(pending.stage==UploadStage::None || pending.stage==UploadStage::Published ||
           (pending.stage==UploadStage::BytesSent && !has_epub)) {
            pending={UploadStage::Started,book.sha256};state.save_upload(user,hash,pending);
        }
        if(pending.stage==UploadStage::Started) {
            File file(book.path); struct stat before,after;
            if(fstat(file.fd,&before) || before.st_size!=book.size) throw std::runtime_error("EPUB changed before upload");
            send_file(cloud,transport,ca,hash,hash+".epub",file.fd,book.size,now,cancel);
            if(fstat(file.fd,&after) || before.st_size!=after.st_size || before.st_mtime!=after.st_mtime || before.st_ctime!=after.st_ctime)
                throw std::runtime_error("EPUB changed during upload");
            pending.stage=UploadStage::BytesSent;state.save_upload(user,hash,pending);
            // Covers are optional; failure must not resend the EPUB.
            try { upload_cover(cloud,verified,transport,ca,root,cancel); }
            catch(const std::exception& e) {check(cancel); result.warning="Cover could not be uploaded: "+std::string(e.what());}
        }
        check(cancel);
        remote=refresh_book(cloud,state,hash,now);
        if(remote.deleted && (!initial_remote.deleted || remote.raw!=initial_remote.raw))
            throw std::runtime_error("Book was removed in Readest during upload. Choose Upload again to restore it.");
        if(publish || pending.stage==UploadStage::BytesSent) {
            Json row(json_object_new_object(),json_object_put);
            put(row.get(),"hash",hash);put(row.get(),"bookHash",hash);put(row.get(),"format","EPUB");
            put(row.get(),"title",remote.hash.empty()?book.book.title:remote.title);
            put(row.get(),"author",remote.hash.empty()?book.book.author:remote.author);
            long long stamp=now*1000;
            if(!remote.raw.empty()) {
                auto previous=parse_json(remote.raw);
                // Preserve cloud metadata while restoring the book or adding bytes.
                for(const auto& key:{std::make_pair("meta_hash","metaHash"),{"source_title","sourceTitle"},
                    {"group_id","groupId"},{"group_name","groupName"},{"tags","tags"},{"progress","progress"},
                    {"reading_status","readingStatus"},{"cover_hash","coverHash"}})
                    if(auto* value=member(previous.get(),key.first)) json_object_object_add(row.get(),key.second,json_object_get(value));
                if(member(previous.get(),"metadata")) {
                    auto metadata=parse_json(string_member(previous.get(),"metadata"));
                    json_object_object_add(row.get(),"metadata",metadata.release());
                }
                for(const auto& key:{std::make_pair("created_at","createdAt"),{"group_updated_at","groupUpdatedAt"},
                    {"reading_status_updated_at","readingStatusUpdatedAt"},{"cover_updated_at","coverUpdatedAt"},{"metadata_updated_at","metadataUpdatedAt"}})
                    json_object_object_add(row.get(),key.second,json_object_new_int64(record_timestamp(previous.get(),key.first)));
                stamp=std::max(stamp,std::max(record_timestamp(previous.get(),"updated_at"),record_timestamp(previous.get(),"deleted_at"))+1);
            } else json_object_object_add(row.get(),"createdAt",json_object_new_int64(stamp));
            if(stamp>now*1000+5*60*1000) throw std::runtime_error("Readest timestamp is ahead; check the device clock");
            for(const auto* key:{"updatedAt","uploadedAt"}) json_object_object_add(row.get(),key,json_object_new_int64(stamp));
            json_object_object_add(row.get(),"deletedAt",nullptr);
            auto response=cloud.post("/api/sync","{\"books\":["+json_text(row.get())+"],\"configs\":[],\"notes\":[]}",now);
            remote=response.status==200?refresh_book(cloud,state,hash,now):LibraryBook{};
            if(response.status!=200 || remote.hash.empty() || remote.deleted)
                throw std::runtime_error("EPUB uploaded; library publication pending. Choose Retry upload.");
        }
    }
    pending={UploadStage::Published,book.sha256};state.save_upload(user,hash,pending);
    BookFiles uploaded_files=found==files.end()?BookFiles{}:found->second;
    uploaded_files.epubs=std::max(1,uploaded_files.epubs);state.save_book_file(user,hash,uploaded_files);
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
