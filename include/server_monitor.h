#pragma once
//服务器监控
#include<QObject>
#include<QString>
#include<QDateTime>
#include<atomic>
#include<chrono>
#include<mutex>
#include<string>
#include<vector>
#include<unordered_map>

//信息连接结构体
struct ConnectionInfo {
    int fd = -1;// 连接文件描述符
    std::string client_ip;// 客户端 IP 地址
    uint16_t client_port = 0;// 客户端端口
    std::chrono::steady_clock::time_point connected_time; // 连接建立时间
    std::chrono::steady_clock::time_point last_activity; // 最后活动时间
    size_t bytes_received = 0;// 接收字节数
    size_t bytes_sent = 0;// 发送字节数
    size_t requests_handled = 0;// 处理的请求数
    std::string current_path;// 当前请求路径

    //UI显示
    QString q_client_ip;// Qt 字符串版 IP
    QString q_connected_time;// Qt 字符串版连接时间
    QString q_current_path;// Qt 字符串版当前路径
    int q_requests_handled = 0; // Qt 整型版请求数
    qint64 q_bytes_sent = 0; // Qt 整型版发送字节数

};
//服务监听类
class ServerMonitor : public QObject {
    Q_OBJECT
public:
    static ServerMonitor& getInstance();

    //信息更新
    void incrementConnections();// 增加活动连接计数
    void decrementConnections();// 减少活动连接计数
    void incrementRequests();// 增加请求计数
    void addBytesReceived(size_t bytes);// 累加接收字节数
    void addBytesSent(size_t bytes);// 累加发送字节数
    void setStartTime();// 设置服务器启动时间

    void addConnection(int fd, const std::string& ip, uint16_t port);
    void removeConnection(int fd);
    void updateConnectionActivity(int fd, size_t bytes_received, size_t bytes_sent);
    void updateConnectionRequest(int fd, const std::string& path);

    //统计数据结构
    struct Stats {
        uint64_t total_connections = 0;// 总连接数
        uint64_t active_connections = 0;// 当前活动连接数
        uint64_t total_requests = 0;// 总请求数
        uint64_t bytes_received = 0;// 总接收字节数
        uint64_t bytes_sent = 0;// 总发送字节数
        double requests_per_second = 0.0;// 每秒请求数（QPS）
        double bytes_per_second = 0.0;// 每秒传输字节数
        std::chrono::system_clock::time_point start_time;// 启动时间
        std::chrono::nanoseconds uptime;// 运行时长
    };

    Stats getStats();//获取统计数据
    std::vector<ConnectionInfo> getActiveConnections();// 获取活动连接列表

    //服务器控制
    void setRunning(bool running);
    bool isRunning() const;
    void setPort(int port);
    int getPort() const;
    void setStaticDir(const std::string& dir);
    std::string getStaticDir() const;
    void setThreadCount(int count);
    int getThreadCount() const;

    //信号
signals:
    //刷新ui
    void statsUpdated();
    //建立新连接
    void connectionAdded(int fd);
    //连接断开
    void connectionRemoved(int fd);
    //服务器已启动
    void serverStarted();
    //服务器已停止
    void serverStopped();
    //发生错误
    void errorOccurred(const QString& error);


private:
    ServerMonitor();
    ~ServerMonitor() = default;

    ServerMonitor(const ServerMonitor&) = delete;
    ServerMonitor& operator=(const ServerMonitor&) = delete;

    //原子操作
    std::atomic<uint64_t> total_connections_{ 0 };
    std::atomic<uint64_t> active_connections_{ 0 };
    std::atomic<uint64_t> total_requests_{ 0 };
    std::atomic<uint64_t> bytes_received_{ 0 };
    std::atomic<uint64_t> bytes_sent_{ 0 };

    //非原子操作（锁内访问）
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_stats_time_;
    uint64_t last_requests_{ 0 };
    uint64_t last_bytes_{ 0 };
    double requests_per_second_{ 0.0 };
    double bytes_per_second_{ 0.0 };

    //互斥锁
    std::mutex connections_mutex_;
    std::unordered_map<int, ConnectionInfo> connections_;

    //服务器状态
    std::atomic<bool> running_{ false };
    int port_{ 8080 };
    std::string static_dir_{ "./static" };
    int thread_count_{ 4 };

    void updateRates();



};