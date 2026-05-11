#include "server.h"
#include"server_monitor.h"
#include<fstream>
#include<sstream>
#include<filesystem>

namespace fs = std::filesystem;


WebServer::WebServer(int port, const std::string& static_dir, int thread_count)
	:port_(port)
	,static_dir_(static_dir)
	,acceptor_(io_context_){
	
	if (thread_count <= 0) {
		thread_count = std::thread::hardware_concurrency();
		if (thread_count <= 0)thread_count = 4;
	}
	thread_pool_ = std::make_unique<ThreadPool>(thread_count);
	LOG_INFO("服务器初始化完成，{} 个工作线程", thread_count);
}

WebServer::~WebServer()
{
	stopServer();
}

bool WebServer::startServer()
{
	if (running_) {
		LOG_WARNING("服务器已在运行中");
		return false;
	}
	try {
		//创建端点
		tcp::endpoint endpoint(asio::ip::make_address("0.0.0.0"), port_);
		//打开acceptor
		acceptor_.open(endpoint.protocol());
		//设置选项，允许地址重用
		acceptor_.set_option(tcp::acceptor::reuse_address(true));
		//绑定到端点
		acceptor_.bind(endpoint);
		//开始监听
		acceptor_.listen(asio::socket_base::max_listen_connections);

		//启动I/O线程
		work_guard_ = std::make_unique<asio::io_context::work>(io_context_);

		running_ = true;

		//更新监听数据
		unsigned int io_thread_count = std::min(std::thread::hardware_concurrency(), 4u);
		
		for (unsigned int i = 0; i < io_thread_count; ++i) {
			io_threads_.emplace_back([this](){
				try {
					io_context_.run();
				}
				catch (const std::exception& e) {
					LOG_ERROR("IO线程异常：{}",e.what());
				}
			});
		}

		startAccept();

		LOG_INFO("服务器启动成功，监听端口：{}", port_);
		return true;
	}
	catch (const boost::system::system_error& e) {
		LOG_ERROR("服务器启动失败（Boost.Asio）:{}", e.what());
		running_ = false;
		return false;
	}
	catch (const std::exception& e) {
		LOG_ERROR("服务器启动失败:{}", e.what());
		running_ = false;
		return false;
	}
}

void WebServer::stopServer()
{
	if (!running_)return;

	running_ = false;
	LOG_INFO("正在停止服务器...");

	//停止新的连接
	boost::system::error_code ec;
	acceptor_.close(ec);
	if (ec) {
		LOG_WARNING("关闭 acceptor 时发生错误: {}", ec.message());
	}
	//销毁work_guard
	work_guard_.reset();
	
	io_context_.stop();

	//等待所有IO线程结束
	for (auto& thread : io_threads_) {
		if(thread.joinable()){
			thread.join();
		}
	}
	io_threads_.clear();
	//关闭所有活动连接
	{
		std::lock_guard<std::mutex> lock(connections_mutex_);
		for (auto& [id, conn] : connections_) {
			boost::system::error_code ec;
			conn->socket.close(ec);
		}
		connections_.clear();
	}
	//更新监控状态
	ServerMonitor::getInstance().setRunning(false);

	LOG_INFO("服务器已停止");

}

void WebServer::startAccept(){
	//创建一个新对象
	auto conn = std::make_shared<Connection>(tcp::socket(io_context_));

	//异步接受新连接
	//当有新的连接到达是，调用handleAccept
	acceptor_.async_accept(
		conn->socket,
		[this, conn](const boost::system::error_code& error) {
			handleAccept(conn, error);
		}
	);
}

