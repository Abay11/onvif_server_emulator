#pragma once


#include <boost/property_tree/ptree_fwd.hpp>

#include <string>
#include <memory>
#include <array>


namespace osrv
{
	using PTreeSP = std::shared_ptr<boost::property_tree::ptree>;

	class ConfigName
	{
	public:

		ConfigName(const std::string& name, const std::string& file_ext)
			: name_(name), file_ext_(file_ext)
		{
		}

		ConfigName(const std::string& name)
			: ConfigName(name, ".config")
		{
		}

		operator std::string() const {
			auto res = name_;
			if (!file_ext_.empty() && file_ext_.front() != '.')
				res += '.';

			res += file_ext_;
			return res;
		}

	private:
		const std::string name_;
		const std::string file_ext_;
	};
	
	class ConfigPath
	{
	public:
		ConfigPath(const std::string& path, const std::string file)
			: path_(path), file_(file) {}

		operator std::string() const {
			return path_ + (path_.back() == '/' ? "" : "/") + file_;
		}

	private:
		const std::string path_;
		const std::string file_;
	};

	class ServiceConfigs
	{
	public:
		explicit ServiceConfigs(const std::string& name, const std::string& path)
			: service_name_(name), config_path_(path)
		{
		}

		operator const PTreeSP();

	private:
		const std::string service_name_;
		const std::string config_path_;
	};
}