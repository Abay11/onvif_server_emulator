#include "Recording.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <fstream>

namespace bptime = boost::posix_time;
namespace pt = boost::property_tree;

osrv::Recording::Recording(const std::string& name, const std::string& from, const std::string& until)
	: name_(name), from_(from), until_(until)
{
	if (!from.empty())
	{
		fixed_from_ = bptime::from_iso_string(from);
	}

	if (!until.empty())
	{
		fixed_until_ = bptime::from_iso_string(until);
	}
}

boost::posix_time::ptime osrv::Recording::DateFrom() const
{
	if (from_.empty())
		return boost::posix_time::second_clock::universal_time() - bptime::hours(24);

	return fixed_from_;
}

boost::posix_time::ptime osrv::Recording::DateUntil() const
{
	if (until_.empty())
		return boost::posix_time::second_clock::universal_time();

	return fixed_until_;
}

std::vector<osrv::Recording> osrv::RecordingsMgr::Recordings()
{
	std::ifstream ifile(file_);
	pt::ptree configs;
	pt::json_parser::read_json(ifile, configs);

	std::vector<Recording> recordings;
	for (const auto& r : configs.find("Recordings")->second)
	{
		recordings.emplace_back(r.second.get<std::string>("Name"),
			r.second.get<std::string>("DateFrom"),
			r.second.get<std::string>("DateUntil"));
	}

	return recordings;
}
