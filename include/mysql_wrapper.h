#pragma once
//MySQL RAII 封装 + 连接池
#include <mysql.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>

// ========== MySQL 数据库连接（RAII） ==========
class MysqlConnection {
public:
	MysqlConnection();
	~MysqlConnection();

	// 禁止拷贝
	MysqlConnection(const MysqlConnection&) = delete;
	MysqlConnection& operator=(const MysqlConnection&) = delete;

	// 连接数据库
	bool connect(const std::string& host, uint16_t port,
	             const std::string& user, const std::string& password,
	             const std::string& database);

	// 执行 SQL（无结果集）
	bool execute(const std::string& sql);

	// 执行查询并返回所有行（列名→值映射）
	std::vector<std::unordered_map<std::string, std::string>> query(const std::string& sql);

	// 获取最后插入的 ID
	uint64_t lastInsertId();

	// 转义字符串（防 SQL 注入）
	std::string escape(const std::string& str);

	// 检查连接是否有效
	bool ping();

	// 关闭连接
	void close();

	// 是否已连接
	bool isConnected() const { return connected_; }

	// 获取原生句柄
	MYSQL* native() { return &mysql_; }

private:
	MYSQL mysql_;
	bool connected_ = false;
};

// ========== MySQL 连接池 ==========
class MysqlPool {
public:
	// config 结构
	struct Config {
		std::string host = "127.0.0.1";
		uint16_t port = 3306;
		std::string user = "root";
		std::string password;
		std::string database;
		size_t pool_size = 8;
	};

	explicit MysqlPool(const Config& config);
	~MysqlPool();

	// 禁止拷贝
	MysqlPool(const MysqlPool&) = delete;
	MysqlPool& operator=(const MysqlPool&) = delete;

	// 获取一个连接的 RAII 守卫（自动归还）
	class ConnectionGuard {
	public:
		ConnectionGuard(MysqlPool* pool, std::unique_ptr<MysqlConnection> conn);
		~ConnectionGuard();

		MysqlConnection* operator->() { return conn_.get(); }
		MysqlConnection& operator*() { return *conn_; }

		ConnectionGuard(ConnectionGuard&&) = default;
		ConnectionGuard& operator=(ConnectionGuard&&) = default;

	private:
		MysqlPool* pool_;
		std::unique_ptr<MysqlConnection> conn_;
	};

	// 获取连接（阻塞直到有可用连接）
	ConnectionGuard acquire();

	// 便捷方法：在连接上执行操作
	template<typename F>
	auto withConnection(F&& func) -> decltype(func(std::declval<MysqlConnection&>())) {
		auto guard = acquire();
		return func(*guard);
	}

	// 当前可用连接数
	size_t available() const;

private:
	void returnConnection(std::unique_ptr<MysqlConnection> conn);

	Config config_;
	std::vector<std::unique_ptr<MysqlConnection>> pool_;
	std::mutex mutex_;
	std::condition_variable cv_;
	size_t in_use_ = 0;
};