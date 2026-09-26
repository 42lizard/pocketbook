#pragma once
#include "application.h"
#include "device_adapter.h"
#include <QObject>
#include <QTimer>
#include <chrono>
#include <thread>

class OperationRunner : public QObject {
public:
    using Task=std::function<readest::OperationResult(const std::atomic<bool>&)>;
    using Completion=std::function<void(readest::OperationResult)>;
    explicit OperationRunner(DeviceAccess device,QObject* parent=nullptr);
    ~OperationRunner() override;
    bool start(Task task,bool online,Completion complete,std::function<void(bool)> phase);
    bool busy() const { return busy_; }
    void cancel();
private:
    struct Connection { std::atomic<bool> done{false}; std::atomic<int> result{0}; };
    using Clock=std::chrono::steady_clock;
    DeviceAccess device_;
    QTimer timer_;
    std::thread worker_;
    std::atomic<bool> cancelled_{false},done_{false};
    bool busy_=false,connecting_=false,online_=false,awake_=false;
    std::shared_ptr<Connection> connection_;
    Clock::time_point deadline_,next_ping_;
    Task task_;
    Completion complete_;
    std::function<void(bool)> phase_;
    readest::OperationResult result_;
    void launch();
    void poll();
    void releaseAwake();
};
