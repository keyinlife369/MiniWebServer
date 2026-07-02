# MiniWebServer 原始版 vs 增强版 对比清单

> **原始版本**：commit `cbea52d` — MiniWebserver1.0 (2026-05-11)  
> **增强版本**：当前工作树 (截至 2026-06-18)  
> **迭代周期**：4 个优先级 × 约 9 天

---

## 1. 项目概览

| 维度 | 原始版 | 增强版 | 变化 |
|---|---|---|---|
| **源文件数** | 9 | 26 | **+17 个新文件** |
| **总代码行数** | ~2,550 | ~4,400 | **+72%** |
| **支持 HTTP 方法** | GET, HEAD | GET, HEAD, POST, PUT, DELETE | **+3 种方法** |
| **HTTPS** | ❌ | ✅ SSL/TLS 双端口 | **新增** |
| **路由系统** | ❌ 无（硬编码静态文件） | ✅ Router + 参数化路径 | **新增** |
| **数据库** | ❌ | ✅ MySQL 9.5 + 连接池 | **新增** |
| **RESTful API** | ❌ | ✅ 4 个 CRUD 端点 | **新增** |
| **单元测试** | ❌ 0 个 | ✅ 48 个测试 | **新增** |
| **压测工具** | ❌ | ✅ 并发基准测试客户端 | **新增** |
| **Docker** | ❌ | ✅ Dockerfile + compose | **新增** |
| **TLS 证书** | ❌ | ✅ 自签名 RSA 4096 | **新增** |

---

## 2. HTTP 协议补全 (Priority 1)

### 2.1 HTTP 方法扩展

**[include/http_parser.h](include/http_parser.h)**

```diff
- enum class HttpMethod { GET, HEAD, UNSUPPORTED };
+ enum class HttpMethod { GET, HEAD, POST, PUT, DELETE_, UNSUPPORTED };
```

**[src/http_parser.cpp](src/http_parser.cpp)** — METHOD 状态解析新增：
```cpp
if (method_str == "POST") current_request_.method = HttpMethod::POST;
else if (method_str == "PUT") current_request_.method = HttpMethod::PUT;
else if (method_str == "DELETE") current_request_.method = HttpMethod::DELETE_;
```

### 2.2 BODY 分块接收（核心增强）

原始版在 BODY 状态一次性从 `buffer_` 复制整个 body，要求所有数据必须一次性到达。增强版改为**增量追加**：

```cpp
// 旧逻辑 (有问题)
if (buffer_.size() >= content_length_) {
    current_request_.body = buffer_.substr(0, content_length_);
    ... reset() ...
}

// 新逻辑 (增量接收)
size_t needed = content_length_ - body_read_;
size_t to_read = std::min(needed, buffer_.size());
current_request_.body.append(buffer_.substr(0, to_read));
body_read_ += to_read;
total_bytes_consumed_ += to_read;
buffer_.erase(0, to_read);
// 未收完 → 返回 nullopt，等待下一块数据
```

### 2.3 HTTP 流水线支持

完成一个请求后不再完全 `reset()`，改为**软重置**——保留 `buffer_` 中剩余数据（下一个请求的内容），仅清空当前请求相关字段：

```cpp
case ParseState::COMPLETE:
    auto request = std::move(current_request_);
    current_request_.reset();
    current_header_key_.clear();
    content_length_ = 0;
    body_read_ = 0;
    // buffer_ 和 total_bytes_consumed_ 不清空！
    state_ = ParseState::METHOD;
    return request;
```

配套 `handleRead` 改为精确消费：
```cpp
// 旧逻辑：消费缓冲区中所有可用数据
conn->read_buffer.consume(available);

// 新逻辑：仅消费解析器实际消耗的字节数
auto consumed_before = parser.getBytesConsumed();
parser.parse(...);
auto consumed = parser.getBytesConsumed() - consumed_before;
conn->read_buffer.consume(consumed);
```

### 2.4 Body 大小限制

```cpp
// [新增] include/http_parser.h
static constexpr size_t DEFAULT_MAX_BODY_SIZE = 10 * 1024 * 1024;  // 10 MB
void setMaxBodySize(size_t limit);

// parseHeader 中验证
if (content_length_ > max_body_size_) {
    LOG_WARNING("Content-Length 超出限制: {} > {}", content_length_, max_body_size_);
    state_ = ParseState::ERROR_;
    return std::nullopt;
}
```

### 2.5 Header 解析 Bug 修复

