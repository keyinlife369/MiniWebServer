#include"http_parser.h"
#include"sstream"
#include<algorithm>
#include<iostream>
#include"logger.h"

HttpParser::HttpParser() {
	reset();
}

void HttpParser::reset() {
	state_ = ParseState::METHOD;
	current_request_ = HttpRequest{};
	current_header_key_.clear();
	buffer_.clear();
	content_length_ = 0;
	body_read_ = 0;
	total_bytes_consumed_ = 0;
}

std::optional<HttpRequest> HttpParser::parse(const char* data, size_t len) {
<<<<<<< HEAD
	// 将新数据追加到内部缓冲区
	buffer_.append(data, len);

	while (true) {
		// ========== BODY 状态：支持分块到达 ==========
		if (state_ == ParseState::BODY) {
			size_t needed = content_length_ - body_read_;
			size_t available = buffer_.size();
			size_t to_read = (std::min)(needed, available);

			current_request_.body.append(buffer_.substr(0, to_read));
			buffer_.erase(0, to_read);
			total_bytes_consumed_ += to_read;
			body_read_ += to_read;

			if (body_read_ >= content_length_) {
				state_ = ParseState::COMPLETE;
				// 继续执行下面的 COMPLETE 处理
			} else {
				// body 数据未完全到达，等待更多数据
				return std::nullopt;
			}
		}

		// ========== COMPLETE：软重置并返回请求 ==========
		if (state_ == ParseState::COMPLETE) {
			auto request = std::move(current_request_);

			// 软重置：仅清空请求级状态，保留 buffer_（可能含流水线请求数据）
			current_request_ = HttpRequest{};
			current_header_key_.clear();
			content_length_ = 0;
			body_read_ = 0;
			// 注意：total_bytes_consumed_ 和 buffer_ 不清零
			// buffer_ 中可能已经有下一个请求的数据
			state_ = ParseState::METHOD;

			return request;
		}

		// ========== 非 BODY/COMPLETE 状态：按行解析 ==========
		// 查找行结束符
		auto pos = buffer_.find("\r\n");
		if (pos == std::string::npos) {
			return std::nullopt;
		}

		std::string line = buffer_.substr(0, pos);
		buffer_.erase(0, pos + 2);
		total_bytes_consumed_ += (pos + 2);  // 追踪消费的字节数

		// 空行 = 头部结束
		if (line.empty()) {
			if (state_ == ParseState::HEADER_KEY || state_ == ParseState::HEADER_VALUE) {
				// 检查是否有 Content-Length
				auto it = current_request_.headers.find("Content-Length");
				if (it != current_request_.headers.end()) {
					try {
						content_length_ = std::stoul(it->second);
					} catch (const std::exception&) {
						LOG_WARNING("无效的 Content-Length: {}", it->second);
						state_ = ParseState::ERROR_;
						reset();
						return std::nullopt;
					}

					// 检查请求体大小限制
					if (content_length_ > max_body_size_) {
						LOG_WARNING("请求体过大: {} bytes (最大允许: {})",
							content_length_, max_body_size_);
						state_ = ParseState::ERROR_;
						reset();
						return std::nullopt;
					}

					state_ = ParseState::BODY;
					continue;  // 继续循环读取 body
				}
				else {
					// 没有 body（GET/HEAD 请求）：直接完成
					state_ = ParseState::COMPLETE;
					continue;  // 回到循环顶部处理 COMPLETE
				}
			}
		}

		parseLine(line);

		if (state_ == ParseState::ERROR_) {
			reset();
			return std::nullopt;
		}
	}
=======
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
>>>>>>> 32048d3d8b9434b5d7a473674304be5258749510
}


void HttpParser::parseLine(const std::string& line) {
	switch (state_) {
		case ParseState::METHOD: {
			std::istringstream iss(line);
			std::string method_str;
			iss >> method_str;

			if (method_str == "GET") {
				current_request_.method = HttpMethod::GET;
			}
			else if (method_str == "HEAD") {
				current_request_.method = HttpMethod::HEAD;
			}
			else if (method_str == "POST") {
				current_request_.method = HttpMethod::POST;
			}
			else if (method_str == "PUT") {
				current_request_.method = HttpMethod::PUT;
			}
			else if (method_str == "DELETE") {
				current_request_.method = HttpMethod::DELETE_;
			}
			else {
				current_request_.method = HttpMethod::UNSUPPORTED;
			}

			iss >> current_request_.path;
			iss >> current_request_.version;
			state_ = ParseState::PATH;
			break;
		}
		case ParseState::PATH:
		case ParseState::VERSION:
		case ParseState::HEADER_VALUE:
		case ParseState::HEADER_KEY: {
			// 统一在 HEADER 相关状态处理：所有非空行尝试作为头部解析
			size_t colon_pos = line.find(":");
			if (colon_pos != std::string::npos) {
				current_header_key_ = line.substr(0, colon_pos);
				std::string value = line.substr(colon_pos + 1);

				// 去除前导空白（空格和制表符）
				value.erase(0, value.find_first_not_of(" \t"));
				// 去除尾部空白和回车
				while (!value.empty() &&
					(value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
					value.pop_back();
				}

				current_request_.headers[current_header_key_] = value;

				// 检测 Connection: keep-alive（不区分大小写）
				if (current_header_key_ == "Connection") {
					std::string lower_value = value;
					std::transform(lower_value.begin(), lower_value.end(),
						lower_value.begin(), ::tolower);
					if (lower_value.find("keep-alive") != std::string::npos) {
						current_request_.keep_alive = true;
					}
				}
			}
			// 保持当前状态，继续解析后续头部行
			state_ = ParseState::HEADER_KEY;
			break;
		}
		default:
			break;
	}
}