void WebServer::handleAccept(std::shared_ptr<Connection> conn, const boost::system::error_code& error)
{
	if (!running_)return;
	if (!error) {
		//获取客户端信息
		boost::system::error_code ec;
		tcp::endpoint remote = conn->socket.remote_endpoint(ec);
		if (!ec) {
			conn->client_ip = remote.address().to_string();
			conn->client_port = remote.port();
		}
		else {
			conn->client_ip = "unknown";
			conn->client_port = 0;
		}
		// TCP_NODELAY: 禁用 Nagle 算法，减少延迟
		conn->socket.set_option(tcp::no_delay(true));

		// SO_KEEPALIVE: 启用 TCP 保活机制
		conn->socket.set_option(asio::socket_base::keep_alive(true));
	
		//生成ID并注册
		int conn_id = next_connection_id_++;
		
		{
			std::lock_guard<std::mutex> lock(connections_mutex_);

			// 检查是否达到最大连接数
			if (connections_.size() >= MAX_CONNECTIONS) {
				LOG_WARNING("达到最大连接数 ({})，拒绝新连接", MAX_CONNECTIONS);
				boost::system::error_code ec;
				conn->socket.close(ec);
				startAccept();  // 继续接受下一个连接
				return;
			}
			connections_[conn_id] = conn;
		}
		//更新监控数据
		auto& monitor = ServerMonitor::getInstance();
		monitor.incrementConnections();
		monitor.addConnection(conn_id, conn->client_ip, conn->client_port);

		LOG_DEBUG("新连接: {}:{} (ID={}, 活动连接={})",
			conn->client_ip, conn->client_port, conn_id,
			connections_.size());

		startRead(conn);
	}
	else {
		// 接受错误
		if (error != asio::error::operation_aborted) {
			LOG_ERROR("接受连接失败: {}", error.message());
		}
	}
	if (running_) {
		startAccept();
	}
}

void WebServer::startRead(std::shared_ptr<Connection> conn)
{
	//准备缓冲区
	auto buffer = conn->read_buffer.prepare(BUFFER_SIZE);

	//异步读取数据
	conn->socket.async_read_some(
		buffer,
		[this, conn](const boost::system::error_code& error,
					 size_t bytes_transferred) {
			handleRead(conn, error, bytes_transferred);
		}
	);
}

void WebServer::handleRead(std::shared_ptr<Connection> conn, const boost::system::error_code& error, size_t bytes_transferred)
{
	if (!running_) return;

	if (error) {
		// 正常关闭连接（EOF）或客户端重置连接
		if (error == asio::error::eof) {
			LOG_DEBUG("客户端正常关闭连接");
		}
		else if (error == asio::error::connection_reset) {
			LOG_DEBUG("客户端重置连接");
		}
		else if (error != asio::error::operation_aborted) {
			LOG_ERROR("读取错误: {}", error.message());
		}
		cleanupConnection(conn);
		return;
	}

	// ========== 处理接收到的数据 ==========
	// commit() 确认数据已写入缓冲区
	conn->read_buffer.commit(bytes_transferred);
	conn->last_activity = std::chrono::steady_clock::now();

	// 更新监控数据
	auto& monitor = ServerMonitor::getInstance();
	monitor.addBytesReceived(bytes_transferred);

	// 获取可读数据的指针和大小
	const char* data = asio::buffer_cast<const char*>(conn->read_buffer.data());
	size_t available = conn->read_buffer.size();

	// ========== 尝试解析 HTTP 请求 ==========
	auto request_opt = conn->parser.parse(data, available);

	if (request_opt) {
		// 解析成功！记录请求信息
		auto& request = *request_opt;

		// 检查是否保持连接
		conn->keep_alive = request.keep_alive;

		// 消耗已解析的数据
		conn->read_buffer.consume(available);

		LOG_DEBUG("收到请求: {} {}",
			request.method == HttpMethod::GET ? "GET" :
			request.method == HttpMethod::HEAD ? "HEAD" : "UNKNOWN",
			request.path);

		// ========== 在线程池中处理请求 ==========
		// 使用 shared_ptr 保证连接对象在处理期间不被释放
		thread_pool_->enqueue([this, conn]() {
			processRequest(conn);
			});

	}
	else {
		// 数据不完整，继续读取
		startRead(conn);
	}
}

void WebServer::handleWrite(std::shared_ptr<Connection> conn, const boost::system::error_code& error, size_t bytes_transferred)
{
	if (error) {
		if (error != asio::error::operation_aborted) {
			LOG_ERROR("写入错误: {}", error.message());
		}
		cleanupConnection(conn);
		return;
	}

	LOG_DEBUG("响应发送完成: {} bytes", bytes_transferred);

	// 清空写缓冲区
	conn->write_buffer.clear();

	// ========== 决定是否保持连接 ==========
	if (conn->keep_alive && conn->requests_handled < 100) {
		// 保持连接，继续读取下一个请求
		startRead(conn);
	}
	else {
		// 关闭连接
		if (conn->requests_handled >= 100) {
			LOG_DEBUG("达到最大请求数限制，关闭连接");
		}
		cleanupConnection(conn);
	}
}