**关键修复**：原始版 `HEADER_KEY` 状态只切换到 `HEADER_VALUE`，不做任何处理 → 每隔一行 header 就丢一行。修复为合并 `HEADER_KEY` 和 `HEADER_VALUE` 到同一个处理逻辑：

```cpp
// 旧代码 — 每两行 header 丢一行
case ParseState::HEADER_KEY:
    state_ = ParseState::HEADER_VALUE;  // 不做任何处理！
    continue;

// 新代码 — 统一处理
case ParseState::PATH:
case ParseState::VERSION:
case ParseState::HEADER_VALUE:
case ParseState::HEADER_KEY:  // ← 新增合并，所有 header 行都不丢
    // 统一处理：解析 "Key: Value"，trim value
```

### 2.6 附加改进

- **Content-Length 验证**：`std::stoul` 包裹 `try/catch`，非法值进入 ERROR 状态
- **Header value 修剪**：去除前导空格/tab、尾部 `\r`、尾部空格/tab
- **Keep-Alive 大小写**：`Connection: KEEP-ALIVE` 也正确识别（先转小写再匹配）
- **`getBytesConsumed()`**：追踪解析器总消费字节数，支持精确的缓冲区管理

---

## 3. URL 路由系统 (Priority 1)

### 3.1 新增文件

| 文件 | 行数 | 说明 |
|---|---|---|
| [include/router.h](include/router.h) | 83 | `HttpResponse` 结构体 + `Router` 类声明 |
| [src/router.cpp](src/router.cpp) | 136 | 路由匹配引擎实现 |

### 3.2 HttpResponse 结构体

```cpp
struct HttpResponse {
    int status_code = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;

    static HttpResponse ok(const std::string& body);         // 200
    static HttpResponse created(const std::string& body);    // 201
    static HttpResponse badRequest(const std::string& msg);  // 400
    static HttpResponse notFound(const std::string& msg);    // 404
    static HttpResponse serverError(const std::string& msg); // 500
};
```

### 3.3 Router 类设计

```
addRoute(method, pattern, handler)
        │
        ├─ pattern 无 :param → exact_routes_[key] = handler   (O(1) 哈希查找)
        │      key = "<int(method)>:<path>"
        │
        └─ pattern 含 :param → param_routes_.push_back(...)   (O(n) 线性匹配)
               匹配时逐段比较，捕获 param 值
```

**匹配优先级**：精确匹配 > 参数化匹配（允许 `/api/user/admin` 覆盖 `/api/user/:id`）

**线程安全**：`std::shared_mutex` — 注册时独占锁，查询时共享锁（无读竞争）

### 3.4 路由参数注入

```cpp
HttpResponse dispatch(method, path, request, &found) {
    std::unordered_map<std::string, std::string> params;
    auto handler = route(method, path, params);
    if (handler) {
        HttpRequest req_with_params = request;
        req_with_params.route_params = std::move(params);  // 注入！
        return handler(req_with_params);
    }
    return HttpResponse::notFound("Route not found");
}
```

Handler 内直接通过 `req.route_params["id"]` 获取路径参数。

### 3.5 服务端集成

`processRequest()` 新增路由分发逻辑：
- **POST/PUT/DELETE** → 必须匹配路由，否则直接返回 404
- **GET/HEAD** → 优先路由匹配，未匹配则回退到静态文件

---

## 4. MySQL 9.5 集成 (Priority 3)

### 4.1 新增文件

| 文件 | 行数 | 说明 |
|---|---|---|
| [include/mysql_wrapper.h](include/mysql_wrapper.h) | 113 | MysqlConnection + MysqlPool 声明 |
| [src/mysql_wrapper.cpp](src/mysql_wrapper.cpp) | 196 | 完整实现 |
| [sql/init.sql](sql/init.sql) | 12 | 数据库建表脚本 |

### 4.2 MysqlConnection (RAII 封装)

不使用 MySQL Connector/C++，直接用 **MySQL C API**（`mysql.h` + `libmysql.lib`）：

| 方法 | 功能 |
|---|---|
| `connect(host, port, user, pass, db)` | `mysql_init` → `mysql_options(utf8mb4)` → `mysql_real_connect` |
| `execute(sql)` | `mysql_real_query` → 消费并释放结果集 |
| `query(sql)` | 执行查询 → `mysql_store_result` → 遍历 `MYSQL_ROW`，返回 `vector<unordered_map<string,string>>` |
| `escape(str)` | `mysql_real_escape_string` → 防 SQL 注入 |
| `ping()` | `mysql_ping` |
| `lastInsertId()` | `mysql_insert_id` |

