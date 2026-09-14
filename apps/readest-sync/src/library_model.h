#pragma once
#include "application.h"
#include <QAbstractListModel>

QString availabilityLabel(readest::Availability value);
QString percentageLabel(double value);
class LibraryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY navigationChanged)
    Q_PROPERTY(int page READ page NOTIFY navigationChanged)
    Q_PROPERTY(int pages READ pages NOTIFY navigationChanged)
    Q_PROPERTY(QString searchText READ searchText NOTIFY navigationChanged)
    Q_PROPERTY(int availabilityFilter READ availabilityFilter NOTIFY navigationChanged)
public:
    enum Role { Account=Qt::UserRole+1, BookHash, Title, Author, Availability, Cover, LocalPath, LocalProgress, ReadestProgress };
    explicit LibraryModel(QObject* parent=nullptr):QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent={}) const override;
    QVariant data(const QModelIndex& index,int role) const override;
    QHash<int,QByteArray> roleNames() const override;
    void replace(std::vector<readest::LibraryEntry> books);
    void search(const QString& text);
    void filter(int value);
    void setCapacity(int value);
    void turnPage(int direction);
    void setCover(const readest::BookId& id,const QString& path);
    const readest::LibraryEntry* find(const readest::BookId& id) const;
    const readest::LibraryEntry& at(int row) const;
    int count() const { return static_cast<int>(visible_.size()); }
    int page() const { return page_+1; }
    int pages() const;
    QString searchText() const { return search_; }
    int availabilityFilter() const { return filter_; }
    const std::vector<readest::LibraryEntry>& entries() const { return entries_; }
signals:
    void navigationChanged();
private:
    std::vector<readest::LibraryEntry> entries_;
    std::vector<size_t> visible_;
    QString search_;
    int filter_=0,page_=0,capacity_=6;
    void rebuild();
};
