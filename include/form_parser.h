#pragma once
//表单数据解析工具
#include <string>
#include <unordered_map>
#include <string_view>

// URL解码：%XX → 字符，+ → 空格
std::string urlDecode(std::string_view str);

// 解析 application/x-www-form-urlencoded 格式的请求体
// "key1=value1&key2=value2" → {"key1":"value1", "key2":"value2"}
std::unordered_map<std::string, std::string> parseUrlEncoded(std::string_view body);

// 解析 application/json（当前仅简单返回原始字符串）
inline std::string parseJsonBody(std::string_view body) {
	return std::string(body);
}