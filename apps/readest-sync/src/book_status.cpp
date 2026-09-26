#include "book_status.h"
#include "application.h"

QString bookStatusLabel(const readest::LibraryEntry& entry) {
    using readest::Availability;
    if(entry.book.book.deleted) return "Removed from Readest";
    if(entry.upload_pending) return "Upload pending";
    if(entry.book.local_only) return "PocketBook only";
    if(entry.availability==Availability::OnDevice && entry.book.epubs==0)
        return "On device · Readest progress only";
    switch(entry.availability) {
    case Availability::OnDevice: return "On device";
    case Availability::Downloadable: return "Available to download";
    case Availability::ProgressOnly: return "Progress only";
    case Availability::Unknown: return "Not checked";
    case Availability::Unavailable: return "EPUB unavailable";
    case Availability::Multiple: return "Multiple EPUBs";
    case Availability::Removed: return "Removed from cloud";
    }
    return {};
}
