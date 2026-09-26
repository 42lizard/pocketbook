#pragma once
#include <QVariantMap>
#include <QVariantList>

namespace readest { struct LibraryEntry; }
enum class BookChoice { None, SyncConflict, OpenConflict, LocalCopy };
enum class BookBlock { None, Pending, Acknowledged };

struct BookActions {
    QVariantList allowed,secondary,menu,choices;
    QVariantMap primary;
    bool choosing=false;
    QVariantMap presentation() const;
};

BookActions bookActions(const readest::LibraryEntry& entry,bool signedIn,
                        BookChoice choice,BookBlock block);
