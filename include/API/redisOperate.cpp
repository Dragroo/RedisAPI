#include "stdafx.h"
#include "redisOperate.h"

// 链接Windows网络库和hiredis库（根据实际库名调整）
#pragma comment(lib, "ws2_32.lib")       // Windows Sockets库
#pragma comment(lib, "hiredis.lib")

/**
*
// 1. 程序启动时初始化（全局一次）
if (!RedisOperate::Initialize("127.0.0.1", 6379, 1)) {
	std::cerr << "Redis初始化失败，程序退出" << std::endl;
	return 1;
}

// 2. 后续所有操作通过单例实例完成（复用同一连接）
auto& redis = RedisOperate::GetInstance();

// 3. 原有操作接口完全兼容，无需修改
if (redis.SetString("mykey", "hello_cpp")) {
	std::cout << "写入成功!" << std::endl;
}

std::string val = redis.GetString("mykey");
std::cout << "读取结果: " << val << std::endl;

// JSON操作示例
json user = {{"id", 1001}, {"name", "Sean"}, {"roles", {"admin", "engineer"}}, {"active", true}};
redis.SetJson("user:1001", user);
*
*
**/

// 静态成员变量的定义与初始化（全局作用域）
RedisOperate* RedisOperate::s_instance = nullptr;
// 构造函数：初始化连接（私有，仅单例内部调用）
RedisOperate::RedisOperate(const std::string& ip, int port, const std::string& password, int dbnum)
	: ip_(ip), port_(port), password_(password), dbnum_(dbnum), current_dbnum_(dbnum) {
	const struct timeval timeout = { 1, 500000 }; // 1.5秒连接超时
	context_ = redisConnectWithTimeout(ip.c_str(), port, timeout);
	if (context_ && !context_->err) {
		// 如果设置了密码，先进行认证
		if (!password.empty()) {
			connected_ = AuthUnlocked(password);
			if (!connected_) {
				std::cerr << "[ERROR] Redis密码认证失败" << std::endl;
				return;
			}
		}
		// 认证成功后选择数据库
		connected_ = SelectDB(dbnum);
	}
	else {
		std::cerr << "[ERROR] Redis初始连接失败: "
			<< (context_ ? context_->errstr : "无法分配连接上下文") << std::endl;
	}
}

// 密码认证（线程安全）
bool RedisOperate::Authenticate(const std::string& password) {
	std::lock_guard<std::mutex> lock(mutex_);
	bool success = AuthUnlocked(password);
	if (success) {
		password_ = password; // 更新保存的密码
	}
	return success;
}

