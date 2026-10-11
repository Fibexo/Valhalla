module;
#include <format>
#include <print>

#ifdef __ANDROID__
#include <android/log.h>
#endif
export module LoggerImpl;

export import Logger;

export class LoggerImpl : public ILogger
{
public:
	LoggerImpl() = default;
	~LoggerImpl() override = default;
protected:
	void LogImpl(const char* message) override
	{
#ifdef __ANDROID__
		__android_log_print(ANDROID_LOG_ERROR, "Valhalla", "%s", message);
#else
		std::print("{}\n", message);
#endif
	}
};
