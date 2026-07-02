#pragma once
#include<string>
#include<unordered_map>
#include<string_view>
#include<optional>

enum class HttpMethod {
	GET,
	HEAD,
	POST,
	PUT,
	DELETE_,
	UNSUPPORTED
};

struct HttpRequest {
	HttpMethod method = HttpMethod::UNSUPPORTED;
	std::string path;
	std::string version;
	std::unordered_map<std::string, std::string> headers;
	std::string body;
	std::unordered_map<std::string, std::string> route_params;  // 路由参数（如 :id）

	bool keep_alive = false;
};

class HttpParser {
public:
	enum class ParseState {
		METHOD,//方法
		PATH,//路径
		VERSION,//版本解析
		HEADER_KEY,//头的键
		HEADER_VALUE,//头的值
		BODY,//请求体
		COMPLETE,//解析完成
		ERROR_//出错
	};

	HttpParser();

	std::optional<HttpRequest> parse(const char* data, size_t len);
	void reset();//重置

	// 获取已消费字节数（用于 Asio buffer consume）
	size_t getBytesConsumed() const { return total_bytes_consumed_; }

	// 最大请求体大小限制（默认 10MB）
	static constexpr size_t DEFAULT_MAX_BODY_SIZE = 10 * 1024 * 1024;
	void setMaxBodySize(size_t max) { max_body_size_ = max; }

private:
	ParseState  state_ = ParseState::METHOD;
	HttpRequest current_request_;
	std::string current_header_key_;
	std::string buffer_;
	size_t content_length_ = 0;
	size_t body_read_ = 0;
	size_t total_bytes_consumed_ = 0;
	size_t max_body_size_ = DEFAULT_MAX_BODY_SIZE;

	void parseLine(const std::string& line);
};