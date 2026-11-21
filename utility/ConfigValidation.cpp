#include "ConfigValidation.h"
#include <boost/property_tree/json_parser.hpp>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <linux/memfd.h>
#include <spawn.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;
namespace utility
{
namespace
{
using Tree = boost::property_tree::ptree;
[[noreturn]] void Unavailable(const std::string& file)
{
	Tree issue, issues;
	issue.put("file", file);
	issue.put("path", "/");
	issue.put("message",
						"Configuration validator is unavailable. Check Python dependencies and bundled validation resources.");
	issues.push_back({"", issue});
	throw ConfigValidationError(std::move(issues), true);
}
struct Descriptor
{
	int value = -1;
	Descriptor() : value(static_cast<int>(syscall(SYS_memfd_create, "onvif-config-validation", MFD_CLOEXEC)))
	{
	}
	~Descriptor()
	{
		if (value >= 0)
			close(value);
	}
};
struct Actions
{
	posix_spawn_file_actions_t value;
	int error;
	Actions() : error(posix_spawn_file_actions_init(&value))
	{
	}
	~Actions()
	{
		if (!error)
			posix_spawn_file_actions_destroy(&value);
	}
};
struct Child
{
	pid_t pid = -1;
	~Child()
	{
		if (pid > 0)
		{
			kill(pid, SIGKILL);
			while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR)
			{
			}
		}
	}
};
std::filesystem::path Resources()
{
	std::error_code error;
	auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
	if (!error)
	{
		auto adjacent = executable.parent_path() / "config_validation";
		if (std::filesystem::is_regular_file(adjacent / "scripts/validate_configs.py", error))
			return adjacent;
	}
	return OSRV_CONFIG_VALIDATION_DIR;
}
} // namespace
void ValidateConfig(const std::string& filename, const std::string& candidate)
{
	Descriptor input, output, diagnostics;
	if (input.value < 0 || output.value < 0 || diagnostics.value < 0)
		Unavailable(filename);
	std::size_t offset = 0;
	while (offset < candidate.size())
	{
		auto written = write(input.value, candidate.data() + offset, candidate.size() - offset);
		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			Unavailable(filename);
		offset += static_cast<std::size_t>(written);
	}
	if (lseek(input.value, 0, SEEK_SET) < 0)
		Unavailable(filename);
	auto script = (Resources() / "scripts/validate_configs.py").string();
	std::string python = OSRV_VALIDATION_PYTHON;
	char* arguments[] = {python.data(), script.data(), const_cast<char*>("--stdin"), const_cast<char*>(filename.c_str()),
											 nullptr};
	Actions actions;
	if (actions.error || posix_spawn_file_actions_adddup2(&actions.value, input.value, STDIN_FILENO) ||
			posix_spawn_file_actions_adddup2(&actions.value, output.value, STDOUT_FILENO) ||
			posix_spawn_file_actions_adddup2(&actions.value, diagnostics.value, STDERR_FILENO) ||
			posix_spawn_file_actions_addclosefrom_np(&actions.value, 3))
		Unavailable(filename);
	Child child;
	if (posix_spawn(&child.pid, python.c_str(), &actions.value, nullptr, arguments, environ))
	{
		child.pid = -1;
		Unavailable(filename);
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	int status = 0;
	for (;;)
	{
		auto result = waitpid(child.pid, &status, WNOHANG);
		if (result == child.pid)
		{
			child.pid = -1;
			break;
		}
		if (result < 0 && errno != EINTR)
			Unavailable(filename);
		if (std::chrono::steady_clock::now() >= deadline)
			Unavailable(filename);
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		Unavailable(filename);
	struct stat info{};
	if (fstat(output.value, &info) || info.st_size <= 0 || info.st_size > 65536)
		Unavailable(filename);
	std::string report(static_cast<std::size_t>(info.st_size), '\0');
	if (pread(output.value, report.data(), report.size(), 0) != static_cast<ssize_t>(report.size()))
		Unavailable(filename);
	Tree result;
	try
	{
		std::istringstream json(report);
		boost::property_tree::read_json(json, result);
	}
	catch (const std::exception&)
	{
		Unavailable(filename);
	}
	if (!result.get<bool>("valid", false))
	{
		auto issues = result.get_child_optional("errors");
		if (!issues || issues->empty())
			Unavailable(filename);
		throw ConfigValidationError(*issues);
	}
}
} // namespace utility
