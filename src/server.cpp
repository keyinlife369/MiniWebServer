#include "server.h"
#include"server_monitor.h"
#include<fstream>
#include<sstream>
#include<filesystem>

namespace fs = std::filesystem;


WebServer::WebServer(int port, const std::string& static_dir, int thread_count,
                     int ssl_port, const std::string& cert_file, const std::string& key_file)
	:port_(port)
	,ssl_port_(ssl_port)
	,static_dir_(static_dir)
	,cert_file_(cert_file)
	,key_file_(key_file)
	,acceptor_(io_context_){

	if (thread_count <= 0) {
		thread_count = std::thread::hardware_concurrency();
		if (thread_count <= 0)thread_count = 4;
	}
	thread_pool_ = std::make_unique<ThreadPool>(thread_count);

	if (ssl_port_ > 0) {
		LOG_INFO("服务器初始化完成，{} 个工作线程 (HTTP:{}, HTTPS:{})",
			thread_count, port_, ssl_port_);
	} else {
		LOG_INFO("服务器初始化完成，{} 个工作线程", thread_count);
	}
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

		// 启动 SSL/HTTPS
		if (ssl_port_ > 0 && initSsl()) {
			tcp::endpoint ssl_endpoint(asio::ip::make_address("0.0.0.0"), ssl_port_);
			ssl_acceptor_ = std::make_unique<tcp::acceptor>(io_context_, ssl_endpoint);
			ssl_acceptor_->set_option(tcp::acceptor::reuse_address(true));
			startSslAccept();
			LOG_INFO("HTTPS 启动成功，监听端口：{}", ssl_port_);
		} else if (ssl_port_ > 0) {
			LOG_WARNING("HTTPS 启动失败，仅提供 HTTP 服务");
		}

		LOG_INFO("服务器启动成功，HTTP端口：{}", port_);
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
		LOG_WARNING("关闭 HTTP acceptor 时发生错误: {}", ec.message());
	}
	if (ssl_acceptor_) {
		ssl_acceptor_->close(ec);
		LOG_INFO("HTTPS acceptor 已关闭");
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
				conn->getSocket().close(ec);
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
void WebServer::handleRead(std::shared_ptr<Connection> conn,
	const boost::system::error_code& error,
	size_t bytes_transferred) {

<<<<<<< HEAD
=======
void WebServer::handleRead(std::shared_ptr<Connection> conn,
	const boost::system::error_code& error,
	size_t bytes_transferred) {
>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510
	if (!running_) return;

	if (error) {
		if (error != asio::error::eof && error != asio::error::connection_reset) {
			LOG_ERROR("读取错误: {}", error.message());
		}
		cleanupConnection(conn);
		return;
	}

<<<<<<< HEAD
	// 确认数据已写入 Asio 缓冲区
	conn->read_buffer.commit(bytes_transferred);
	conn->last_activity = std::chrono::steady_clock::now();

	// 更新监控：接收字节数
	auto& monitor = ServerMonitor::getInstance();
	monitor.addBytesReceived(bytes_transferred);

	// 获取原始缓冲区数据
	const char* data = asio::buffer_cast<const char*>(conn->read_buffer.data());
	size_t available = conn->read_buffer.size();

	// 记录解析前已消费的字节数
	size_t consumed_before = conn->parser.getBytesConsumed();

	// 将原始数据传给解析器
	auto request_opt = conn->parser.parse(data, available);
=======
	conn->read_buffer.commit(bytes_transferred);
	conn->last_activity = std::chrono::steady_clock::now();

	// ========== 改用 istream 读取 ==========
	std::istream is(&conn->read_buffer);
	std::string http_data;

	// 读取直到遇到 \r\n\r\n（HTTP头部结束标志）
	std::string line;
	while (std::getline(is, line)) {
		http_data += line + "\n";
		if (line.empty() || line == "\r") {
			break;  // 空行 = 头部结束
		}
	}

	LOG_DEBUG("收到的HTTP数据:\n{}", http_data);

	// 用 stringstream 模拟原始数据给 parser
	std::string raw = http_data;
	auto request_opt = conn->parser.parse(raw.c_str(), raw.size());
>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510

	// 计算本次解析消耗的字节数
	size_t consumed_after = conn->parser.getBytesConsumed();
	size_t consumed = consumed_after - consumed_before;

	// 从 Asio 缓冲区中精确移除已消费的数据
	if (consumed > 0) {
		conn->read_buffer.consume(consumed);
	}

	if (request_opt) {
<<<<<<< HEAD
		// 解析成功，保存请求对象
		conn->current_request = std::move(request_opt);
		conn->keep_alive = conn->current_request->keep_alive;

		const auto& req = conn->current_request.value();
		const char* method_str =
			req.method == HttpMethod::GET    ? "GET"    :
			req.method == HttpMethod::HEAD   ? "HEAD"   :
			req.method == HttpMethod::POST   ? "POST"   :
			req.method == HttpMethod::PUT    ? "PUT"    :
			req.method == HttpMethod::DELETE_ ? "DELETE" : "UNKNOWN";

		LOG_INFO("解析成功: {} {}", method_str, req.path);

		// 在线程池中异步处理请求
		thread_pool_->enqueue([this, conn]() {
			processRequest(conn);
		});
=======
		conn->current_request = std::move(request_opt);

		LOG_INFO("解析成功: {} {}",
			conn->current_request->method == HttpMethod::GET ? "GET" : "HEAD",
			conn->current_request->path);

		thread_pool_->enqueue([this, conn]() {
			processRequest(conn);
			});
>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510
	}
	else {
		LOG_DEBUG("解析失败，继续等待");
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

	//获取解析好的请求对象
	if (!conn->current_request.has_value()) {
		LOG_ERROR("请求对象为空，无法处理");
		sendErrorResponse(conn, 500);
		return;
	}

	// 获取请求引用（方便使用）
	const auto& request = conn->current_request.value();

	// 检查请求方法
<<<<<<< HEAD
	if (request.method == HttpMethod::UNSUPPORTED) {
		LOG_WARNING("不支持的方法: {}", request.path);
		sendErrorResponse(conn, 405, "\r\nAllow: GET, HEAD, POST");
		return;
	}

	std::string path = request.path;
	const char* method_str =
		request.method == HttpMethod::GET    ? "GET"    :
		request.method == HttpMethod::HEAD   ? "HEAD"   :
		request.method == HttpMethod::POST   ? "POST"   :
		request.method == HttpMethod::PUT    ? "PUT"    :
		request.method == HttpMethod::DELETE_ ? "DELETE" : "OTHER";

	LOG_DEBUG("处理请求: {} {}", method_str, path);

	// 移除查询字符串（用于路由匹配和文件路径）
	size_t query_pos = path.find('?');
	std::string route_path = (query_pos != std::string::npos) ? path.substr(0, query_pos) : path;

	// ========== 路由分发 ==========
	// POST/PUT/DELETE：必须走路由
	// GET/HEAD：优先路由匹配，未匹配则回退到静态文件

	bool is_dynamic = (request.method == HttpMethod::POST ||
	                   request.method == HttpMethod::PUT ||
	                   request.method == HttpMethod::DELETE_);

	if (is_dynamic || request.method == HttpMethod::GET || request.method == HttpMethod::HEAD) {
		bool route_found = false;
		HttpResponse route_response = router_.dispatch(request.method, route_path, request, &route_found);

		if (route_found) {
			// 路由匹配成功，使用处理器返回的完整 HttpResponse
			std::string status_text;
			switch (route_response.status_code) {
				case 200: status_text = "200 OK"; break;
				case 201: status_text = "201 Created"; break;
				case 400: status_text = "400 Bad Request"; break;
				case 404: status_text = "404 Not Found"; break;
				case 500: status_text = "500 Internal Server Error"; break;
				default:  status_text = std::to_string(route_response.status_code) + " OK"; break;
			}

			std::string response = buildHttpResponse(status_text, route_response.content_type,
				route_response.body, conn->keep_alive, false);

			sendResponse(conn, response);
			conn->requests_handled++;
			monitor.addBytesSent(response.size());

			{
				std::lock_guard<std::mutex> lock(connections_mutex_);
				for (const auto& [fd, c] : connections_) {
					if (c == conn) {
						monitor.updateConnectionRequest(fd, request.path);
						break;
					}
				}
			}

			conn->current_request.reset();
			return;
		}

		// 动态请求必须匹配路由
		if (is_dynamic) {
			LOG_DEBUG("无匹配路由: {} {}", method_str, route_path);
			sendErrorResponse(conn, 404);
			conn->requests_handled++;
			conn->current_request.reset();
			return;
		}
	}

	// ========== 回退：静态文件服务（GET/HEAD） ==========

	if (path.find("..") != std::string::npos ||
		path.find("//") != std::string::npos ||
		path.find("\\") != std::string::npos ||
		path.find('\0') != std::string::npos ||
		path.find("%2e%2e") != std::string::npos ||
		path.find("%2f") != std::string::npos) {
=======

	if (request.method == HttpMethod::UNSUPPORTED) {
		LOG_WARNING("不支持的方法");
		sendErrorResponse(conn, 405);  // Method Not Allowed
		return;
	}

	// 获取并安全检查请求路径
	std::string path = request.path;

	LOG_DEBUG("处理请求: {} {}",
		(request.method == HttpMethod::GET ? "GET" : "HEAD"),
		path);



	if (path.find("..") != std::string::npos ||      // 目录回溯
		path.find("//") != std::string::npos ||       // 双斜杠
		path.find("\\") != std::string::npos ||       // Windows反斜杠
		path.find('\0') != std::string::npos ||       // 空字节截断
		path.find("%2e%2e") != std::string::npos ||   // URL编码的 ..
		path.find("%2f") != std::string::npos) {      // URL编码的 /
>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510
		LOG_WARNING("检测到可疑路径访问: {}", path);
		sendErrorResponse(conn, 403);
		return;
	}

	if (path == "/") {
		path = "/index.html";
	}

<<<<<<< HEAD
	if (!path.empty() && path.back() == '/') {
		path += "index.html";
	}

=======

	size_t query_pos = path.find('?');
	if (query_pos != std::string::npos) {
		path = path.substr(0, query_pos);
		LOG_DEBUG("移除查询字符串，实际路径: {}", path);
	}


	if (!path.empty() && path.back() == '/') {
		path += "index.html";  // 目录访问默认返回 index.html
	}


>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510
	std::string file_path = static_dir_ + path;

	LOG_DEBUG("文件路径: {}", file_path);

<<<<<<< HEAD
=======


	//  检查是否存在 
>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510
	if (!fs::exists(file_path)) {
		LOG_DEBUG("文件不存在: {}", file_path);
		sendErrorResponse(conn, 404);
		return;
	}

<<<<<<< HEAD
	if (!fs::is_regular_file(file_path)) {
		LOG_DEBUG("路径不是文件，可能是目录: {}", file_path);
		sendErrorResponse(conn, 404);
		return;
	}

=======
	//  确保是普通文件 
	if (!fs::is_regular_file(file_path)) {
		LOG_DEBUG("路径不是文件，可能是目录: {}", file_path);
		sendErrorResponse(conn, 404);  
		return;
	}

	//检查文件是否可读
>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510
	std::error_code ec;
	auto perms = fs::status(file_path, ec).permissions();
	if (ec) {
		LOG_ERROR("无法获取文件权限: {}", ec.message());
		sendErrorResponse(conn, 500);
		return;
	}
<<<<<<< HEAD
=======

	// 检查所有者读权限（简化处理）
>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510
	if ((perms & fs::perms::owner_read) == fs::perms::none) {
		LOG_WARNING("文件不可读: {}", file_path);
		sendErrorResponse(conn, 403);
		return;
	}

<<<<<<< HEAD
	auto it = request.headers.find("If-Modified-Since");
	if (it != request.headers.end()) {
		LOG_DEBUG("客户端要求检查修改时间: {}", it->second);
		auto file_time = fs::last_write_time(file_path, ec);
		if (!ec) {
			auto ftime = std::chrono::system_clock::to_time_t(
				std::chrono::clock_cast<std::chrono::system_clock>(file_time));
		}
	}

	bool is_head = (request.method == HttpMethod::HEAD);

	std::string content;
	if (!is_head) {
		content = readFile(file_path);
		if (content.empty() && fs::file_size(file_path) > 0) {
=======

	//处理缓存相关头部（If-Modified-Since）


	auto it = request.headers.find("If-Modified-Since");
	if (it != request.headers.end()) {
		LOG_DEBUG("客户端要求检查修改时间: {}", it->second);

		// 获取文件最后修改时间
		auto file_time = fs::last_write_time(file_path, ec);
		if (!ec) {
			// 将文件时间转换为 HTTP 时间格式
			auto ftime = std::chrono::system_clock::to_time_t(
				std::chrono::clock_cast<std::chrono::system_clock>(file_time));

		}
	}


	// 处理 HEAD 请求


	bool is_head = (request.method == HttpMethod::HEAD);

	//读取文件内容

	std::string content;
	if (!is_head) {
		// GET 请求：读取完整文件内容
		content = readFile(file_path);
		if (content.empty() && fs::file_size(file_path) > 0) {
			// 文件不为空但读取失败
>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510
			LOG_ERROR("读取文件失败: {}", file_path);
			sendErrorResponse(conn, 500);
			return;
		}
	}
<<<<<<< HEAD

	std::string mime_type = getMimeType(file_path);

	auto accept_it = request.headers.find("Accept");
	if (accept_it != request.headers.end()) {
		LOG_DEBUG("客户端接受类型: {}", accept_it->second);
	}

	LOG_DEBUG("响应: {} {} -> {} ({} bytes, type: {})",
		method_str, request.path, file_path, content.size(), mime_type);

	std::string response = buildHttpResponse(
		"200 OK", mime_type, content, conn->keep_alive, true);

	sendResponse(conn, response);

	conn->requests_handled++;

=======
	// HEAD 请求：content 保持为空字符串

	//获取 MIME 类型
	
	std::string mime_type = getMimeType(file_path);

	//根据 Accept 头部调整响应
	
	auto accept_it = request.headers.find("Accept");
	if (accept_it != request.headers.end()) {
		LOG_DEBUG("客户端接受类型: {}", accept_it->second);

	}

	//记录日志

	LOG_DEBUG("响应: {} {} -> {} ({} bytes, type: {})",
		request.method == HttpMethod::GET ? "GET" : "HEAD",
		request.path,
		file_path,
		content.size(),
		mime_type);

	//构建并发送 HTTP 响应

	std::string status = "200 OK";

	std::string response = buildHttpResponse(
		status,
		mime_type,
		content,               // HEAD 请求时为空字符串
		conn->keep_alive        // 从请求中解析的 Keep-Alive 标志
	);




	sendResponse(conn, response);

	//更新统计信息

	conn->requests_handled++;

	// 找到正确的连接 ID（从 Monitor 中查找）
>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510
	{
		std::lock_guard<std::mutex> lock(connections_mutex_);
		for (const auto& [fd, c] : connections_) {
			if (c == conn) {
				monitor.updateConnectionRequest(fd, request.path);
				break;
			}
		}
	}

	monitor.addBytesSent(response.size());

<<<<<<< HEAD
=======
	//清理当前请求（准备处理下一个请求）

>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510
	conn->current_request.reset();
}

void WebServer::sendResponse(std::shared_ptr<Connection> conn, const std::string& response)
{
	// SSL 连接：使用 SSL 写入
	if (auto ssl_conn = std::dynamic_pointer_cast<SslConnection>(conn)) {
		sendSslResponse(ssl_conn, response);
		return;
	}

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

void WebServer::sendErrorResponse(std::shared_ptr<Connection> conn, int status_code,
                                   const std::string& extra_headers)
{
	// SSL 连接：使用 SSL 错误响应
	if (auto ssl_conn = std::dynamic_pointer_cast<SslConnection>(conn)) {
		sendSslErrorResponse(ssl_conn, status_code, extra_headers);
		return;
	}
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

	// 注入额外头部（如 405 的 Allow）
	if (!extra_headers.empty()) {
		size_t header_end = response.find("\r\n\r\n");
		if (header_end != std::string::npos) {
			response.insert(header_end, extra_headers);
		}
	}

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
	conn->getSocket().close(ec);

	// ========== 更新监控数据 ==========
	auto& monitor = ServerMonitor::getInstance();
	if (conn_id > 0) {
		monitor.removeConnection(conn_id);
	}
	monitor.decrementConnections();

	LOG_DEBUG("连接关闭: ID={}, 处理请求数={}, 活动连接={}",
		conn_id, conn->requests_handled, connections_.size());
}

std::string WebServer::buildHttpResponse(const std::string& status, const std::string& content_type, const std::string& body, bool keep_alive, bool cacheable)
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
	if (cacheable) {
		response << "Cache-Control: public, max-age=3600\r\n";
	} else {
		response << "Cache-Control: no-cache, no-store, must-revalidate\r\n";
	}

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

// ==================== SSL/HTTPS 实现 ====================

bool WebServer::initSsl() {
	try {
		ssl_context_ = std::make_unique<ssl::context>(ssl::context::sslv23);
		ssl_context_->set_options(
			ssl::context::default_workarounds |
			ssl::context::no_sslv2 |
			ssl::context::no_sslv3 |
			ssl::context::single_dh_use);

		ssl_context_->use_certificate_chain_file(cert_file_);
		ssl_context_->use_private_key_file(key_file_, ssl::context::pem);

		LOG_INFO("SSL 证书已加载: {}", cert_file_);
		return true;
	} catch (const std::exception& e) {
		LOG_ERROR("SSL 初始化失败: {}", e.what());
		return false;
	}
}

void WebServer::startSslAccept() {
	if (!running_) return;

	auto conn = std::make_shared<SslConnection>(
		tcp::socket(io_context_), *ssl_context_);

	ssl_acceptor_->async_accept(
		conn->getSocket(),
		[this, conn](const boost::system::error_code& error) {
			handleSslAccept(conn, error);
		});
}

void WebServer::handleSslAccept(std::shared_ptr<SslConnection> conn,
                                const boost::system::error_code& error) {
	if (!running_) return;

	if (!error) {
		boost::system::error_code ec;
		tcp::endpoint remote = conn->getSocket().remote_endpoint(ec);
		if (!ec) {
			conn->client_ip = remote.address().to_string();
			conn->client_port = remote.port();
		}

		conn->getSocket().set_option(tcp::no_delay(true));
		conn->getSocket().set_option(asio::socket_base::keep_alive(true));

		int conn_id = next_connection_id_++;
		{
			std::lock_guard<std::mutex> lock(connections_mutex_);
			if (connections_.size() >= MAX_CONNECTIONS) {
				LOG_WARNING("达到最大连接数，拒绝新SSL连接");
				boost::system::error_code ec;
				conn->getSocket().close(ec);
				startSslAccept();
				return;
			}
			connections_[conn_id] = conn;
		}

		auto& monitor = ServerMonitor::getInstance();
		monitor.incrementConnections();
		monitor.addConnection(conn_id, conn->client_ip, conn->client_port);

		LOG_DEBUG("新SSL连接: {}:{} (ID={})", conn->client_ip, conn->client_port, conn_id);

		// 开始 SSL 握手
		startSslHandshake(conn);
	} else {
		if (error != asio::error::operation_aborted) {
			LOG_ERROR("SSL接受连接失败: {}", error.message());
		}
	}

	if (running_) {
		startSslAccept();
	}
}

void WebServer::startSslHandshake(std::shared_ptr<SslConnection> conn) {
	auto self = conn;
	conn->ssl_socket.async_handshake(
		ssl::stream_base::server,
		[this, conn](const boost::system::error_code& error) {
			handleSslHandshake(conn, error);
		});
}

void WebServer::handleSslHandshake(std::shared_ptr<SslConnection> conn,
                                   const boost::system::error_code& error) {
	if (!running_) return;

	if (!error) {
		conn->ssl_handshaked = true;
		LOG_DEBUG("SSL握手完成: {}:{}", conn->client_ip, conn->client_port);
		startSslRead(conn);
	} else {
		if (error != asio::error::operation_aborted) {
			LOG_ERROR("SSL握手失败: {} — {}", conn->client_ip, error.message());
		}
		cleanupSslConnection(conn);
	}
}

void WebServer::startSslRead(std::shared_ptr<SslConnection> conn) {
	auto buffer = conn->read_buffer.prepare(BUFFER_SIZE);

	conn->ssl_socket.async_read_some(
		buffer,
		[this, conn](const boost::system::error_code& error,
		             size_t bytes_transferred) {
			handleSslRead(conn, error, bytes_transferred);
		});
}

void WebServer::handleSslRead(std::shared_ptr<SslConnection> conn,
                              const boost::system::error_code& error,
                              size_t bytes_transferred) {
	if (!running_) return;

	if (error) {
		if (error != asio::error::eof && error != asio::error::connection_reset) {
			if (error.category() != asio::error::get_ssl_category()) {
				LOG_ERROR("SSL读取错误: {}", error.message());
			} else {
				LOG_DEBUG("SSL连接关闭: {}", error.message());
			}
		}
		cleanupSslConnection(conn);
		return;
	}

	conn->read_buffer.commit(bytes_transferred);
	conn->last_activity = std::chrono::steady_clock::now();

	auto& monitor = ServerMonitor::getInstance();
	monitor.addBytesReceived(bytes_transferred);

	const char* data = asio::buffer_cast<const char*>(conn->read_buffer.data());
	size_t available = conn->read_buffer.size();

	size_t consumed_before = conn->parser.getBytesConsumed();
	auto request_opt = conn->parser.parse(data, available);
	size_t consumed_after = conn->parser.getBytesConsumed();
	size_t consumed = consumed_after - consumed_before;

	if (consumed > 0) {
		conn->read_buffer.consume(consumed);
	}

	if (request_opt) {
		conn->current_request = std::move(request_opt);
		conn->keep_alive = conn->current_request->keep_alive;

		const auto& req = conn->current_request.value();
		const char* method_str =
			req.method == HttpMethod::GET    ? "GET"    :
			req.method == HttpMethod::HEAD   ? "HEAD"   :
			req.method == HttpMethod::POST   ? "POST"   :
			req.method == HttpMethod::PUT    ? "PUT"    :
			req.method == HttpMethod::DELETE_ ? "DELETE" : "UNKNOWN";

		LOG_INFO("HTTPS解析成功: {} {}", method_str, req.path);

		thread_pool_->enqueue([this, conn]() {
			processRequest(conn);
		});
	} else {
		startSslRead(conn);
	}
}

void WebServer::handleSslWrite(std::shared_ptr<SslConnection> conn,
                               const boost::system::error_code& error,
                               size_t bytes_transferred) {
	if (error) {
		if (error != asio::error::operation_aborted) {
			LOG_ERROR("SSL写入错误: {}", error.message());
		}
		cleanupSslConnection(conn);
		return;
	}

	LOG_DEBUG("SSL响应发送完成: {} bytes", bytes_transferred);

	conn->write_buffer.clear();

	if (conn->keep_alive && conn->requests_handled < 100) {
		startSslRead(conn);
	} else {
		cleanupSslConnection(conn);
	}
}

void WebServer::sendSslResponse(std::shared_ptr<SslConnection> conn, const std::string& response) {
	conn->write_buffer = response;

	asio::async_write(
		conn->ssl_socket,
		asio::buffer(conn->write_buffer),
		[this, conn](const boost::system::error_code& error,
		             size_t bytes_transferred) {
			handleSslWrite(conn, error, bytes_transferred);
		});
}

void WebServer::sendSslErrorResponse(std::shared_ptr<SslConnection> conn, int status_code,
                                     const std::string& extra_headers) {
	std::string status_text;
	switch (status_code) {
		case 400: status_text = "400 Bad Request"; break;
		case 403: status_text = "403 Forbidden"; break;
		case 404: status_text = "404 Not Found"; break;
		case 405: status_text = "405 Method Not Allowed"; break;
		case 500: status_text = "500 Internal Server Error"; break;
		default:  status_text = "500 Internal Server Error"; break;
	}

	std::string body =
		"<!DOCTYPE html>\n"
		"<html><head><meta charset='UTF-8'><title>" + status_text + "</title></head>\n"
		"<body style='font-family:Arial;text-align:center;padding:50px;'>\n"
		"<h1>" + status_text + "</h1>\n"
		"<hr><p>Mini Web Server 1.0 (HTTPS)</p>\n"
		"</body></html>";

	std::string response = buildHttpResponse(status_text, "text/html", body, false);
	response += "Strict-Transport-Security: max-age=31536000\r\n";

	sendSslResponse(conn, response);
}

void WebServer::cleanupSslConnection(std::shared_ptr<SslConnection> conn) {
	int conn_id = 0;

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

	boost::system::error_code ec;
	if (conn->ssl_handshaked) {
		conn->ssl_socket.shutdown(ec);
	}
	conn->getSocket().close(ec);

	auto& monitor = ServerMonitor::getInstance();
	if (conn_id > 0) {
		monitor.removeConnection(conn_id);
	}
	monitor.decrementConnections();

	LOG_DEBUG("SSL连接关闭: ID={}, 处理请求数={}",
		conn_id, conn->requests_handled);
}

