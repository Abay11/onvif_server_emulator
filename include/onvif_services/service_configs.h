#pragma once


#include <boost/property_tree/ptree_fwd.hpp>

#include <string>
#include <memory>
#include <array>


namespace osrv
{
	using PTreeSP = std::shared_ptr<boost::property_tree::ptree>;

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