RAII：构造 → `mysql_init`，析构 → `mysql_close`。禁止拷贝。

### 4.3 MysqlPool (连接池)

```cpp
MysqlPool::Config config;
config.host = "127.0.0.1";
config.port = 3306;
config.user = "root";
config.password = "710978";
config.database = "miniwebserver";
config.pool_size = 8;  // 默认 8，与线程池对齐

auto pool = std::make_shared<MysqlPool>(config);
```

**核心机制**：
- **预创建**：构造函数中创建 `pool_size` 个连接，失败则 warn 并继续
- **阻塞获取**：`acquire()` 使用 `condition_variable::wait()` 阻塞到有可用连接
- **RAII 归还**：`ConnectionGuard` 析构时自动归还（`returnConnection`）
- **自动重连**：归还时 `ping()` 检查，失效则尝试重连 → 失败则新建连接 → 二次失败则丢弃

### 4.4 RESTful API 路由

注册在 [src/mainWindow.cpp](src/mainwindow.cpp) 的 `startServer()` 中：

| 方法 | 路径 | 功能 | SQL |
|---|---|---|---|
| `POST` | `/api/messages` | 创建留言 | `INSERT INTO messages (author, content) VALUES (...)` |
| `GET` | `/api/messages` | 所有留言 | `SELECT ... FROM messages ORDER BY id DESC LIMIT 100` |
| `GET` | `/api/message/:id` | 单条留言 | `SELECT ... FROM messages WHERE id=?` |
| `DELETE` | `/api/message/:id` | 删除留言 | `DELETE FROM messages WHERE id=?` |

所有 handler 使用 `HttpResponse` 返回类型 + `conn->escape()` 防注入。

### 4.5 启动时自动建库建表

```cpp
auto conn = db_pool->acquire();
conn->execute("CREATE DATABASE IF NOT EXISTS miniwebserver "
              "CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");
conn->execute("USE miniwebserver");
conn->execute("CREATE TABLE IF NOT EXISTS messages ("
              "id INT AUTO_INCREMENT PRIMARY KEY,"
              "author VARCHAR(100) NOT NULL,"
              "content TEXT NOT NULL,"
              "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
              ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
```

---

## 5. HTTPS + TLS (Priority 4)

### 5.1 架构变更

原始版：`Connection` 直接持有 `tcp::socket`  
增强版：引入**多态 Connection 体系**

```
        Connection (基类)
        │  virtual tcp::socket& getSocket()
        │
        ├─ 纯 HTTP: Connection::socket 直接操作
        │
        └─ HTTPS: SslConnection
               ssl::stream<tcp::socket> ssl_socket
               getSocket() → ssl_socket.next_layer()
```

**关键设计**：`sendResponse()` / `sendErrorResponse()` 用 `dynamic_pointer_cast<SslConnection>` 自动检测 SSL 连接并分发到对应路径。避免重复 `processRequest`。

### 5.2 新增/修改的方法

| 方法 | 行数 | 说明 |
|---|---|---|
| `initSsl()` | ~25 | 创建 `ssl::context(sslv23)`，选项 `no_sslv2\|no_sslv3\|single_dh_use`，加载证书链 + 私钥 |
| `startSslAccept()` | ~15 | 在 `ssl_acceptor_` 上异步接受 |
| `handleSslAccept()` | ~35 | 注册连接 → `startSslHandshake()` |
| `startSslHandshake()` | ~5 | `ssl_socket.async_handshake(server)` |
| `handleSslHandshake()` | ~15 | 握手完成 → `startSslRead()` |
| `startSslRead()` | ~10 | `ssl_socket.async_read_some()` |
| `handleSslRead()` | ~50 | 同 HTTP handleRead，使用 `ssl_socket` |
| `handleSslWrite()` | ~15 | 写入完成 → keep-alive 或清理 |
| `sendSslResponse()` | ~10 | `async_write` 到 `ssl_socket` |
| `sendSslErrorResponse()` | ~15 | 额外注入 `Strict-Transport-Security` 头部 |
| `cleanupSslConnection()` | ~10 | 关闭 SSL + TCP socket |

### 5.3 TLS 证书

[certs/server.crt](certs/server.crt) + [certs/server.key](certs/server.key)：
- 自签名，CN=localhost
- RSA 4096 位
- 有效期：2026-06-18 ~ 2027-06-18（1年）

### 5.4 双端口监听

