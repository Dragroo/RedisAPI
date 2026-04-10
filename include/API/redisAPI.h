#pragma once
#include "stdafx.h"
#include "redisOperate.h"

// 获取Redis中的JSON数据并反序列化为指定类型
template <typename T>
inline T getRedisData(const std::string& key) {
	T retData{};
	try {
		auto& redis = RedisOperate::GetInstance();
		json retJson = redis.GetJson(key);
		if (retJson.is_null()) {
			return retData;
		}
		retData = retJson.get<T>();  // 反序列化为指定类型
		return retData;
	}
	catch (const std::exception& e) {
		//ALARM(AL_INFO, 1002, FormatString()("JSON解析异常: %s", e.what()));
		return retData;
	}
	catch (...) {
		//ALARM(AL_INFO, 1002, "未知异常");
		return retData;
	}
}

// 将对象序列化为JSON并存储到Redis
template <typename T>
inline bool setRedisData(const std::string& key, const T& data) {
	try {
		auto& redis = RedisOperate::GetInstance();
		json retJson = data;  // 自动序列化
		redis.SetJson(key, retJson);
		return true;
	}
	catch (...) {
		//ALARM(AL_INFO, 1002, "异常");
		return false;
	}
}

template<>
inline bool setRedisData<std::string>(const std::string& key, const std::string& data) {
	try {
		auto& redis = RedisOperate::GetInstance();
		return redis.SetString(key, data);
	}
	catch (...) {
		return false;
	}
}

template<>
inline std::string getRedisData<std::string>(const std::string& key) {
	std::string retStr;
	try {
		auto& redis = RedisOperate::GetInstance();
		return redis.GetString(key);
	}
	catch (...) {
		return retStr;
	}
}

// double类型的特化 - 写入时将double转为string存储
template<>
inline bool setRedisData<double>(const std::string& key, const double& data) {
	try {
		auto& redis = RedisOperate::GetInstance();
		// 将double转换为string
		std::string strValue = std::to_string(data);
		return redis.SetString(key, strValue);
	}
	catch (...) {
		return false;
	}
}

// double类型的特化 - 读取时将string转换为double
template<>
inline double getRedisData<double>(const std::string& key) {
	double retValue = 0.0;
	try {
		auto& redis = RedisOperate::GetInstance();
		std::string strValue = redis.GetString(key);
		if (!strValue.empty()) {
			retValue = std::stod(strValue);
		}
		return retValue;
	}
	catch (const std::exception& e) {
		// 处理转换异常，例如字符串不是有效的double格式
		ALARM(AL_INFO, 1002, FormatString()("double转换异常: %s", (char *)e.what()));
		return retValue;
	}
	catch (...) {
		return retValue;
	}
}
