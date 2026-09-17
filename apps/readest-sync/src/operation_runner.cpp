#include "operation_runner.h"
#include "network_trace.h"
using namespace readest;
OperationRunner::OperationRunner(DeviceAccess device,QObject* parent):QObject(parent),device_(std::move(device)) {
    QObject::connect(&timer_,&QTimer::timeout,this,[this] { poll(); });
}
OperationRunner::~OperationRunner() {
    cancelled_=true; timer_.stop(); task_=nullptr;
    if(worker_.joinable()) worker_.join();
    releaseAwake();
}
void OperationRunner::releaseAwake() {
    if(online_ && device_.keepAwake) device_.keepAwake(false);
    online_=false;
}
void OperationRunner::cancel() { cancelled_=true; }
bool OperationRunner::start(Task task,bool online,Completion complete,std::function<void(bool)> phase) {
    if(busy_) return false;
    task_=std::move(task); complete_=std::move(complete); phase_=std::move(phase);
    result_={}; cancelled_=false; done_=false; busy_=true; online_=online; connecting_=online;
    if(online) {
        // PocketBook standby pauses both background work and the monotonic
        // timeout clock. Hold it off until this online operation finishes.
        if(device_.keepAwake) device_.keepAwake(true);
        networkTrace("runner.online");
        if(connection_ && connection_->done) connection_.reset();
        next_ping_=Clock::now()+std::chrono::milliseconds(device_.keepaliveMs);
        if(device_.networkReady && device_.networkReady()) {
            device_.ping();
            connecting_=false; phase_(false); launch(); timer_.start(200); return true;
        }
        phase_(true);
        // Cancelling a cover request cannot cancel the firmware's connection.
        // Its successor waits for that same callback instead of starting a
        // second connection (which the device adapter would reject).
        if(!connection_) {
            connection_=std::make_shared<Connection>();
            auto token=connection_;
            if(!device_.connect([token](int result) { token->result=result; token->done=true; })) {
                connection_.reset();
                releaseAwake();
                busy_=false; connecting_=false; task_=nullptr; complete_=nullptr; phase_=nullptr;
                return false;
            }
            deadline_=Clock::now()+std::chrono::milliseconds(device_.connectionTimeoutMs);
        } else if(Clock::now()>=deadline_) {
            networkTrace("runner.previous-timeout");
            releaseAwake();
            busy_=false; connecting_=false; task_=nullptr; complete_=nullptr; phase_=nullptr;
            return false;
        }
    } else { phase_(false); launch(); }
    timer_.start(200); return true;
}
void OperationRunner::launch() {
    if(online_) networkTrace("runner.http-ready");
    try {
        auto task=std::move(task_); task_=nullptr;
        worker_=std::thread([this,task=std::move(task)] {
            try { result_=task(cancelled_); }
            catch(const std::exception& e) { result_.outcome=Outcome::Failed; result_.error=e.what(); }
            catch(...) { result_.outcome=Outcome::Failed; result_.error="Operation failed."; }
            done_=true;
        });
    } catch(const std::exception& e) { result_.outcome=Outcome::Failed; result_.error=e.what(); done_=true; }
}
void OperationRunner::poll() {
    if(!busy_) return;
    if(connecting_) {
        if(cancelled_) {
            networkTrace("runner.cancelled");
            result_.outcome=Outcome::Cancelled; result_.error="Cancelled.";
            task_=nullptr; connecting_=false; done_=true;
        } else if(device_.networkReady && device_.networkReady()) {
            // Some firmware connection attempts establish Wi-Fi without
            // delivering a callback. Keep its token alive for a late reply,
            // but do not hold up the actual request once the route is ready.
            connecting_=false;device_.ping();phase_(false); launch();
        } else if(Clock::now()>=deadline_) {
            networkTrace("runner.timeout",connection_->done?connection_->result.load():-9999);
            result_.outcome=Outcome::Failed;
            result_.error="Wi-Fi connection timed out. Check Wi-Fi in PocketBook settings, then retry or use Read offline.";
            task_=nullptr; connecting_=false; done_=true;
        } else if(connection_->done && (connection_->result!=0 || !device_.networkReady)) {
            connecting_=false;
            const auto status=connection_->result.load();
            connection_.reset();
            if(status==0) { device_.ping();phase_(false); launch(); }
            else {
                result_.outcome=Outcome::Failed;
                result_.error="Wi-Fi connection failed (network "+std::to_string(status)+"). Try again or use Read offline.";
                task_=nullptr; done_=true;
            }
        }
    }
    if(online_ && !connecting_ && !done_ && !cancelled_ && Clock::now()>=next_ping_) {
        device_.ping(); next_ping_=Clock::now()+std::chrono::milliseconds(device_.keepaliveMs);
    }
    if(!done_) return;
    if(worker_.joinable()) worker_.join();
    timer_.stop(); busy_=false; releaseAwake(); phase_=nullptr;
    auto complete=std::move(complete_); complete_=nullptr;
    complete(std::move(result_));
}
