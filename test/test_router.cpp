// 路由系统单元测试
#include <gtest/gtest.h>
#include "router.h"

class RouterTest : public ::testing::Test {
protected:
    Router router_;  // 每个测试用例前自动默认构造

    // 辅助函数：创建一个简单处理器
    static RouteHandler makeHandler(const std::string& name) {
        return [name](const HttpRequest&) -> HttpResponse {
            return HttpResponse::ok(name);
        };
    }
};

// ========== 精确匹配路由 ==========
TEST_F(RouterTest, ExactMatchGet) {
    router_.addRoute(HttpMethod::GET, "/api/info", makeHandler("info"));
    router_.addRoute(HttpMethod::GET, "/api/status", makeHandler("status"));

    std::unordered_map<std::string, std::string> params;
    auto handler = router_.route(HttpMethod::GET, "/api/info", params);
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler(HttpRequest{}).body, "info");
    EXPECT_TRUE(params.empty());
}

TEST_F(RouterTest, MethodFiltering) {
    router_.addRoute(HttpMethod::GET, "/api/data", makeHandler("get_data"));
    router_.addRoute(HttpMethod::POST, "/api/data", makeHandler("post_data"));

    std::unordered_map<std::string, std::string> params;

    auto get_handler = router_.route(HttpMethod::GET, "/api/data", params);
    ASSERT_NE(get_handler, nullptr);
    EXPECT_EQ(get_handler(HttpRequest{}).body, "get_data");

    auto post_handler = router_.route(HttpMethod::POST, "/api/data", params);
    ASSERT_NE(post_handler, nullptr);
    EXPECT_EQ(post_handler(HttpRequest{}).body, "post_data");
}

TEST_F(RouterTest, NotFound) {
    router_.addRoute(HttpMethod::GET, "/api/info", makeHandler("info"));

    std::unordered_map<std::string, std::string> params;
    auto handler = router_.route(HttpMethod::GET, "/api/nonexistent", params);
    EXPECT_EQ(handler, nullptr);
}

TEST_F(RouterTest, NotFoundDifferentMethod) {
    router_.addRoute(HttpMethod::POST, "/api/echo", makeHandler("echo"));

    std::unordered_map<std::string, std::string> params;
    auto handler = router_.route(HttpMethod::GET, "/api/echo", params);
    EXPECT_EQ(handler, nullptr);
}

// ========== 参数化路由 ==========
TEST_F(RouterTest, SingleParam) {
    router_.addRoute(HttpMethod::GET, "/api/user/:id", makeHandler("user"));

    std::unordered_map<std::string, std::string> params;
    auto handler = router_.route(HttpMethod::GET, "/api/user/42", params);
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(params["id"], "42");
}

TEST_F(RouterTest, MultipleParams) {
    router_.addRoute(HttpMethod::GET, "/api/user/:uid/post/:pid", makeHandler("post"));

    std::unordered_map<std::string, std::string> params;
    auto handler = router_.route(HttpMethod::GET, "/api/user/10/post/99", params);
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(params["uid"], "10");
    EXPECT_EQ(params["pid"], "99");
}

TEST_F(RouterTest, ParamRouteWrongSegmentCount) {
    router_.addRoute(HttpMethod::GET, "/api/user/:id/posts", makeHandler("posts"));

    std::unordered_map<std::string, std::string> params;
    // 段数不对
    auto handler = router_.route(HttpMethod::GET, "/api/user/42", params);
    EXPECT_EQ(handler, nullptr);
}

TEST_F(RouterTest, ParamRouteWithNonParamMismatch) {
    router_.addRoute(HttpMethod::GET, "/api/user/:id/profile", makeHandler("profile"));

    std::unordered_map<std::string, std::string> params;
    // "settings" != "profile"
    auto handler = router_.route(HttpMethod::GET, "/api/user/42/settings", params);
    EXPECT_EQ(handler, nullptr);
}

// ========== 精确路由优先于参数化路由 ==========
TEST_F(RouterTest, ExactBeforeParam) {
    // 先注册参数化路由
    router_.addRoute(HttpMethod::GET, "/api/user/:id", makeHandler("param_user"));
    // 后注册精确路由（应覆盖）
    router_.addRoute(HttpMethod::GET, "/api/user/admin", makeHandler("admin_user"));

    std::unordered_map<std::string, std::string> params;
    auto handler = router_.route(HttpMethod::GET, "/api/user/admin", params);
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler(HttpRequest{}).body, "admin_user");
}

// ========== dispatch 便捷方法 ==========
TEST_F(RouterTest, DispatchFound) {
    router_.addRoute(HttpMethod::GET, "/api/hello", [](const HttpRequest&) {
        return HttpResponse::ok("world");
    });

    HttpRequest req;
    req.method = HttpMethod::GET;
    req.path = "/api/hello";

    bool found = false;
    HttpResponse result = router_.dispatch(HttpMethod::GET, "/api/hello", req, &found);
    EXPECT_TRUE(found);
    EXPECT_EQ(result.body, "world");
}

TEST_F(RouterTest, DispatchNotFound) {
    HttpRequest req;

    bool found = true;
    HttpResponse result = router_.dispatch(HttpMethod::GET, "/nowhere", req, &found);
    EXPECT_FALSE(found);
    EXPECT_EQ(result.status_code, 404);
}

// ========== 所有 HTTP 方法 ==========
TEST_F(RouterTest, AllHttpMethods) {
    router_.addRoute(HttpMethod::GET, "/data", makeHandler("get"));
    router_.addRoute(HttpMethod::POST, "/data", makeHandler("post"));
    router_.addRoute(HttpMethod::PUT, "/data", makeHandler("put"));
    router_.addRoute(HttpMethod::DELETE_, "/data", makeHandler("delete"));

    std::unordered_map<std::string, std::string> params;

    auto g = router_.route(HttpMethod::GET, "/data", params);
    EXPECT_EQ(g(HttpRequest{}).body, "get");

    auto p = router_.route(HttpMethod::POST, "/data", params);
    EXPECT_EQ(p(HttpRequest{}).body, "post");

    auto pu = router_.route(HttpMethod::PUT, "/data", params);
    EXPECT_EQ(pu(HttpRequest{}).body, "put");

    auto d = router_.route(HttpMethod::DELETE_, "/data", params);
    EXPECT_EQ(d(HttpRequest{}).body, "delete");
}