#pragma once

#include <sstream>

#include <boost/date_time/posix_time/posix_time.hpp>

namespace utility
{
	namespace datetime
	{
		inline std::string posix_datetime_to_utc(boost::posix_time::ptime tm)
		{
			std::stringstream ss;

			namespace pt = boost::posix_time;
			
			//date format example: 2020-10-27T11:20:42Z
			//pt::time_facet* tf = new pt::time_facet("%Y-%m-%dT%H:M%:%S:%FZ"); 
			pt::time_facet* tf = new pt::time_facet("%Y-%m-%dT%H:%M:%SZ"); 
			ss.imbue(std::locale(ss.getloc(), tf));

			ss << tm;

			return ss.str();
		}
		
		inline std::string posix_time_to_utc(boost::posix_time::ptime tm)
		{
			std::stringstream ss;

			namespace pt = boost::posix_time;
			
			pt::time_facet* tf = new pt::time_facet("%H:%M:%SZ"); 
			ss.imbue(std::locale(ss.getloc(), tf));

			ss << tm;

			return ss.str();
		}

		inline std::string system_utc_datetime()
		{
			namespace pt = boost::posix_time;

			return posix_datetime_to_utc(pt::microsec_clock::universal_time());
		}

		inline std::string system_utc_time()
		{
			namespace pt = boost::posix_time;

			return posix_time_to_utc(pt::microsec_clock::universal_time());

		}
	}
}