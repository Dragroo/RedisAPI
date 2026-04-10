#ifndef REDIS_OPERATE_H
#define REDIS_OPERATE_H


#include <hiredis/hiredis.h>
#include <string>
#include <memory>
#include <mutex>

using json = nlohmann::json;

class RedisOperate {
public:
	// 禁止拷贝构造和赋值（单例核心）
	RedisOperate(const RedisOperate&) = delete;
	RedisOperate& operator=(const RedisOperate&) = delete;

	// 1. 初始化连接（需在程序启动时调用，配置IP/端口/密码/数据库）
	// password 参数可选，如果Redis未设置密码可传空字符串
	static bool Initialize(const std::string& ip, int port, const std::string& password, int dbnum) {
		if (s_instance) {
			// 已初始化：检查参数是否一致，避免重复配置冲突
			if (s_instance->ip_ != ip || s_instance->port_ != port || s_instance->password_ != password || s_instance->dbnum_ != dbnum) {
				std::cerr << "[ERROR] Redis初始化参数冲突（已有实例："
					<< s_instance->ip_ << ":" << s_instance->port_ << "/db" << s_instance->dbnum_ << "）" << std::endl;
				return false;
			}
			return s_instance->IsConnected(); // 返回当前连接状态
		}
		// 首次初始化：创建单例实例
		s_instance = new RedisOperate(ip, port, password, dbnum);
		return s_instance->IsConnected();
	}
	
	// 向后兼容版本：无密码初始化（password默认为空）
	static bool Initialize(const std::string& ip, int port, int dbnum) {
		return Initialize(ip, port, "", dbnum);
	}

	// 2. 获取全局唯一实例（需先调用Initialize）
	static RedisOperate& GetInstance() {
		if (!s_instance) {
			throw std::runtime_error("RedisOperate未初始化！请先调用Initialize(ip, port, [password], dbnum)");
		}
		return *s_instance;
	}

	// 3. 连接状态检查（含实时错误检测，非仅依赖初始连接状态，线程安全）
	bool IsConnected() const {
		std::lock_guard<std::mutex> lock(mutex_); // 加锁保护
		return IsConnectedUnlocked(); // 调用不加锁的内部版本
	}
	
	// 内部版本：不加锁的连接状态检查（供已持有锁的方法调用）
	bool IsConnectedUnlocked() const {
		return context_ && !context_->err && connected_; // 需同时满足：context有效、无错误、初始连接成功
	}

	// 4. 保留原有核心功能接口（无需修改用户调用代码）
	bool SetString(const std::string& key, const std::string& value);
	std::string GetString(const std::string& key);
	bool KeyExists(const std::string& key);
	bool DeleteKey(const std::string& key);
	bool SetExpire(const std::string& key, int seconds);
	bool SetJson(const std::string& key, const json& value);
	json GetJson(const std::string& key);
	bool PipelineSet(const std::vector<std::pair<std::string, std::string>>& kvs);
	
	// 6. 密码认证接口（可用于运行时修改密码）
	bool Authenticate(const std::string& password);
	
	// 5. 数据库管理接口
	bool SelectDB(int dbnum);
	int GetCurrentDB() const; // 获取当前数据库号

private:
	// 单例模式：私有构造/析构，禁止外部实例化
	RedisOperate(const std::string& ip, int port, const std::string& password, int dbnum);
	~RedisOperate() { if (context_) redisFree(context_); }
	// 5. 自动重连机制（连接断开时调用，复用IP/端口/密码/数据库参数）
	// 注意：此方法假设调用者已持有锁
	bool Reconnect() {
		if (context_) {
			redisFree(context_); // 释放旧连接
			context_ = nullptr;
		}
		// 重新建立连接
		const struct timeval timeout = { 1, 500000 }; // 1.5秒超时
		context_ = redisConnectWithTimeout(ip_.c_str(), port_, timeout);
		if (!context_ || context_->err) {
			std::cerr << "[ERROR] Redis重连失败: " << (context_ ? context_->errstr : "无法分配连接上下文") << std::endl;
			connected_ = false;
			return false;
		}
		// 重连后进行密码认证（如果设置了密码）
		if (!password_.empty()) {
			if (!AuthUnlocked(password_)) {
				std::cerr << "[ERROR] Redis重连后认证失败" << std::endl;
				connected_ = false;
				return false;
			}
		}
		// 认证后重新选择数据库（SelectDB 内部不加锁，因为调用者已持有锁）
		connected_ = SelectDBUnlocked(dbnum_);
		return connected_;
	}

	// 内部版本：不加锁的数据库选择（供已持有锁的方法调用）
	bool SelectDBUnlocked(int dbnum);
	
	// 内部版本：不加锁的密码认证（供已持有锁的方法调用）
	bool AuthUnlocked(const std::string& password);

	// 7. 错误日志记录（复用原始逻辑）
	void LogError(const std::string& action, redisReply* reply);

	// 成员变量
	static RedisOperate* s_instance; // 单例实例指针
	redisContext* context_ = nullptr; // Redis连接上下文
	std::string ip_;                 // 保存连接参数（用于重连）
	int port_;                       // 保存连接参数（用于重连）
	std::string password_;           // 保存密码参数（用于重连）
	int dbnum_;                      // 保存连接参数（用于重连）
	int current_dbnum_;              // 当前数据库号（用于跟踪）
	bool connected_ = false;         // 初始连接状态标记
	mutable std::mutex mutex_;       // 互斥锁，保护所有Redis操作（线程安全）
};

// 初始化静态单例指针
//RedisOperate* RedisOperate::s_instance = nullptr;



#endif // REDIS_OPERATE_H