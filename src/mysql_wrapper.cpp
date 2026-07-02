#include "mysql_wrapper.h"
#include "logger.h"
#include <cstring>

// ========== MysqlConnection ==========

MysqlConnection::MysqlConnection() {
	mysql_init(&mysql_);
}

MysqlConnection::~MysqlConnection() {
	close();
}

bool MysqlConnection::connect(const std::string& host, uint16_t port,
                              const std::string& user, const std::string& password,
                              const std::string& database) {
	if (connected_) {
		close();
		mysql_init(&mysql_);
	}

	// 设置字符集
	mysql_options(&mysql_, MYSQL_SET_CHARSET_NAME, "utf8mb4");

	if (!mysql_real_connect(&mysql_, host.c_str(), user.c_str(), password.c_str(),
	                        database.c_str(), port, nullptr, 0)) {
		LOG_ERROR("MySQL 连接失败: {}", mysql_error(&mysql_));
		connected_ = false;
		return false;
	}

	connected_ = true;
	LOG_INFO("MySQL 已连接: {}@{}:{}/{}", user, host, port, database);
	return true;
}

bool MysqlConnection::execute(const std::string& sql) {
	if (!connected_) return false;

	if (mysql_real_query(&mysql_, sql.c_str(), sql.size()) != 0) {
		LOG_ERROR("SQL 执行失败: {} — SQL: {}", mysql_error(&mysql_), sql);
		return false;
	}

	// 消费结果集（如果有的话）
	MYSQL_RES* res = mysql_store_result(&mysql_);
	if (res) {
		mysql_free_result(res);
	}

	return true;
}

std::vector<std::unordered_map<std::string, std::string>> MysqlConnection::query(const std::string& sql) {
	std::vector<std::unordered_map<std::string, std::string>> result;

	if (!connected_) return result;

	if (mysql_real_query(&mysql_, sql.c_str(), sql.size()) != 0) {
		LOG_ERROR("SQL 查询失败: {} — SQL: {}", mysql_error(&mysql_), sql);
		return result;
	}

	MYSQL_RES* res = mysql_store_result(&mysql_);
	if (!res) return result;

	// 获取列信息
	int num_fields = mysql_num_fields(res);
	MYSQL_FIELD* fields = mysql_fetch_fields(res);

	// 预先保存列名
	std::vector<std::string> col_names;
	col_names.reserve(num_fields);
	for (int i = 0; i < num_fields; ++i) {
		col_names.emplace_back(fields[i].name);
	}

	// 遍历每行
	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res))) {
		unsigned long* lengths = mysql_fetch_lengths(res);
		std::unordered_map<std::string, std::string> row_map;
		for (int i = 0; i < num_fields; ++i) {
			if (row[i]) {
				row_map[col_names[i]] = std::string(row[i], lengths[i]);
			} else {
				row_map[col_names[i]] = "";  // NULL → 空字符串
			}
		}
		result.push_back(std::move(row_map));
	}

	mysql_free_result(res);
	return result;
}

uint64_t MysqlConnection::lastInsertId() {
	return mysql_insert_id(&mysql_);
}

std::string MysqlConnection::escape(const std::string& str) {
	if (str.empty()) return "";

	std::string escaped(str.size() * 2 + 1, '\0');
	unsigned long len = mysql_real_escape_string(&mysql_,
		escaped.data(), str.c_str(), str.size());
	escaped.resize(len);
	return escaped;
}

bool MysqlConnection::ping() {
	if (!connected_) return false;
	return mysql_ping(&mysql_) == 0;
}

void MysqlConnection::close() {
	if (connected_) {
		mysql_close(&mysql_);
		connected_ = false;
	}
}

// ========== MysqlPool ==========

MysqlPool::MysqlPool(const Config& config) : config_(config) {
	// 预创建所有连接
	for (size_t i = 0; i < config_.pool_size; ++i) {
		auto conn = std::make_unique<MysqlConnection>();
		if (conn->connect(config_.host, config_.port, config_.user,
		                  config_.password, config_.database)) {
			pool_.push_back(std::move(conn));
		} else {
			LOG_WARNING("连接池: 创建连接 {}/{} 失败", i + 1, config_.pool_size);
		}
	}
	LOG_INFO("MySQL 连接池已创建: {}/{} 个连接", pool_.size(), config_.pool_size);
}

MysqlPool::~MysqlPool() {
	// 所有连接由 RAII 自动关闭
}

MysqlPool::ConnectionGuard::ConnectionGuard(MysqlPool* pool, std::unique_ptr<MysqlConnection> conn)
	: pool_(pool), conn_(std::move(conn)) {}

MysqlPool::ConnectionGuard::~ConnectionGuard() {
	if (conn_ && pool_) {
		pool_->returnConnection(std::move(conn_));
	}
}

MysqlPool::ConnectionGuard MysqlPool::acquire() {
	std::unique_lock lock(mutex_);

	// 等待直到有可用连接
	cv_.wait(lock, [this] {
		return !pool_.empty();
	});

	auto conn = std::move(pool_.back());
	pool_.pop_back();
	in_use_++;

	return ConnectionGuard(this, std::move(conn));
}

void MysqlPool::returnConnection(std::unique_ptr<MysqlConnection> conn) {
	std::lock_guard lock(mutex_);
	// 检查连接是否仍有效，无效则重连
	if (!conn->ping()) {
		LOG_WARNING("连接池: 连接失效，尝试重连...");
		if (conn->connect(config_.host, config_.port, config_.user,
		                  config_.password, config_.database)) {
			LOG_INFO("连接池: 重连成功");
		} else {
			LOG_ERROR("连接池: 重连失败，丢弃连接");
			// 创建新连接替代
			auto new_conn = std::make_unique<MysqlConnection>();
			if (new_conn->connect(config_.host, config_.port, config_.user,
			                      config_.password, config_.database)) {
				conn = std::move(new_conn);
			} else {
				conn = nullptr;
			}
		}
	}
	if (conn) {
		pool_.push_back(std::move(conn));
	}
	in_use_--;
	cv_.notify_one();
}

size_t MysqlPool::available() const {
	return pool_.size();
}