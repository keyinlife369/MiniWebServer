#include"http_parser.h"
#include<sstream>
#include<algorithm>

HttpParser::HttpParser() {
	reset();
}

void HttpParser::reset() {
	state_ = ParseState::METHOD;//解析请求方法
	current_request_ = HttpRequest{};//初始化Http请求对象
	current_header_key_.clear();//清空临时key
	buffer_.clear();//清空数据缓冲区
	content_length_ = 0;//请求体长度初始化为0
	body_read_ = 0;//已读请求体数据长度初始化为0
}
std::optional<HttpRequest> HttpParser::parse(const char* data, size_t len) {
	buffer_.append(data, len);//把接收到的数据存入缓冲区末尾

	while (true) {
		if (state_ == ParseState::BODY) {
			//缓冲区数据大于需要读取的请求体总长
			if (buffer_.size() >= content_length_) {
				//截取对应长度的数据做请求体
				current_request_.body = buffer_.substr(0, content_length_);
				buffer_.erase(0, content_length_);//从缓冲区删除已经解析的数据
				state_ = ParseState::COMPLETE;
				
				auto request = std::move(current_request_);//转移所有权
				reset();//解析完成，重置，解析下一个请求
				return request;
			}
			return std::nullopt;
		}
		//在缓冲区查找分隔符回车和换行
		auto pos = buffer_.find("\r\n");
		if (pos == std::string::npos) {
			return std::nullopt;//找不到返回空（数据不完整）
		}

		//提取一行数据并从缓冲区删除
		std::string line = buffer_.substr(0, pos);//截取一行内容
		buffer_.erase(0, pos + 2);//删除这一行+换行符

		//读到空行且但前状态是解析请求头
		if (line.empty() && state_ == ParseState::HEADER_VALUE) {
			//查找请求头中的Content-Length
			auto it = current_request_.headers.find("Content-Length");

			if (it != current_request_.headers.end()) {
				content_length_ = std::stoul(it->second);//读取请求体的长度切换状态为解析BODY
				state_ = ParseState::BODY;
			}
			else {
				//没有请求体直接标记解析完成
				state_ = ParseState::COMPLETE;
				auto request=std::move(current_request_);
				reset();
				return request;
			}
			continue;//继续循环，处理请求体
		}
		parseLine(line);//解析单行数据

		if (state_ == ParseState::ERROR_) {
			reset();//出错重置解析
			return std::nullopt;
		}
	}
}

void HttpParser::parseLine(const std::string& line) {
	switch (state_) {//根据解析状态，执行不同的逻辑
		case ParseState::METHOD:{
			std::istringstream iss(line);
			std::string method_str;
			iss >> method_str;
			//匹配支持的请求方法
			if (method_str == "GET") {
				current_request_.method = HttpMethod::GET;
			}
			else if (method_str == "HEAD") {
				current_request_.method = HttpMethod::HEAD;
			}
			else {
				current_request_.method = HttpMethod::UNSUPPORTED;
			}

			iss >> current_request_.path;//读取第二个请求路径
			iss >> current_request_.version;//读取第三个：Http版本
			state_ = ParseState::PATH;//切换状态
			break;
		}
		case ParseState::PATH:
		case ParseState::VERSION:
		case ParseState::HEADER_VALUE: {
			size_t colon_pos = line.find(":");
			if (colon_pos != std::string::npos) {
				current_header_key_ = line.substr(0, colon_pos);
				std::string value = line.substr(colon_pos + 1);

				value.erase(0, value.find_first_not_of("\t"));

				current_request_.headers[current_header_key_] = value;

				if (current_header_key_ == "Connection" && value.find("keep-alive") != std::string::npos) {
					current_request_.keep_alive = true;
				}
			}
			state_ = ParseState::HEADER_KEY;
			break;
		}
		case ParseState::HEADER_KEY:
			state_ = ParseState::HEADER_VALUE;
			break;
		default:
			break;
	}
}