void WebServer::processRequest(std::shared_ptr<Connection> conn)
{
	auto& monitor = ServerMonitor::getInstance();
	monitor.incrementRequests();

	// ========== 重新解析完整请求（简化版） ==========
	// 实际项目中应该在 handleRead 中传递解析好的对象
	auto& parser = conn->parser;

	// 简化：假设请求在缓冲区中（需要从 Connection 获取）
	// 这里为了演示，直接使用默认值
	std::string path = "/index.html";

	// ========== 安全检查：防止目录遍历攻击 ==========
	if (path.find("..") != std::string::npos ||
		path.find("//") != std::string::npos ||
		path.find("\\") != std::string::npos) {
		LOG_WARNING("检测到可疑路径访问: {}", path);
		sendErrorResponse(conn, 403);  // Forbidden
		return;
	}

	// 根路径重定向到 index.html
	if (path == "/") {
		path = "/index.html";
	}

	// ========== 构建文件系统路径 ==========
	std::string file_path = static_dir_ + path;

	// ========== 检查文件是否存在 ==========
	if (!fs::exists(file_path)) {
		LOG_DEBUG("文件不存在: {}", file_path);
		sendErrorResponse(conn, 404);  // Not Found
		return;
	}

	// 确保是普通文件（不是目录）
	if (!fs::is_regular_file(file_path)) {
		LOG_DEBUG("路径不是文件: {}", file_path);
		sendErrorResponse(conn, 404);
		return;
	}

	// ========== 读取文件内容 ==========
	std::string content = readFile(file_path);
	if (content.empty()) {
		LOG_ERROR("读取文件失败: {}", file_path);
		sendErrorResponse(conn, 500);  // Internal Server Error
		return;
	}

	// ========== 获取 MIME 类型 ==========
	std::string mime_type = getMimeType(file_path);

	LOG_DEBUG("响应: {} ({} bytes, type: {})",
		file_path, content.size(), mime_type);

	// ========== 构建 HTTP 响应 ==========
	std::string response = buildHttpResponse(
		"200 OK",      // 状态行
		mime_type,      // Content-Type
		content,        // 响应体
		conn->keep_alive  // 是否保持连接
	);

	// ========== 发送响应 ==========
	sendResponse(conn, response);

	// ========== 更新统计 ==========
	conn->requests_handled++;
	monitor.updateConnectionRequest(0, path);  // TODO: 传入正确的 fd
	monitor.addBytesSent(response.size());
}

void WebServer::sendResponse(std::shared_ptr<Connection> conn, const std::string& response)
{
	conn->write_buffer = response;

	// 异步写入所有数据
	asio::async_write(
		conn->socket,
		asio::buffer(conn->write_buffer),
		[this, conn](const boost::system::error_code& error,
			size_t bytes_transferred) {
				handleWrite(conn, error, bytes_transferred);
		}
	);
}

void WebServer::sendErrorResponse(std::shared_ptr<Connection> conn, int status_code)
{
	std::string status_text;
	switch (status_code) {
	case 400: status_text = "400 Bad Request"; break;
	case 403: status_text = "403 Forbidden"; break;
	case 404: status_text = "404 Not Found"; break;
	case 405: status_text = "405 Method Not Allowed"; break;
	case 408: status_text = "408 Request Timeout"; break;
	case 413: status_text = "413 Payload Too Large"; break;
	case 500: status_text = "500 Internal Server Error"; break;
	case 503: status_text = "503 Service Unavailable"; break;
	default:  status_text = "500 Internal Server Error"; break;
	}

	// 构建简单的 HTML 错误页面
	std::string body =
		"<!DOCTYPE html>\n"
		"<html><head><meta charset='UTF-8'><title>" + status_text + "</title></head>\n"
		"<body style='font-family:Arial;text-align:center;padding:50px;'>\n"
		"<h1>" + status_text + "</h1>\n"
		"<hr><p>Mini Web Server 1.0</p>\n"
		"</body></html>";

	std::string response = buildHttpResponse(status_text, "text/html", body, false);
	sendResponse(conn, response);
}

