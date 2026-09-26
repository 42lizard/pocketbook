#pragma once
#include "application.h"
#include "book_actions.h"
#include "cover_loader.h"
#include "device_adapter.h"
#include "library_model.h"
#include "operation_runner.h"
#include <QObject>
#include <QVariantList>

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool initialized READ initialized NOTIFY changed)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(bool signingIn READ signingIn NOTIFY changed)
    Q_PROPERTY(bool detail READ detail NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString statusKind READ statusKind NOTIFY changed)
    Q_PROPERTY(QString hint READ hint NOTIFY changed)
    Q_PROPERTY(QVariantMap book READ book NOTIFY changed)
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    Q_PROPERTY(QVariantMap actionPresentation READ actionPresentation NOTIFY changed)
    Q_PROPERTY(bool decision READ decision NOTIFY changed)
    Q_PROPERTY(bool blocking READ blocking NOTIFY changed)
    Q_PROPERTY(QString blockingMessage READ blockingMessage NOTIFY changed)
    Q_PROPERTY(LibraryModel* library READ library CONSTANT)
public:
    explicit AppController(QObject* parent=nullptr);
    AppController(readest::ApplicationConfig config,DeviceAccess device,QObject* parent=nullptr);
    ~AppController() override;
    bool busy() const { return (runner_.busy() && !covers_.active()) || bool(pending_request_); }
    bool initialized() const { return initialized_; }
    bool signedIn() const { return signed_in_; }
    bool signingIn() const { return signing_in_; }
    bool detail() const { return !selected_.hash.empty(); }
    QString title() const;
    QString status() const { return busy()?(exiting_?"Stopping…":busy_message_+"…"):status_; }
    QString statusKind() const { return status_kind_; }
    QString hint() const;
    QVariantMap book() const;
    QVariantList actions() const;
    QVariantMap actionPresentation() const;
    bool decision() const { return choice_==ChoiceState::SyncConflict || choice_==ChoiceState::OpenConflict; }
    bool blocking() const;
    QString blockingMessage() const;
    LibraryModel* library() { return &library_; }
    Q_INVOKABLE void initialize();
    Q_INVOKABLE void signIn(const QString& email,const QString& password);
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void showSignIn();
    Q_INVOKABLE void refreshLibrary();
    Q_INVOKABLE void scanDevice();
    Q_INVOKABLE void selectBook(const QString& account,const QString& hash);
    Q_INVOKABLE void runAction(const QString& command);
    Q_INVOKABLE void cancelDecision();
    Q_INVOKABLE void acknowledgeBlocking();
    Q_INVOKABLE void search(const QString& text);
    Q_INVOKABLE void setAvailabilityFilter(int value);
    Q_INVOKABLE void setPageCapacity(int value);
    Q_INVOKABLE void turnPage(int direction);
    Q_INVOKABLE void back();
    Q_INVOKABLE void close();
    Q_INVOKABLE void resume();
signals:
    void changed();
private:
    DeviceAccess device_;
    std::shared_ptr<readest::ApplicationService> service_;
    LibraryModel library_;
    OperationRunner runner_; // Destroy/join the runner before the service.
    VisibleCoverLoader covers_; // Invalidates callbacks before runner destruction.
    readest::BookId selected_;
    using ChoiceState=BookChoice;
    ChoiceState choice_=ChoiceState::None;
    enum class BlockingState { None, AppliedUnrecorded, NativeCommitUncertain };
    BlockingState blocking_state_=BlockingState::None;
    readest::BookId blocking_book_;
    bool blocking_acknowledged_=false;
    long long revision_=0;
    bool signing_in_=false,initialized_=false,signed_in_=false,exiting_=false,reader_opened_=false;
    QString status_="Starting…",status_kind_="info",busy_message_;
    std::unique_ptr<readest::Request> pending_request_;
    void submit(readest::Request request);
    void complete(const readest::Request& request,readest::OperationResult result);
    void prepareCovers();
    BookActions bookActions() const;
};
