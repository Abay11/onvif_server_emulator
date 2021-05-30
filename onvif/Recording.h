#pragma once

#include <boost/date_time/posix_time/posix_time.hpp>

#include <string>

namespace osrv
{
	class Recording
	{
	public:
		Recording(const std::string& name,
			const std::string& from = {},
			const std::string& until = {});
			

		const std::string& Name() const
		{
			return name_;
		}

		boost::posix_time::ptime DateFrom() const;
		boost::posix_time::ptime DateUntil() const;


	private:
		const std::string name_;
			const std::string from_;
			const std::string until_;
			boost::posix_time::ptime fixed_from_;
			boost::posix_time::ptime fixed_until_;

	};


	class RecordingsMgr
	{
	public:
		RecordingsMgr(const std::string& file)
			: file_(file)
		{}

		//Recording Recording(const std::string& name);
		std::vector<Recording> Recordings();


	private:
		const std::string& file_;
	};
}