void WebServer::cleanupConnection(std::shared_ptr<Connection> conn)
{
	int conn_id = 0;

	// ========== 从连接表中移除 ==========
	{
		std::lock_guard<std::mutex> lock(connections_mutex_);
		for (auto it = connections_.begin(); it != connections_.end(); ++it) {
			if (it->second == conn) {
				conn_id = it->first;
				connections_.erase(it);
				break;
			}
		}
	}

	// ========== 关闭 Socket ==========
	boost::system::error_code ec;
	conn->socket.close(ec);

	// ========== 更新监控数据 ==========
	auto& monitor = ServerMonitor::getInstance();
	if (conn_id > 0) {
		monitor.removeConnection(conn_id);
	}
	monitor.decrementConnections();

	LOG_DEBUG("连接关闭: ID={}, 处理请求数={}, 活动连接={}",
		conn_id, conn->requests_handled, connections_.size());
}

std::string WebServer::buildHttpResponse(const std::string& status, const std::string& content_type, const std::string& body, bool keep_alive)
{
	std::stringstream response;

	// 状态行
	response << "HTTP/1.1 " << status << "\r\n";

	// 服务器信息
	response << "Server: MiniWebServer/1.0 (Windows)\r\n";

	// 内容类型和长度
	response << "Content-Type: " << content_type << "; charset=utf-8\r\n";
	response << "Content-Length: " << body.size() << "\r\n";

	// 连接控制
	if (keep_alive) {
		response << "Connection: keep-alive\r\n";
		response << "Keep-Alive: timeout=5, max=100\r\n";
	}
	else {
		response << "Connection: close\r\n";
	}

	// 缓存控制
	response << "Cache-Control: public, max-age=3600\r\n";

	// 响应头结束（空行分隔）
	response << "\r\n";

	// 响应体
	response << body;

	return response.str();
}

std::string WebServer::getMimeType(const std::string& path)
{
	static const std::unordered_map<std::string, std::string> mime_types = {
		// 文本类型
		{".html", "text/html"},
		{".htm",  "text/html"},
		{".css",  "text/css"},
		{".txt",  "text/plain"},
		{".csv",  "text/csv"},
		{".xml",  "application/xml"},

		// 脚本类型
		{".js",   "application/javascript"},
		{".mjs",  "application/javascript"},
		{".json", "application/json"},

		// 图片类型
		{".png",  "image/png"},
		{".jpg",  "image/jpeg"},
		{".jpeg", "image/jpeg"},
		{".gif",  "image/gif"},
		{".bmp",  "image/bmp"},
		{".ico",  "image/x-icon"},
		{".svg",  "image/svg+xml"},
		{".webp", "image/webp"},

		// 字体类型
		{".woff",  "font/woff"},
		{".woff2", "font/woff2"},
		{".ttf",   "font/ttf"},
		{".otf",   "font/otf"},

		// 应用程序类型
		{".pdf",  "application/pdf"},
		{".zip",  "application/zip"},
		{".tar",  "application/x-tar"},
		{".gz",   "application/gzip"},
		{".mp3",  "audio/mpeg"},
		{".mp4",  "video/mp4"},
		{".ogg",  "audio/ogg"},
	};

	// 提取文件扩展名
	size_t dot_pos = path.rfind('.');
	if (dot_pos != std::string::npos) {
		std::string ext = path.substr(dot_pos);

		// 转换为小写（兼容大小写）
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		auto it = mime_types.find(ext);
		if (it != mime_types.end()) {
			return it->second;
		}
	}

	// 默认二进制流类型
	return "application/octet-stream";
}

std::string WebServer::readFile(const std::string& path)
{
	try {
		// 以二进制模式打开文件
		std::ifstream file(path, std::ios::binary | std::ios::ate);

		if (!file.is_open()) {
			LOG_ERROR("无法打开文件: {}", path);
			return "";
		}

		// 获取文件大小
		std::streamsize size = file.tellg();

		// 检查文件大小限制（最大 100MB）
		constexpr std::streamsize MAX_FILE_SIZE = 100 * 1024 * 1024;
		if (size > MAX_FILE_SIZE) {
			LOG_WARNING("文件过大 ({} bytes), 最大允许 {} bytes",
				size, MAX_FILE_SIZE);
			return "";
		}

		// 回到文件开头
		file.seekg(0, std::ios::beg);

		// 创建足够大的缓冲区
		std::string buffer(static_cast<size_t>(size), '\0');

		// 读取文件内容
		if (!file.read(buffer.data(), size)) {
			LOG_ERROR("读取文件失败: {}", path);
			return "";
		}

		return buffer;

	}
	catch (const std::exception& e) {
		LOG_ERROR("读取文件异常: {} - {}", path, e.what());
		return "";
	}
}
