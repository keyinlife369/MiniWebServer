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
    buffer_.append(data, len);

    while (true) {
        if (state_ == ParseState::BODY) {
            if (buffer_.size() >= content_length_) {
                current_request_.body = buffer_.substr(0, content_length_);
                buffer_.erase(0, content_length_);
                state_ = ParseState::COMPLETE;

                auto request = std::move(current_request_);
                reset();
                return request;
            }
            return std::nullopt;
        }

        // 查找行结束符
        auto pos = buffer_.find("\r\n");
        if (pos == std::string::npos) {
            return std::nullopt;
        }

        std::string line = buffer_.substr(0, pos);
        buffer_.erase(0, pos + 2);

        // ========== 关键修改 ==========
        if (line.empty()) {
            // 遇到空行 = 头部结束
            if (state_ == ParseState::HEADER_KEY || state_ == ParseState::HEADER_VALUE) {
                // 检查是否有 Content-Length
                auto it = current_request_.headers.find("Content-Length");
                if (it != current_request_.headers.end()) {
                    content_length_ = std::stoul(it->second);
                    state_ = ParseState::BODY;
                    continue;  // 继续循环读取 body
                }
                else {
                    // GET 请求：没有 body，直接完成
                    state_ = ParseState::COMPLETE;
                    auto request = std::move(current_request_);
                    reset();
                    return request;  // 直接返回！
                }
            }
        }
        // ============================

        parseLine(line);

        if (state_ == ParseState::ERROR_) {
            reset();
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
