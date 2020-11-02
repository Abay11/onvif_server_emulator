#pragma once

#include <sstream>

#include <boost/date_time/posix_time/posix_time.hpp>

namespace utility
{
	namespace datetime
	{
		inline std::string posix_to_utc(boost::posix_time::ptime tm)
		{
			std::stringstream ss;

			namespace pt = boost::posix_time;
			
			//date format example: 2020-10-27T11:20:42Z
			//pt::time_facet* tf = new pt::time_facet("%Y-%m-%dT%H:M%:%S:%FZ"); 
			pt::time_facet* tf = new pt::time_facet("%Y-%m-%dT%H:%M:%S:%fZ"); 
			ss.imbue(std::locale(ss.getloc(), tf));

			ss << tm;

			return ss.str();
		}

		inline std::string system_utc_datetime()
		{
			namespace pt = boost::posix_time;

			return posix_to_utc(pt::microsec_clock::universal_time());
		}
	}
}