```cpp
WebServer server(8080, "./static", thread_count,
                 8443,                    // ← SSL 端口
                 "certs/server.crt",      // ← 证书
                 "certs/server.key");     // ← 私钥

// 日志输出：
// 服务器初始化完成，20 个工作线程 (HTTP:8080, HTTPS:8443)
// SSL 证书已加载: certs/server.crt
// HTTPS 启动成功，监听端口：8443
```

---

## 6. 测试体系 (Priority 2)

### 6.1 新增文件

| 文件 | 行数 | 测试数 | 覆盖范围 |
|---|---|---|---|
| [test/test_main.cpp](test/test_main.cpp) | 6 | — | GTest 入口 |
| [test/test_http_parser.cpp](test/test_http_parser.cpp) | 229 | 12 | HTTP 解析器 |
| [test/test_thread_pool.cpp](test/test_thread_pool.cpp) | 124 | 8 | 线程池 |
| [test/test_form_parser.cpp](test/test_form_parser.cpp) | 88 | 14 | 表单解析 |
| [test/test_router.cpp](test/test_router.cpp) | 155 | 13 | 路由系统 |

**总计：48 个测试（原 0 个）**

### 6.2 测试框架集成

```cmake
# CMakeLists.txt 新增
option(BUILD_TESTS "Build unit tests" ON)
FetchContent_Declare(googletest GIT_REPOSITORY ... GIT_TAG v1.15.2)
FetchContent_MakeAvailable(googletest)

add_executable(MiniWebServer_tests ...)
target_link_libraries(MiniWebServer_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(MiniWebServer_tests)
```

### 6.3 关键测试场景

**HTTP 解析器**：GET/HEAD/POST/PUT/DELETE 解析、BODY 分块接收、Body 大小限制、非法 Content-Length、Header 值修剪、HTTP 流水线、字节消费追踪、空 Body、Keep-Alive 大小写不敏感

**路由**：精确匹配、方法过滤、未找到路径、单/多参数捕获、段数不匹配、非参数段不匹配、精确先于参数、dispatch 分发、所有 5 种 HTTP 方法

**线程池**：入队执行、多任务并发、乱序执行证明、析构等待任务完成、停止后入队异常、默认线程数、带参数任务、void 返回任务

**表单解析**：URL 解码（纯文本/+→空格/%编码/混合编码/中文 UTF-8）、非法 % 序列、空字符串、单/多键值对、无值键、空 Body、值中含等号、JSON 透传

---

## 7. 压测工具 (Priority 2)

### 7.1 新增文件

[bench/bench_client.cpp](bench/bench_client.cpp) — 136 行

### 7.2 功能

```
用法: bench_client.exe <host> <port> <path> <connections> <requests_per_conn>

示例: bench_client.exe localhost 8080 /api/info 100 100
      → 100 并发连接 × 100 请求/连接 = 10,000 总请求
```

**指标输出**：
- 总耗时（秒）
- 总请求数 / 成功数 / 失败数
- 总传输量（MB）
- QPS（请求/秒）
- 吞吐量（B/KB/MB per second）
- 平均延迟（ms）

**架构**：每个连接一个 `io_context` + 独立线程（避免 strand 竞争），全局 `atomic` 计数器聚合。

---

## 8. Docker 化 (Priority 4)

### 8.1 新增文件

| 文件 | 行数 | 说明 |
|---|---|---|
| [Dockerfile](Dockerfile) | 51 | 多阶段构建（builder → runtime） |
| [docker-compose.yml](docker-compose.yml) | 29 | MySQL 9.5 服务编排 |
| [sql/init.sql](sql/init.sql) | 12 | 容器首次启动自动执行 |

### 8.2 Dockerfile 架构

```dockerfile
# 阶段 1: builder (ubuntu:24.04)
apt install: build-essential cmake ninja-build libboost-all-dev
            libssl-dev libmysqlclient-dev qt6-base-dev libqt6network6
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DBUILD_BENCH=ON
cmake --build . -j$(nproc)

# 阶段 2: runtime (ubuntu:24.04)
apt install: libboost-all-dev libssl3 libmysqlclient21 qt6-base-dev libqt6network6
COPY --from=builder: MiniWebServer, static/, certs/
EXPOSE 8080 8443
CMD ["./MiniWebServer"]
```

### 8.3 Docker Compose

```yaml
mysql:
  image: mysql:9.5
  environment:
    MYSQL_ROOT_PASSWORD: 710978
    MYSQL_DATABASE: miniwebserver
  ports: "3306:3306"
  volumes:
    - mysql_data:/var/lib/mysql        # 持久化
    - ./sql/init.sql:/docker-entrypoint-initdb.d/01-init.sql:ro  # 自动建表
  healthcheck:
    test: ["CMD", "mysqladmin", "ping", "-h", "localhost"]
```

