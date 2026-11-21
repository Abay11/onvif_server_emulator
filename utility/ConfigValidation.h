#pragma once
#include <boost/property_tree/ptree.hpp>
#include <stdexcept>
#include <string>

namespace utility
{
class ConfigValidationError : public std::runtime_error
{
public:
	ConfigValidationError(boost::property_tree::ptree issues, bool unavailable = false)
			: std::runtime_error("Configuration validation failed"), issues(std::move(issues)), unavailable(unavailable)
	{
	}
	boost::property_tree::ptree issues;
	bool unavailable;
};
// Uses the bundled Draft 2020-12 validator on memory-only candidate bytes.
// A missing/broken validator fails closed. No shell or network access is used.
void ValidateConfig(const std::string& filename, const std::string& candidate);
} // namespace utility
