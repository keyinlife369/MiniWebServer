#pragma once
//URL路由系统
#include "http_parser.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <shared_mutex>
#include <string_view>

// HTTP 响应结构体（由路由处理器返回）
struct HttpResponse {
	int status_code = 200;
	std::string content_type = "application/json; charset=utf-8";
	std::string body;

	// 便捷工厂
	static HttpResponse ok(const std::string& body, const std::string& ct = "application/json; charset=utf-8") {
		return { 200, ct, body };
	}
	static HttpResponse created(const std::string& body) {
		return { 201, "application/json; charset=utf-8", body };
	}
	static HttpResponse badRequest(const std::string& msg) {
		return { 400, "application/json; charset=utf-8", "{\"error\":\"" + msg + "\"}" };
	}
	static HttpResponse notFound(const std::string& msg = "Not Found") {
		return { 404, "application/json; charset=utf-8", "{\"error\":\"" + msg + "\"}" };
	}
	static HttpResponse serverError(const std::string& msg = "Internal Server Error") {
		return { 500, "application/json; charset=utf-8", "{\"error\":\"" + msg + "\"}" };
	}
};

// 路由处理函数签名
// 参数：已解析的HTTP请求对象（含路由参数）
// 返回：HttpResponse（含状态码/Content-Type/响应体）
using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

// 参数化路由条目
struct ParamRoute {
	HttpMethod method;
	std::vector<std::string> segments;   // 路径分段，以":"开头的为参数占位符
	std::vector<size_t> param_indices;   // 哪些分段是参数
	std::vector<std::string> param_names;// 参数名称（不含冒号）
	RouteHandler handler;
};

class Router {
public:
	Router() = default;

	// 注册路由（必须在服务器启动前调用，线程安全）
	// 支持精确匹配："/api/echo"
	// 支持参数化路径："/api/user/:id"
	void addRoute(HttpMethod method, const std::string& path_pattern, RouteHandler handler);

	// 路由查找：返回匹配的处理器，同时填充路径参数
	// 返回 nullptr 表示未找到匹配
	RouteHandler route(HttpMethod method, const std::string& path,
	                   std::unordered_map<std::string, std::string>& out_params) const;

	// 便捷方法：路由并执行
	// found 输出参数指示是否匹配到路由
	HttpResponse dispatch(HttpMethod method, const std::string& path,
	                      const HttpRequest& request, bool* found = nullptr) const;

private:
	// 精确匹配路由表，key = "METHOD:path"（如 "2:/api/info"）
	std::unordered_map<std::string, RouteHandler> exact_routes_;

	// 参数化路由表（按注册顺序排列，先注册的优先匹配）
	std::vector<ParamRoute> param_routes_;

	// 读写锁：初始化阶段写，运行阶段读
	mutable std::shared_mutex routes_mutex_;

	// 解析路径模式为分段列表
	static std::vector<std::string> splitPath(const std::string& path);

	// 尝试匹配参数化路由
	bool matchParamRoute(const ParamRoute& route, const std::string& path,
	                     std::unordered_map<std::string, std::string>& params) const;
};