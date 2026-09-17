#include "library_model.h"
#include <algorithm>
QString percentageLabel(double value) { return value<0?QStringLiteral("—"):QString::number(value,'f',1)+"%"; }
QString availabilityLabel(readest::Availability value) {
    using readest::Availability;
    switch(value) {
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
int LibraryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid()?0:std::max(0,std::min(capacity_,count()-page_*capacity_));
}
const readest::LibraryEntry& LibraryModel::at(int row) const { return entries_.at(visible_.at(page_*capacity_+row)); }
QVariant LibraryModel::data(const QModelIndex& index,int role) const {
    if(!index.isValid() || index.row()<0 || index.row()>=rowCount()) return {};
    const auto& entry=at(index.row());
    const auto& book=entry.book.book;
    switch(role) {
    case Account: return QString::fromStdString(entry.id.account);
    case BookHash: return QString::fromStdString(entry.id.hash);
    case Title: return QString::fromStdString(book.title.empty()?"Untitled":book.title);
    case Author: return QString::fromStdString(book.author);
    case Availability: return book.deleted?QStringLiteral("Removed from Readest"):entry.upload_pending?QStringLiteral("Upload pending"):entry.book.local_only?QStringLiteral("PocketBook only"):
        entry.availability==readest::Availability::OnDevice && entry.book.epubs==0?QStringLiteral("On device · Readest progress only"):availabilityLabel(entry.availability);
    case Cover: return QString::fromStdString(entry.cover);
    case LocalPath: return QString::fromStdString(entry.book.path);
    case LocalProgress: return percentageLabel(entry.local_percentage);
    case ReadestProgress: return percentageLabel(entry.remote_percentage);
    }
    return {};
}
QHash<int,QByteArray> LibraryModel::roleNames() const {
    return {{Account,"account"},{BookHash,"bookHash"},{Title,"bookTitle"},{Author,"author"},{Availability,"availability"},
        {Cover,"coverPath"},{LocalPath,"localPath"},{LocalProgress,"localProgress"},{ReadestProgress,"readestProgress"}};
}
int LibraryModel::pages() const { return std::max(1,(count()+capacity_-1)/capacity_); }
void LibraryModel::rebuild() {
    visible_.clear();
    for(size_t i=0;i<entries_.size();++i) {
        const auto& entry=entries_[i];
        if(filter_==1 && entry.availability!=readest::Availability::Downloadable) continue;
        if(filter_==2 && entry.availability!=readest::Availability::OnDevice) continue;
        if(filter_==4 && !entry.book.local_only) continue;
        if(filter_==3 && entry.availability!=readest::Availability::ProgressOnly) continue;
        if(QString::fromStdString(entry.book.book.title+" "+entry.book.book.author).contains(search_,Qt::CaseInsensitive)) visible_.push_back(i);
    }
    if(page_>=pages()) page_=0;
}
void LibraryModel::replace(std::vector<readest::LibraryEntry> books) {
    beginResetModel(); entries_=std::move(books); rebuild(); endResetModel(); emit navigationChanged();
}
void LibraryModel::search(const QString& text) {
    beginResetModel(); search_=text; page_=0; rebuild(); endResetModel(); emit navigationChanged();
}
void LibraryModel::filter(int value) {
    if(value<0 || value>4) return;
    beginResetModel(); filter_=value; page_=0; rebuild(); endResetModel(); emit navigationChanged();
}
void LibraryModel::setCapacity(int value) {
    if(value<1 || value>100 || value==capacity_) return;
    beginResetModel(); capacity_=value; page_=0; rebuild(); endResetModel(); emit navigationChanged();
}
void LibraryModel::turnPage(int direction) {
    const auto next=std::max(0,std::min(pages()-1,page_+(direction<0?-1:1)));
    if(next==page_) return;
    beginResetModel(); page_=next; endResetModel(); emit navigationChanged();
}
const readest::LibraryEntry* LibraryModel::find(const readest::BookId& id) const {
    for(const auto& entry:entries_) if(entry.id==id) return &entry;
    return nullptr;
}
void LibraryModel::setCover(const readest::BookId& id,const QString& path) {
    for(auto& entry:entries_) if(entry.id==id) entry.cover=path.toStdString();
    for(int row=0;row<rowCount();++row) if(at(row).id==id) emit dataChanged(index(row),index(row),{Cover});
}
