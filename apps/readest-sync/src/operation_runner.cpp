#include "operation_runner.h"
using namespace readest;
OperationRunner::OperationRunner(DeviceAccess device,QObject* parent):QObject(parent),device_(std::move(device)) {
    QObject::connect(&timer_,&QTimer::timeout,this,[this] { poll(); });
}
OperationRunner::~OperationRunner() {
    cancelled_=true; timer_.stop(); task_=nullptr;
    if(worker_.joinable()) worker_.join();
}
void OperationRunner::cancel() { cancelled_=true; }
bool OperationRunner::start(Task task,bool online,Completion complete,std::function<void(bool)> phase) {
    if(busy_) return false;
    task_=std::move(task); complete_=std::move(complete); phase_=std::move(phase);
    result_={}; cancelled_=false; done_=false; busy_=true; online_=online; connecting_=online;
    if(online) {
        if(connection_ && connection_->done) connection_.reset();
        next_ping_=Clock::now()+std::chrono::milliseconds(device_.keepaliveMs);
        device_.ping();
        if(device_.networkReady && device_.networkReady()) {
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
                busy_=false; connecting_=false; task_=nullptr; complete_=nullptr; phase_=nullptr;
                return false;
            }
            deadline_=Clock::now()+std::chrono::milliseconds(device_.connectionTimeoutMs);
        } else if(!connection_->done && Clock::now()>=deadline_) {
            busy_=false; connecting_=false; task_=nullptr; complete_=nullptr; phase_=nullptr;
            return false;
        }
    } else { phase_(false); launch(); }
    timer_.start(200); return true;
}
void OperationRunner::launch() {
    try {
        auto task=std::move(task_); task_=nullptr;
        worker_=std::thread([this,task=std::move(task)] {
            set_http_cancellation(&cancelled_);
            try { result_=task(cancelled_); }
            catch(const std::exception& e) { result_.outcome=Outcome::Failed; result_.error=e.what(); }
            catch(...) { result_.outcome=Outcome::Failed; result_.error="Operation failed."; }
            set_http_cancellation(nullptr); done_=true;
        });
    } catch(const std::exception& e) { result_.outcome=Outcome::Failed; result_.error=e.what(); done_=true; }
}
void OperationRunner::poll() {
    if(!busy_) return;
    if(connecting_) {
        if(cancelled_) {
            result_.outcome=Outcome::Cancelled; result_.error="Cancelled.";
            task_=nullptr; connecting_=false; done_=true;
        } else if(device_.networkReady && device_.networkReady()) {
            // Some firmware connection attempts establish Wi-Fi without
            // delivering a callback. Keep its token alive for a late reply,
            // but do not hold up the actual request once the route is ready.
            connecting_=false; phase_(false); launch();
        } else if(!connection_->done && Clock::now()>=deadline_) {
            result_.outcome=Outcome::Failed;
            result_.error="Wi-Fi connection timed out. Check Wi-Fi in PocketBook settings, then retry or use Read offline.";
            task_=nullptr; connecting_=false; done_=true;
        } else if(connection_->done) {
            connecting_=false;
            const auto status=connection_->result.load();
            connection_.reset();
            if(status==0) { phase_(false); launch(); }
            else {
                result_.outcome=Outcome::Failed;
                result_.error="Wi-Fi connection failed (network "+std::to_string(status)+"). Try again or use Read offline.";
                task_=nullptr; done_=true;
            }
        }
    }
    if(online_ && !done_ && !cancelled_ && Clock::now()>=next_ping_) {
        device_.ping(); next_ping_=Clock::now()+std::chrono::milliseconds(device_.keepaliveMs);
    }
    if(!done_) return;
    if(worker_.joinable()) worker_.join();
    timer_.stop(); busy_=false; online_=false; phase_=nullptr;
    auto complete=std::move(complete_); complete_=nullptr;
    complete(std::move(result_));
}
