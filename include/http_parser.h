#pragma once
#include<string>
#include<unordered_map>
#include<string_view>
#include<optional>

enum class HttpMethod {
	GET,
	HEAD,
	UNSUPPORTED
};

struct HttpRequest {
	HttpMethod method = HttpMethod::UNSUPPORTED;
	std::string path;
	std::string version;
	std::unordered_map<std::string, std::string> headers;
	std::string body;

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
	
private:
	ParseState  state_ = ParseState::METHOD;
	HttpRequest current_request_;
	std::string current_header_key_;
	std::string buffer_;
	size_t content_length_ = 0;
	size_t body_read_ = 0;

	void parseLine(const std::string& line);
};