// 内部版本：不加锁的密码认证（供已持有锁的方法调用）
bool RedisOperate::AuthUnlocked(const std::string& password) {
	redisReply* reply = (redisReply*)redisCommand(context_, "AUTH %s", password.c_str());
	if (!reply) return false;
	bool success = (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK");
	if (!success) {
		LogError("AUTH", reply);
	}
	freeReplyObject(reply);
	return success;
}

// 选择数据库（复用原始逻辑，线程安全）
bool RedisOperate::SelectDB(int dbnum) {
	std::lock_guard<std::mutex> lock(mutex_); // 加锁保护
	return SelectDBUnlocked(dbnum);
}

// 内部版本：不加锁的数据库选择（供已持有锁的方法调用）
bool RedisOperate::SelectDBUnlocked(int dbnum) {
	redisReply* reply = (redisReply*)redisCommand(context_, "SELECT %d", dbnum);
	if (!reply) return false;
	bool success = (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK");
	if (success) {
		current_dbnum_ = dbnum; // 更新当前数据库号
	}
	freeReplyObject(reply);
	return success;
}

// 获取当前数据库号（线程安全）
int RedisOperate::GetCurrentDB() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return current_dbnum_;
}

// 字符串写入（新增连接检查与重连，线程安全）
bool RedisOperate::SetString(const std::string& key, const std::string& value) {
	std::lock_guard<std::mutex> lock(mutex_); // 加锁保护
	
	// 操作前检查连接，无效则尝试重连（使用不加锁版本，因为已持有锁）
	if (!IsConnectedUnlocked() && !Reconnect()) return false;

	redisReply* reply = (redisReply*)redisCommand(context_, "SET %s %s", key.c_str(), value.c_str());
	if (!reply) {
		std::cerr << "[ERROR] SET命令失败：redisCommand返回空（连接可能已断开）" << std::endl;
		connected_ = false; // 标记连接失效，下次操作触发重连
		return false;
	}

	bool success = (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK");
	if (!success) LogError("SET", reply);
	freeReplyObject(reply);
	return success;
}

// 字符串读取（新增连接检查与重连，线程安全）
std::string RedisOperate::GetString(const std::string& key) {
	std::lock_guard<std::mutex> lock(mutex_); // 加锁保护
	
	if (!IsConnectedUnlocked() && !Reconnect()) return "";

	redisReply* reply = (redisReply*)redisCommand(context_, "GET %s", key.c_str());
	if (!reply) {
		std::cerr << "[ERROR] GET命令失败：redisCommand返回空（连接可能已断开）" << std::endl;
		connected_ = false;
		return "";
	}

	std::string result = (reply->type == REDIS_REPLY_STRING) ? reply->str : "";
	if (reply->type != REDIS_REPLY_STRING) LogError("GET", reply);
	freeReplyObject(reply);
	return result;
}

// Key存在性检查（复用逻辑，增加连接检查，线程安全）
bool RedisOperate::KeyExists(const std::string& key) {
	std::lock_guard<std::mutex> lock(mutex_); // 加锁保护
	
	if (!IsConnectedUnlocked() && !Reconnect()) return false;

	redisReply* reply = (redisReply*)redisCommand(context_, "EXISTS %s", key.c_str());
	if (!reply) {
		connected_ = false;
		return false;
	}

	bool exists = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
	freeReplyObject(reply);
	return exists;
}

// Key删除（复用逻辑，增加连接检查，线程安全）
bool RedisOperate::DeleteKey(const std::string& key) {
	std::lock_guard<std::mutex> lock(mutex_); // 加锁保护
	
	if (!IsConnectedUnlocked() && !Reconnect()) return false;

	redisReply* reply = (redisReply*)redisCommand(context_, "DEL %s", key.c_str());
	if (!reply) {
		connected_ = false;
		return false;
	}

	bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 1);
	if (!success) LogError("DEL", reply);
	freeReplyObject(reply);
	return success;
}

// 设置过期时间（复用逻辑，增加连接检查，线程安全）
bool RedisOperate::SetExpire(const std::string& key, int seconds) {
	std::lock_guard<std::mutex> lock(mutex_); // 加锁保护
	
	if (!IsConnectedUnlocked() && !Reconnect()) return false;

	redisReply* reply = (redisReply*)redisCommand(context_, "EXPIRE %s %d", key.c_str(), seconds);
	if (!reply) {
		connected_ = false;
		return false;
	}

	bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
	if (!success) LogError("EXPIRE", reply);
	freeReplyObject(reply);
	return success;
}

// JSON写入（基于SetString实现）
bool RedisOperate::SetJson(const std::string& key, const json& value) {
	return SetString(key, value.dump()); // JSON序列化后存入
}

// JSON读取（基于GetString实现）
json RedisOperate::GetJson(const std::string& key) {
	std::string raw = GetString(key);
	if (raw.empty()) return json();

	try {
		return json::parse(raw);
	}
	catch (const json::parse_error& e) {
		std::cerr << "[ERROR] JSON解析失败: " << e.what() << std::endl;
		return json();
	}
}

// 错误日志（复用原始逻辑）
void RedisOperate::LogError(const std::string& action, redisReply* reply) {
	std::cerr << "[ERROR] Redis命令失败 [" << action << "] 类型: " << reply->type
		<< ", 内容: " << (reply->str ? reply->str : "无") << std::endl;
}

bool RedisOperate::PipelineSet(const std::vector<std::pair<std::string, std::string>>& kvs) {
	std::lock_guard<std::mutex> lock(mutex_); // 加锁保护
	
	if (!IsConnectedUnlocked() && !Reconnect()) return false;

	// 1. 批量追加命令
	for (const auto& kv : kvs) {
		if (redisAppendCommand(context_, "SET %s %s", kv.first.c_str(), kv.second.c_str()) != REDIS_OK) {
			std::cerr << "[ERROR] Pipeline append failed for key: " << kv.first << std::endl;
			connected_ = false;
			return false;
		}
	}

	// 2. 批量获取结果
	for (size_t i = 0; i < kvs.size(); ++i) {
		redisReply* reply = nullptr;
		if (redisGetReply(context_, (void**)&reply) != REDIS_OK || !reply) {
			std::cerr << "[ERROR] Pipeline get reply failed" << std::endl;
			connected_ = false;
			return false;
		}
		bool success = (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK");
		if (!success) LogError("Pipeline SET", reply);
		freeReplyObject(reply);
	}

	return true;
}