---

## 9. CMake 构建系统改进

### 9.1 路径配置化（不再硬编码）

| 依赖 | 旧方式 | 新方式 |
|---|---|---|
| Boost | `set(BOOST_ROOT "D:/...")` 硬编码 | `$ENV{BOOST_ROOT}` → `-DBOOST_ROOT=...` → 系统默认 |
| MySQL | ❌ 不存在 | `$ENV{MYSQL_DIR}` → `-DMYSQL_DIR=...` → 默认路径 |
| OpenSSL | ❌ 不存在 | `$ENV{OPENSSL_ROOT_DIR}` → `-DOPENSSL_ROOT_DIR=...` → vcpkg |

### 9.2 新增构建选项

```cmake
option(BUILD_TESTS "Build unit tests" ON)       # 单元测试（可关闭）
option(BUILD_BENCH "Build benchmark client" ON)  # 压测工具（可关闭）
```

### 9.3 新增 POST_BUILD 复制

```
libmysql.dll       → 输出目录
libssl-3-x64.dll   → 输出目录
libcrypto-3-x64.dll → 输出目录
certs/             → 输出目录/certs
```

### 9.4 新增目标

| 目标 | 源文件 | 链接 |
|---|---|---|
| `MiniWebServer` | +router.cpp, form_parser.cpp, mysql_wrapper.cpp | +libmysql.lib, libssl.lib, libcrypto.lib |
| `MiniWebServer_tests` | 5 个测试文件 + 被测 .cpp | GTest::gtest_main |
| `MiniWebServer_bench` | bench_client.cpp | ws2_32, mswsock |

---

## 10. Bug 修复清单

