#include "Recording.h"

namespace bptime = boost::posix_time;

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

boost::posix_time::ptime osrv::Recording::DataFrom() const
{
	if (from_.empty())
		return boost::posix_time::second_clock::universal_time() - bptime::hours(24);

	return fixed_from_;
}

boost::posix_time::ptime osrv::Recording::DataUntil() const
{
	if (until_.empty())
		return boost::posix_time::second_clock::universal_time();

	return fixed_until_;
}
