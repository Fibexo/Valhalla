module;

#include <format>

export module Logger;

export class ILogger
{
protected:
	virtual void LogImpl(const char* message) = 0;

public:
	virtual ~ILogger() = default;

	template <class... Args>
	void Log(const std::format_string<Args...> format, Args&&... args)
	{
		LogImpl(std::format(format, std::forward<Args>(args)...).c_str());
	}
};

export ILogger* Logger = nullptr;