#include "server_monitor.h"
#include <sstream>
#include <iomanip>

ServerMonitor& ServerMonitor::getInstance() {
    static ServerMonitor instance;
    return instance;
}

ServerMonitor::ServerMonitor() {
    start_time_ = std::chrono::steady_clock::now();
    last_stats_time_ = start_time_;
}

void ServerMonitor::incrementConnections() {
    total_connections_++;
    active_connections_++;
    emit statsUpdated();  // 触发 Qt 信号
}

void ServerMonitor::decrementConnections() {
    if (active_connections_ > 0) {
        active_connections_--;
    }
    emit statsUpdated();
}

void ServerMonitor::incrementRequests() {
    total_requests_++;
    updateRates();  // 更新速率统计
    emit statsUpdated();
}

void ServerMonitor::addBytesReceived(size_t bytes) {
    bytes_received_ += bytes;
    emit statsUpdated();
}

void ServerMonitor::addBytesSent(size_t bytes) {
    bytes_sent_ += bytes;
    emit statsUpdated();
}

void ServerMonitor::setStartTime() {
    start_time_ = std::chrono::steady_clock::now();
    last_stats_time_ = start_time_;
    last_requests_ = 0;
    last_bytes_ = 0;
    requests_per_second_ = 0.0;
    bytes_per_second_ = 0.0;
}

void ServerMonitor::addConnection(int fd, const std::string& ip, uint16_t port) {
    std::lock_guard<std::mutex> lock(connections_mutex_);

    ConnectionInfo info;
    info.fd = fd;
    info.client_ip = ip;
    info.client_port = port;
    info.connected_time = std::chrono::steady_clock::now();
    info.last_activity = info.connected_time;
    info.bytes_received = 0;
    info.bytes_sent = 0;
    info.requests_handled = 0;

    // 填充 Qt 兼容字段
    info.q_client_ip = QString::fromStdString(ip);
    info.q_connected_time = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    connections_[fd] = info;
    emit connectionAdded(fd);
}

void ServerMonitor::removeConnection(int fd) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.erase(fd);
    emit connectionRemoved(fd);
}

void ServerMonitor::updateConnectionActivity(int fd, size_t received, size_t sent) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(fd);
    if (it != connections_.end()) {
        it->second.last_activity = std::chrono::steady_clock::now();
        it->second.bytes_received += received;
        it->second.bytes_sent += sent;
        it->second.q_bytes_sent = static_cast<qint64>(it->second.bytes_sent);
    }
}

void ServerMonitor::updateConnectionRequest(int fd, const std::string& path) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(fd);
    if (it != connections_.end()) {
        it->second.requests_handled++;
        it->second.current_path = path;
        it->second.q_requests_handled = static_cast<int>(it->second.requests_handled);
        it->second.q_current_path = QString::fromStdString(path);
    }
}

ServerMonitor::Stats ServerMonitor::getStats() {
    Stats stats;
    stats.total_connections = total_connections_.load();
    stats.active_connections = active_connections_.load();
    stats.total_requests = total_requests_.load();
    stats.bytes_received = bytes_received_.load();
    stats.bytes_sent = bytes_sent_.load();
    stats.requests_per_second = requests_per_second_;
    stats.bytes_per_second = bytes_per_second_;
    stats.start_time = std::chrono::system_clock::now();
    stats.uptime = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start_time_);
    return stats;
}

std::vector<ConnectionInfo> ServerMonitor::getActiveConnections() {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    std::vector<ConnectionInfo> result;
    result.reserve(connections_.size());
    for (const auto& [fd, info] : connections_) {
        result.push_back(info);
    }
    return result;
}
void ServerMonitor::updateRates() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - last_stats_time_).count();

    // 每秒钟更新一次速率
    if (elapsed >= 1.0) {
        requests_per_second_ = (total_requests_ - last_requests_) / elapsed;
        bytes_per_second_ = (bytes_sent_ - last_bytes_) / elapsed;

        last_stats_time_ = now;
        last_requests_ = total_requests_.load();
        last_bytes_ = bytes_sent_.load();
    }
}

void ServerMonitor::setRunning(bool running) {
    running_ = running;
    if (running) {
        emit serverStarted();
    }
    else {
        emit serverStopped();
    }
}


bool ServerMonitor::isRunning() const { return running_.load(); }

void ServerMonitor::setPort(int port) { port_ = port; }
int ServerMonitor::getPort() const { return port_; }

void ServerMonitor::setStaticDir(const std::string& dir) { static_dir_ = dir; }
std::string ServerMonitor::getStaticDir() const { return static_dir_; }

void ServerMonitor::setThreadCount(int count) { thread_count_ = count; }
int ServerMonitor::getThreadCount() const { return thread_count_; }