#pragma once
#include <boost/asio.hpp>//跨平台网络库（替代epoll/socket）
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/system/error_code.hpp>
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <mutex>
#include "thread_pool.h"
#include "http_parser.h"
#include "logger.h"
#include "router.h"

namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

constexpr int MAX_CONNECTIONS = 10000;//最大支持10000并发链接
constexpr int BUFFER_SIZE = 65536;  // 64KB缓冲

//客户端连接结构体
struct Connection {
	tcp::socket socket;//跨平台socket
	asio::streambuf read_buffer;//读缓存区
	std::string write_buffer;//写缓
	HttpParser parser;//独立HTTP解析器
	bool keep_alive = false;//是否长链接
	std::chrono::steady_clock::time_point last_activity;//最后活跃时间
	size_t requests_handled = 0;//此链接已处理请求
	std::string client_ip;//客户端ip
	uint16_t client_port = 0;//客户端口

<<<<<<< HEAD
	std::optional<HttpRequest> current_request;

	explicit Connection(tcp::socket&& sock)
		: socket(std::move(sock))
		, last_activity(std::chrono::steady_clock::now()) {
	}

	virtual ~Connection() = default;
	virtual tcp::socket& getSocket() { return socket; }
};

// SSL 连接结构体
struct SslConnection : public Connection {
	ssl::stream<tcp::socket> ssl_socket;
	bool ssl_handshaked = false;

	explicit SslConnection(tcp::socket&& sock, ssl::context& ctx)
		: Connection(std::move(sock))
		, ssl_socket(std::move(Connection::socket), ctx) {
	}

	tcp::socket& getSocket() override {
		return ssl_socket.next_layer();  // 返回 SSL 流底层 TCP socket
	}
=======
    std::optional<HttpRequest> current_request;
    explicit Connection(tcp::socket&& sock)
        : socket(std::move(sock))
        , last_activity(std::chrono::steady_clock::now()) {
    }
>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510
};

class WebServer {
public:
	WebServer(int port, const std::string& static_dir, int thread_count = 0,
	          int ssl_port = 0, const std::string& cert_file = "", const std::string& key_file = "");
	~WebServer();

	bool startServer();
	void stopServer();
	bool isRunning() const { return running_.load(); }

	// 获取路由表（在服务器启动前注册路由）
	Router& router() { return router_; }

private:
	int port_;
	int ssl_port_;
	std::string static_dir_;
	std::string cert_file_;
	std::string key_file_;
	std::unique_ptr<ThreadPool> thread_pool_;
	std::atomic<bool> running_{ false };

	// Boost.Asio组件
	asio::io_context io_context_;//事件循环
	std::unique_ptr<asio::io_context::work> work_guard_;//防止事件循环提前退出
	tcp::acceptor acceptor_;// HTTP 监听器
	std::unique_ptr<tcp::acceptor> ssl_acceptor_;// HTTPS 监听器（可选）
	std::unique_ptr<ssl::context> ssl_context_;// SSL 上下文
	std::vector<std::thread> io_threads_;//Asio多线程IO模型

	// 连接管理
	std::unordered_map<int, std::shared_ptr<Connection>> connections_;//连接表
	std::mutex connections_mutex_;
	std::atomic<int> next_connection_id_{ 1 };//链接ID自增（线程安全）

	// 路由系统
	Router router_;

	// HTTP 处理
	void startAccept();
	void handleAccept(std::shared_ptr<Connection> conn, const boost::system::error_code& error);
	void startRead(std::shared_ptr<Connection> conn);
	void handleRead(std::shared_ptr<Connection> conn, const boost::system::error_code& error,
		size_t bytes_transferred);
	void handleWrite(std::shared_ptr<Connection> conn, const boost::system::error_code& error,
		size_t bytes_transferred);

<<<<<<< HEAD
	// SSL/HTTPS 处理
	bool initSsl();
	void startSslAccept();
	void handleSslAccept(std::shared_ptr<SslConnection> conn, const boost::system::error_code& error);
	void startSslHandshake(std::shared_ptr<SslConnection> conn);
	void handleSslHandshake(std::shared_ptr<SslConnection> conn, const boost::system::error_code& error);
	void startSslRead(std::shared_ptr<SslConnection> conn);
	void handleSslRead(std::shared_ptr<SslConnection> conn, const boost::system::error_code& error,
		size_t bytes_transferred);
	void handleSslWrite(std::shared_ptr<SslConnection> conn, const boost::system::error_code& error,
		size_t bytes_transferred);

	// 共用
	void processRequest(std::shared_ptr<Connection> conn);
	void sendResponse(std::shared_ptr<Connection> conn, const std::string& response);
	void sendSslResponse(std::shared_ptr<SslConnection> conn, const std::string& response);
	void sendErrorResponse(std::shared_ptr<Connection> conn, int status_code,
		const std::string& extra_headers = "");
	void sendSslErrorResponse(std::shared_ptr<SslConnection> conn, int status_code,
		const std::string& extra_headers = "");
	void cleanupConnection(std::shared_ptr<Connection> conn);
	void cleanupSslConnection(std::shared_ptr<SslConnection> conn);

	std::string buildHttpResponse(const std::string& status,
		const std::string& content_type,
		const std::string& body,
		bool keep_alive,
		bool cacheable = true);

	std::string getMimeType(const std::string& path);
	std::string readFile(const std::string& path);
};
=======
    std::string getMimeType(const std::string& path);
    std::string readFile(const std::string& path);
};
>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510
