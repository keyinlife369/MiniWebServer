#include "router.h"
#include "logger.h"
#include <sstream>

// ========== 路径分割：按 '/' 拆分 ==========
std::vector<std::string> Router::splitPath(const std::string& path) {
	std::vector<std::string> segments;
	std::stringstream ss(path);
	std::string segment;
	while (std::getline(ss, segment, '/')) {
		if (!segment.empty()) {
			segments.push_back(segment);
		}
	}
	return segments;
}

// ========== 注册路由 ==========
void Router::addRoute(HttpMethod method, const std::string& path_pattern, RouteHandler handler) {
	std::unique_lock lock(routes_mutex_);

	auto segments = splitPath(path_pattern);
	bool has_params = false;
	std::vector<size_t> param_indices;
	std::vector<std::string> param_names;

	for (size_t i = 0; i < segments.size(); ++i) {
		if (!segments[i].empty() && segments[i][0] == ':') {
			has_params = true;
			param_indices.push_back(i);
			param_names.push_back(segments[i].substr(1));  // 去掉冒号
		}
	}

	if (!has_params) {
		// 精确匹配：存入哈希表
		std::string key = std::to_string(static_cast<int>(method)) + ":" + path_pattern;
		exact_routes_[key] = std::move(handler);
		LOG_DEBUG("注册精确路由: {} {}",
			method == HttpMethod::GET ? "GET" :
			method == HttpMethod::POST ? "POST" :
			method == HttpMethod::PUT ? "PUT" :
			method == HttpMethod::DELETE_ ? "DELETE" : "HEAD",
			path_pattern);
	} else {
		// 参数化路由：存入有序列表
		ParamRoute pr;
		pr.method = method;
		pr.segments = std::move(segments);
		pr.param_indices = std::move(param_indices);
		pr.param_names = std::move(param_names);
		pr.handler = std::move(handler);
		param_routes_.push_back(std::move(pr));
	}
}

// ========== 参数化路由匹配 ==========
bool Router::matchParamRoute(const ParamRoute& route, const std::string& path,
                             std::unordered_map<std::string, std::string>& params) const {
	auto path_segments = splitPath(path);

	// 段数必须一致
	if (path_segments.size() != route.segments.size()) {
		return false;
	}

	// 逐段比较
	for (size_t i = 0; i < route.segments.size(); ++i) {
		// 检查该段是否为参数占位符
		bool is_param = false;
		for (size_t pi : route.param_indices) {
			if (pi == i) {
				is_param = true;
				break;
			}
		}

		if (is_param) {
			// 参数段：捕获路径值
			// 找到对应的参数名
			for (size_t p = 0; p < route.param_indices.size(); ++p) {
				if (route.param_indices[p] == i) {
					params[route.param_names[p]] = path_segments[i];
					break;
				}
			}
		} else {
			// 普通段：必须精确匹配
			if (route.segments[i] != path_segments[i]) {
				return false;
			}
		}
	}

	return true;
}

// ========== 路由查找 ==========
RouteHandler Router::route(HttpMethod method, const std::string& path,
                           std::unordered_map<std::string, std::string>& out_params) const {
	std::shared_lock lock(routes_mutex_);

	// 1. 先查精确匹配（O(1)）
	std::string key = std::to_string(static_cast<int>(method)) + ":" + path;
	auto it = exact_routes_.find(key);
	if (it != exact_routes_.end()) {
		return it->second;
	}

	// 2. 遍历参数化路由（O(n)）
	for (const auto& pr : param_routes_) {
		if (pr.method != method) continue;
		if (matchParamRoute(pr, path, out_params)) {
			return pr.handler;
		}
	}

	return nullptr;  // 未找到
}

// ========== 便捷方法：路由并执行 ==========
HttpResponse Router::dispatch(HttpMethod method, const std::string& path,
                             const HttpRequest& request, bool* found) const {
	std::unordered_map<std::string, std::string> params;
	auto handler = route(method, path, params);

	if (handler) {
		if (found) *found = true;
		// 将路由参数注入到请求对象中（mutable copy）
		HttpRequest req_with_params = request;
		req_with_params.route_params = std::move(params);
		return handler(req_with_params);
	}

	if (found) *found = false;
	return HttpResponse::notFound("Route not found");
}