| # | 严重度 | 描述 | 文件 | 修复方式 |
|---|---|---|---|---|
| 1 | 🔴 严重 | HEADER_KEY 状态只切换状态，丢失每隔一行的 header（导致 keep-alive 无法识别） | [src/http_parser.cpp](src/http_parser.cpp) | 合并 HEADER_KEY 到 header 处理的 fall-through |
| 2 | 🔴 严重 | `processRequest` 中 `/` → `index.html` 修正写到了 `path` 但文件路径用的是未修正的 `route_path`（导致首页 404） | [src/server.cpp:442](src/server.cpp#L442) | `static_dir_ + route_path` → `static_dir_ + path` |
| 3 | 🟡 中等 | `buildHttpResponse` 拼写错误 `"Cache-Control"` 少一个 `a` | [src/server.cpp](src/server.cpp) | 修正为 `"Cache-Control"` |
| 4 | 🟡 中等 | `std::stoul` 无异常保护，非法 Content-Length 导致未定义行为 | [src/http_parser.cpp](src/http_parser.cpp) | 添加 try/catch，失败进入 ERROR 状态 |
| 5 | 🟡 中等 | `std::_Invoke_result_t` 是 MSVC 内部类型，不兼容 Clang/GCC | [include/thread_pool.h](include/thread_pool.h) | 未修复（已知 MSVC 兼容性问题） |
| 6 | 🟢 低 | `worker.join()` 未检查 `joinable()` | [include/thread_pool.h](include/thread_pool.h) | 添加 `if(worker.joinable())` |
| 7 | 🟢 低 | 自定义 delimiter raw string `R"HTML(...)HTML"` 避免 `)"` 提前关闭 | [src/main.cpp](src/main.cpp) | guestbook.html 生成使用 `R"HTML(`
| 8 | 🟢 低 | `conn->socket` 直接访问对 SSL 连接无效（socket 已被 move） | [src/server.cpp](src/server.cpp) | 统一使用 `conn->getSocket()` 虚函数 |

---

## 11. 前端页面变更

### 11.1 index.html 更新

| 项目 | 原始 | 增强版 |
|---|---|---|
| HTTP 方法描述 | "GET/HEAD 支持" | "GET/HEAD/POST 支持" |
| 新功能条目 | 3 个 | 6 个 (+ 路由分发、留言板、HTTPS) |
| 导航链接 | 无 | +2 个链接（/form.html, /api/info） |

### 11.2 新增页面

**form.html**（投稿表单，约 100 行）：
- POST /api/echo 的测试页面
- 猫猫主题深色 UI
- 表单字段：name、message
- Fetch API 发送 application/x-www-form-urlencoded

**guestbook.html**（留言板 SPA，约 180 行）：
- 完整的留言板单页应用
- POST /api/messages 发送留言
- GET /api/messages 加载列表（5 秒自动刷新）
- DELETE /api/message/:id 删除留言
- `escapeHtml()` 防 XSS

---

## 12. 关键设计决策

| # | 决策 | 理由 |
|---|---|---|
| 1 | MySQL C API 而非 Connector/C++ | 避免额外依赖，C API 更轻量、学习价值更高 |
| 2 | 连接池而非单连接 | 并发安全的连接池（互斥锁 + 条件变量）匹配多线程架构 |
| 3 | RouteHandler 返回 `HttpResponse` 而非 `string` | 支持状态码多样性（201/400/404/500），符合 RESTful 规范 |
| 4 | `dynamic_pointer_cast` 检测 SSL | 避免重复 `processRequest` 代码，单一函数同时处理 HTTP/HTTPS |
| 5 | `shared_mutex` 保护路由表 | 写锁仅启动时使用，运行时全是读锁 → 零竞争 |
| 6 | 精确匹配优先于参数匹配 | O(1) 哈希查找 > O(n) 线性扫描；允许特定路径覆盖泛型路由 |
| 7 | 多阶段 Docker 构建 | builder 包含完整编译工具链 800MB+，runtime 仅需运行时库 ~200MB |
| 8 | `mysql_real_escape_string` 而非 prepared statements | 实现简单，当前阶段足够；后续可升级为 `mysql_stmt_*` API |

---

## 13. 统计总结

```
                   原始版          增强版          差异
─────────────────────────────────────────────────────
源文件              9              26             +17
头文件              6              10             +4
代码行数          ~2,550          ~4,400         +1,850
HTTP 方法           2               5              +3
单元测试            0              48             +48
压测工具            0               1              +1
Docker 文件         0               3              +3
TLS 证书            0               2              +2
CMake 目标          1               3              +2
构建选项            0               2              +2
POST_BUILD 步骤     1               5              +4
链接库              2               7              +5
Bug 修复            —               8              —
```

---

## 14. 文件变更完整索引

### 修改的文件（9 个）

| 文件 | Δ 行数 | 章节 |
|---|---|---|
| [CMakeLists.txt](CMakeLists.txt) | +168 / -10 | §9 |
| [include/http_parser.h](include/http_parser.h) | +15 / -1 | §2 |
| [include/server.h](include/server.h) | +171 / -69 | §5 |
| [include/thread_pool.h](include/thread_pool.h) | +4 / -4 | §10 |
| [src/http_parser.cpp](src/http_parser.cpp) | +174 / -118 | §2 |
| [src/main.cpp](src/main.cpp) | +310 / -9 | §11 |
| [src/mainWindow.cpp](src/mainwindow.cpp) | +217 / -2 | §3,§4 |
| [src/server.cpp](src/server.cpp) | +584 / -270 | §3,§5 |
| [src/server_monitor.cpp](src/server_monitor.cpp) | +6 | — |

### 新增的文件（17 个）

| 文件 | 行数 | 章节 |
|---|---|---|
| [include/router.h](include/router.h) | 83 | §3 |
| [src/router.cpp](src/router.cpp) | 136 | §3 |
| [include/form_parser.h](include/form_parser.h) | 16 | — |
| [src/form_parser.cpp](src/form_parser.cpp) | 63 | — |
| [include/mysql_wrapper.h](include/mysql_wrapper.h) | 113 | §4 |
| [src/mysql_wrapper.cpp](src/mysql_wrapper.cpp) | 196 | §4 |
| [test/test_main.cpp](test/test_main.cpp) | 6 | §6 |
| [test/test_http_parser.cpp](test/test_http_parser.cpp) | 229 | §6 |
| [test/test_thread_pool.cpp](test/test_thread_pool.cpp) | 124 | §6 |
| [test/test_form_parser.cpp](test/test_form_parser.cpp) | 88 | §6 |
| [test/test_router.cpp](test/test_router.cpp) | 155 | §6 |
| [bench/bench_client.cpp](bench/bench_client.cpp) | 136 | §7 |
| [Dockerfile](Dockerfile) | 51 | §8 |
| [docker-compose.yml](docker-compose.yml) | 29 | §8 |
| [sql/init.sql](sql/init.sql) | 12 | §4 |
| [certs/server.crt](certs/server.crt) | 29 | §5 |
| [certs/server.key](certs/server.key) | 52 | §5 |
