#include "form_parser.h"
#include <sstream>
#include <iomanip>

// ========== URL 解码 ==========
std::string urlDecode(std::string_view str) {
	std::string result;
	result.reserve(str.size());

	for (size_t i = 0; i < str.size(); ++i) {
		char c = str[i];
		if (c == '%') {
			// 需要两个十六进制字符
			if (i + 2 < str.size()) {
				char hex[3] = { str[i + 1], str[i + 2], '\0' };
				char* end = nullptr;
				long value = std::strtol(hex, &end, 16);
				if (end == hex + 2) {
					result += static_cast<char>(value);
					i += 2;
					continue;
				}
			}
			// 解码失败，保留原字符
			result += c;
		} else if (c == '+') {
			result += ' ';
		} else {
			result += c;
		}
	}

	return result;
}

// ========== 解析 URL 编码表单数据 ==========
std::unordered_map<std::string, std::string> parseUrlEncoded(std::string_view body) {
	std::unordered_map<std::string, std::string> result;

	if (body.empty()) {
		return result;
	}

	std::string body_str(body);
	std::stringstream ss(body_str);
	std::string pair;

	while (std::getline(ss, pair, '&')) {
		if (pair.empty()) continue;

		size_t eq_pos = pair.find('=');
		if (eq_pos != std::string::npos) {
			std::string key = urlDecode(pair.substr(0, eq_pos));
			std::string value = urlDecode(pair.substr(eq_pos + 1));
			result[std::move(key)] = std::move(value);
		} else {
			// 仅有 key，没有 value
			std::string key = urlDecode(pair);
			result[std::move(key)] = "";
		}
	}

	return result;
}