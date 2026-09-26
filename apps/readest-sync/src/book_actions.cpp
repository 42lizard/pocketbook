#include "book_actions.h"
#include "library_model.h"

QVariantMap BookActions::presentation() const {
    return {{"primary",primary.isEmpty()?QVariant():QVariant(primary)},{"secondary",secondary},{"menu",menu},
        {"choices",choices},{"choosing",choosing}};
}

BookActions bookActions(const readest::LibraryEntry& entry,bool signedIn,
                        BookChoice choice,BookBlock block) {
    using readest::Availability;
    BookActions result;
    const bool conflict=choice==BookChoice::OpenConflict || choice==BookChoice::SyncConflict;
    enum class Place { Content, Menu, Navigation };
    auto add=[&](const QString& command,const QString& text,Place place=Place::Content) {
        const QVariantMap action{{"command",command},{"text",text}};
        result.allowed.append(action);
        if(place==Place::Navigation) return;
        if(conflict) result.choices.append(action);
        else if(place==Place::Menu) result.menu.append(action);
        else if(result.choosing || !result.primary.isEmpty()) result.secondary.append(action);
        else result.primary=action;
    };
    auto back=[&] { add("back","Back to library",Place::Navigation); };
    if(block!=BookBlock::None) {
        if(block==BookBlock::Pending) return result;
        add(entry.availability==Availability::OnDevice?"sync":"refresh",
            entry.availability==Availability::OnDevice?"Sync now":"Check availability");
        back(); return result;
    }
    if(entry.book.needs_copy_choice || choice==BookChoice::LocalCopy) {
        result.choosing=!entry.book.copies.empty();
        for(size_t i=0;i<entry.book.copies.size();++i) {
            const auto& copy=entry.book.copies[i];
            add("copy:"+QString::number(i),QString::fromStdString(copy.path)+" · "+percentageLabel(copy.percentage));
        }
        back(); return result;
    }
    if(choice==BookChoice::OpenConflict) {
        add("openPocketBook","Open at PocketBook position"); add("openReadest","Open at Readest position");
        back(); return result;
    }
    if(choice==BookChoice::SyncConflict) {
        add("usePocketBook","Use PocketBook position"); add("useReadest","Use Readest position");
        back(); return result;
    }
    if(entry.upload_pending && signedIn) {
        add("upload","Retry upload / reading position"); back(); return result;
    }
    if(entry.book.local_only || entry.book.book.deleted || !signedIn) {
        add("offline","Open at PocketBook position");
        if(signedIn) add("upload",entry.book.book.deleted?"Re-upload to Readest":entry.upload_pending?"Retry upload":"Upload to Readest");
        else add("signin","Sign in to upload");
        if(entry.book.copies.size()>1) add("copies","Choose local copy");
        back(); return result;
    }
    if(entry.book.copies.size()>1) add("copies","Choose local copy");
    if(entry.availability==Availability::OnDevice) {
        add("open",entry.sync.pending_remote.empty()?"Open":"Open at Readest position");
        add("sync","Sync now"); add("offline","Read offline");
    } else if(entry.availability==Availability::Downloadable) add("download","Download EPUB");
    else add("refresh","Check availability");
    if(entry.availability==Availability::OnDevice && entry.book.epubs==0)
        add("upload","Upload EPUB to Readest");
    if(entry.availability==Availability::OnDevice && entry.book.epubs>0)
        add("uploadCover","Upload cover to Readest",Place::Menu);
    back(); return result;
}
