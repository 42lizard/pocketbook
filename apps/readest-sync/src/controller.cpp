#include "controller.h"
#include "book_status.h"
#include <QCoreApplication>
#include <QTimer>
using namespace readest;
namespace {
QString syncMessage(SyncAction action) {
    switch(action) {
    case SyncAction::Upload: return "PocketBook position uploaded to Readest.";
    case SyncAction::ApplyRemote: return "Readest position downloaded. Choose Open at Readest position to apply it.";
    case SyncAction::Conflict: return "Both positions differ. Choose which position to use.";
    case SyncAction::Unsupported: return "This position format is unsupported. Reading progress was not replaced.";
    case SyncAction::BackwardUnsupported: return "The saved positions differ. Choose where to open this book.";
    default: return "Reading positions are synchronized.";
    }
}
QString baseResultMessage(const OperationResult& result) {
    const auto error=QString::fromStdString(result.error);
    if(!result.recovery_warning.empty()) return "Some downloads need attention: "+QString::fromStdString(result.recovery_warning);
    switch(result.outcome) {
    case Outcome::Ready: return {};
    case Outcome::SessionInvalid: return "Saved sign-in could not be loaded. Please sign in again.";
    case Outcome::SignedIn: return "Signed in. Choose Refresh library.";
    case Outcome::SignedOut: return "Signed out. Downloaded files are retained.";
    case Outcome::Refreshed:
        if(!result.metadata_error.empty()) return "File availability updated. Library metadata refresh failed: "+QString::fromStdString(result.metadata_error);
        return "Library refreshed. Visible covers load in the background.";
    case Outcome::Scanned: return QString("Device scan complete. Matched %1 existing EPUBs.").arg(result.matched);
    case Outcome::Downloaded: return "Downloaded and verified. Choose Open to read.";
    case Outcome::Reused: return "Found a matching EPUB on device. Choose Open to read.";
    case Outcome::Uploaded: return "Book available in Readest.";
    case Outcome::CoverUploaded: return "Cover uploaded to Readest.";
    case Outcome::UploadPending: return "Book uploaded; reading position pending. Retry or resolve the differing positions.";
    case Outcome::CopySelected: return "Local copy selected.";
    case Outcome::Synced: {
        auto text=syncMessage(result.sync_action);
        if(result.position_order) text+=result.position_order>0?" Readest is earlier; PocketBook is farther ahead.":" PocketBook is earlier; Readest is farther ahead.";
        return text;
    }
    case Outcome::LocalOpen: return "Opening the saved PocketBook position.";
    case Outcome::SyncUnavailable: return "Opening saved local position. Sync unavailable: "+error;
    case Outcome::NeedsNativeSettings: return "Open once, close with Back, then Sync. If positions differ, choose Use Readest position, then Open.";
    case Outcome::Applied: return "Readest position applied.";
    case Outcome::AppliedUnrecorded: case Outcome::NativeCommitUncertain: case Outcome::Failed: return error;
    case Outcome::Cancelled: return "Cancelled.";
    }
    return {};
}
QString resultMessage(const OperationResult& result) {
    const auto message=baseResultMessage(result);
    return result.progress_warning.empty()?message:message+" "+QString::fromStdString(result.progress_warning);
}
QString operationMessage(Command command) {
    switch(command) {
    case Command::Initialize: return "Starting";
    case Command::SignIn: return "Signing in";
    case Command::SignOut: return "Signing out";
    case Command::Refresh: return "Refreshing library and covers";
    case Command::Scan: return "Looking for existing EPUBs";
    case Command::Download: return "Downloading";
    case Command::Sync: return "Synchronizing";
    case Command::Open: return "Opening book";
    case Command::ReadOffline: return "Opening PocketBook position";
    case Command::Resume: return "Updating PocketBook progress";
    case Command::Upload: return "Uploading book and reading position";
    case Command::UploadCover: return "Uploading cover to Readest";
    case Command::SelectCopy: return "Selecting local copy";
    case Command::Covers: return "Loading covers";
    }
    return {};
}
}
AppController::AppController(QObject* parent):AppController(deviceApplicationConfig(),deviceAccess(),parent) {}
AppController::AppController(ApplicationConfig config,DeviceAccess device,QObject* parent)
    :QObject(parent),device_(std::move(device)),service_(std::make_shared<ApplicationService>(std::move(config))),library_(this),runner_(device_,this),
     covers_({
         [this](std::function<void()> work) { QTimer::singleShot(0,this,std::move(work)); },
         [this](const LibraryEntry& entry) { return device_.prepareCover?device_.prepareCover(entry).toStdString():std::string(); },
         [this](const std::vector<BookId>& ids,VisibleCoverLoader::Completion done) {
             Request request; request.command=Command::Covers; request.books=ids;
             const auto service=service_;
             return runner_.start([service,request](const std::atomic<bool>& cancel) { return service->execute(request,cancel); },
                                  true,std::move(done),[](bool) {});
         },
         [this] { runner_.cancel(); }},
         [this](const BookId& id,const std::string& path) { library_.setCover(id,QString::fromStdString(path)); },
         [this] {
             if(exiting_) { QCoreApplication::quit(); return; }
             if(pending_request_) { auto next=std::move(*pending_request_); pending_request_.reset(); submit(std::move(next)); }
         }) {
    connect(&library_,&LibraryModel::navigationChanged,this,[this] { prepareCovers(); });
}
AppController::~AppController() { runner_.cancel(); }
QString AppController::title() const {
    const auto* entry=library_.find(selected_);
    return entry?QString::fromStdString(entry->book.book.title):QStringLiteral("Readest Sync");
}
bool AppController::blocking() const {
    return blocking_state_!=BlockingState::None && selected_==blocking_book_ && !blocking_acknowledged_;
}
QString AppController::blockingMessage() const {
    if(blocking_state_==BlockingState::AppliedUnrecorded)
        return QStringLiteral("The reading position was applied, but Readest could not confirm the saved result. Sync again before opening.");
    if(blocking_state_==BlockingState::NativeCommitUncertain)
        return QStringLiteral("The PocketBook write could not be confirmed. Acknowledge this warning, then check the position again.");
    return {};
}
QString AppController::hint() const {
    const auto* entry=library_.find(selected_); if(!entry) return {};
    auto text=bookStatusLabel(*entry)+". "+QString::fromStdString(entry->book.book.author);
    if(!entry->book.local_only && !entry->book.book.deleted && entry->book.epubs==0)
        text+=entry->availability==Availability::OnDevice?"\nReadest has progress only. You can upload this local EPUB.":"\nReadest has progress only. No local EPUB is available to upload.";
    text+="\nPocketBook: "+percentageLabel(entry->local_percentage)+" · Readest: "+percentageLabel(entry->remote_percentage);
    return text+"\nPercentages use each reader’s page counts. — means unavailable.";
}
QVariantMap AppController::book() const {
    const auto* entry=library_.find(selected_); if(!entry) return {};
    return {{"title",QString::fromStdString(entry->book.book.title.empty()?"Untitled":entry->book.book.title)},
        {"author",QString::fromStdString(entry->book.book.author)},
        {"availability",bookStatusLabel(*entry)},{"coverPath",QString::fromStdString(entry->cover)},
        {"pocketBookProgress",percentageLabel(entry->local_percentage)},
        {"readestProgress",percentageLabel(entry->remote_percentage)}};
}
BookActions AppController::bookActions() const {
    const auto* entry=library_.find(selected_);
    if(busy() || !entry) return {};
    const auto block=blocking_state_!=BlockingState::None && selected_==blocking_book_
        ?(blocking_acknowledged_?BookBlock::Acknowledged:BookBlock::Pending):BookBlock::None;
    return ::bookActions(*entry,signed_in_,choice_,block);
}
QVariantList AppController::actions() const { return bookActions().allowed; }
QVariantMap AppController::actionPresentation() const { return bookActions().presentation(); }
void AppController::submit(Request request) {
    if(runner_.busy()) {
        if(covers_.active()) { pending_request_=std::make_unique<Request>(std::move(request)); covers_.show({}); emit changed(); }
        return;
    }
    covers_.show({});
    const bool online=request.command==Command::SignIn || request.command==Command::Refresh || request.command==Command::Download ||
        request.command==Command::Sync || request.command==Command::Open || request.command==Command::Upload || request.command==Command::UploadCover;
    const auto message=operationMessage(request.command);
    // The completion captures no credentials. Service work cannot mutate presentation state.
    Request context; context.command=request.command; context.book=request.book; context.choice=request.choice;
    const auto service=service_;
    const bool started=runner_.start([service,request=std::move(request)](const std::atomic<bool>& cancel) {
        return service->execute(request,cancel);
    },online,[this,context](OperationResult result) { complete(context,std::move(result)); },
    [this,message](bool connecting) { busy_message_=connecting?QStringLiteral("Connecting to Wi-Fi"):message; emit changed(); });
    if(!started) { status_="The previous Wi-Fi connection is still finishing. Try again shortly."; status_kind_="warning"; emit changed(); }
}
void AppController::complete(const Request& request,OperationResult result) {
    if(exiting_) { QCoreApplication::quit(); return; }
    if(request.command==Command::Initialize || (request.command==Command::SignIn && result.outcome==Outcome::SignedIn))
        covers_.resetSession(result.library.account);
    if(request.command==Command::Refresh && result.outcome==Outcome::Refreshed) covers_.refreshed();
    if(result.library.initialized) {
        initialized_=true; signed_in_=result.library.signed_in;
        if(signed_in_) signing_in_=false;
        library_.replace(std::move(result.library.books));
        if(!library_.find(selected_)) selected_={};
    }
    status_=resultMessage(result);
    const bool warning=result.outcome==Outcome::SessionInvalid || result.outcome==Outcome::UploadPending ||
        result.outcome==Outcome::SyncUnavailable || result.outcome==Outcome::NeedsNativeSettings ||
        result.outcome==Outcome::AppliedUnrecorded || result.outcome==Outcome::NativeCommitUncertain ||
        result.outcome==Outcome::Failed || !result.metadata_error.empty() || !result.recovery_warning.empty() ||
        !result.progress_warning.empty();
    status_kind_=status_.isEmpty()?QStringLiteral("none"):(warning?QStringLiteral("warning"):QStringLiteral("info"));
    if(result.outcome==Outcome::AppliedUnrecorded || result.outcome==Outcome::NativeCommitUncertain) {
        blocking_book_=request.book; blocking_acknowledged_=false;
        blocking_state_=result.outcome==Outcome::AppliedUnrecorded?BlockingState::AppliedUnrecorded:BlockingState::NativeCommitUncertain;
        status_=blockingMessage();
    } else if(request.command==Command::Sync && request.book==blocking_book_ && result.outcome!=Outcome::Failed) {
        blocking_state_=BlockingState::None; blocking_book_={}; blocking_acknowledged_=false;
    }
    if(request.command==Command::Sync || request.command==Command::Open || request.command==Command::Upload) {
        revision_=result.revision;
        choice_=result.sync_action==SyncAction::Conflict?
            (request.command==Command::Open?ChoiceState::OpenConflict:ChoiceState::SyncConflict):
            result.sync_action==SyncAction::BackwardUnsupported?ChoiceState::OpenConflict:ChoiceState::None;
    } else if(request.command!=Command::Resume) choice_=ChoiceState::None;
    emit changed(); prepareCovers();
    if(!result.open_path.empty()) {
        reader_opened_=true;
        if(!device_.open(QString::fromStdString(result.open_path))) {
            reader_opened_=false; status_="The native reader could not open this book."; status_kind_="warning"; emit changed();
        }
    }
}
void AppController::prepareCovers() {
    std::vector<LibraryEntry> visible;
    if(!busy() && !detail() && !signing_in_)
        for(int row=0;row<library_.rowCount();++row) visible.push_back(library_.at(row));
    covers_.show(std::move(visible));
}
void AppController::initialize() { if(!initialized_) submit(Request{}); }
void AppController::signIn(const QString& email,const QString& password) {
    if(busy() || !initialized_ || signed_in_) return;
    if(email.trimmed().isEmpty() || password.isEmpty()) { status_="Enter both your email and password first."; status_kind_="warning"; emit changed(); return; }
    if(email.trimmed().toUtf8().size()>=256 || password.toUtf8().size()>=1024) { status_="Email or password is too long."; status_kind_="warning"; emit changed(); return; }
    Request request; request.command=Command::SignIn; request.email=email.trimmed().toStdString(); request.password=password.toStdString(); submit(std::move(request));
}
void AppController::showSignIn() { if(!busy() && !signed_in_) { signing_in_=true;status_.clear();status_kind_="none";emit changed();prepareCovers(); } }
void AppController::signOut() { if(!signed_in_) return; covers_.resetSession(""); Request r; r.command=Command::SignOut; submit(r); }
void AppController::refreshLibrary() { if(!signed_in_) { scanDevice(); return; } Request r; r.command=Command::Refresh; submit(r); }
void AppController::scanDevice() { Request r; r.command=Command::Scan; submit(r); }
void AppController::selectBook(const QString& account,const QString& hash) {
    if(busy()) return;
    const BookId id{account.toStdString(),hash.toStdString()};
    const auto* entry=library_.find(id); if(!entry) return;
    selected_=id; revision_=entry->sync.revision; choice_=ChoiceState::None;
    status_=id==blocking_book_?blockingMessage():QString(); status_kind_=status_.isEmpty()?"none":"warning"; emit changed(); prepareCovers();
}
void AppController::cancelDecision() {
    if(!busy() && decision()) { choice_=ChoiceState::None; emit changed(); }
}
void AppController::acknowledgeBlocking() {
    if(blocking()) { blocking_acknowledged_=true; emit changed(); }
}
void AppController::runAction(const QString& command) {
    if(busy()) return;
    bool allowed=false;
    for(const auto& action:actions()) if(action.toMap()["command"].toString()==command) allowed=true;
    if(!allowed) return;
    if(command=="back") { back(); return; }
    if(command=="refresh") { refreshLibrary(); return; }
    if(command=="signin") {showSignIn();return;}
    if(command=="copies") {choice_=ChoiceState::LocalCopy;emit changed();return;}
    Request request; request.book=selected_; request.revision=revision_;
    if(command.startsWith("copy:")) {
        const auto* entry=library_.find(selected_);
        request.command=Command::SelectCopy;request.local_path=entry->book.copies.at(command.mid(5).toUInt()).path;
    } else if(command=="upload") request.command=Command::Upload;
    else if(command=="uploadCover") request.command=Command::UploadCover;
    else if(command=="download") request.command=Command::Download;
    else if(command=="offline" || command=="openPocketBook") request.command=Command::ReadOffline;
    else if(command=="open" || command=="openReadest") request.command=Command::Open;
    else request.command=Command::Sync;
    if(command=="openReadest" || command=="useReadest") request.choice=ProgressChoice::Readest;
    if(command=="usePocketBook") request.choice=ProgressChoice::PocketBook;
    submit(request);
}
void AppController::search(const QString& text) { if(!busy()) library_.search(text); }
void AppController::setAvailabilityFilter(int value) { if(!busy()) library_.filter(value); }
void AppController::setPageCapacity(int value) { library_.setCapacity(value); }
void AppController::turnPage(int direction) { if(!busy() && !detail()) library_.turnPage(direction); }
void AppController::back() {
    if(blocking()) return;
    if(decision()) { cancelDecision(); return; }
    if(busy()) close();
    else if(signing_in_) {signing_in_=false;emit changed();prepareCovers();}
    else if(detail()) { selected_={}; choice_=ChoiceState::None; emit changed(); prepareCovers(); }
    else close();
}
void AppController::close() { if(busy()) { exiting_=true; runner_.cancel(); emit changed(); } else QCoreApplication::quit(); }
void AppController::resume() {
    if(!busy() && initialized_ && reader_opened_) {
        reader_opened_=false; Request r; r.command=Command::Resume; submit(r);
    }
}
