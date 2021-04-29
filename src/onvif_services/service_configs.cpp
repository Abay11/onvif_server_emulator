#include "..\..\include\onvif_services\service_configs.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace
{
	namespace pt = boost::property_tree;

	class ConfigName
	{
	public:

		ConfigName(const std::string& name, const std::string& file_ext)
			: name_(name), file_ext_(file_ext) {}

		ConfigName(const std::string name)
		{
			ConfigName(name, ".config");
		}

		operator std::string() const {
			return name_ + file_ext_;
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
}

namespace osrv
{
	ServiceConfigs::operator const PTreeSP()
	{
		auto json_config_ = std::make_shared<pt::ptree>();
		pt::read_json(ConfigPath(config_path_, ConfigName(service_name_)), *json_config_);
		return json_config_;
	}
}
