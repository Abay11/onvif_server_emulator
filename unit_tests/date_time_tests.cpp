#include <boost/test/unit_test.hpp>

#include "../utility/DateTime.hpp"

BOOST_AUTO_TEST_CASE(system_utc_datetime_func)
{
	using namespace utility::datetime;

	namespace pt = boost::posix_time;
	auto test_date = "2020-10-27 11:20:42";
	auto actual = posix_datetime_to_utc(pt::time_from_string(test_date));
	std::string expected = "2020-10-27T11:20:42.000000Z";
	BOOST_TEST(expected == actual);

	auto actual2 = posix_time_to_utc(pt::time_from_string(test_date));
	std::string expected2 = "11:20:42.000000Z";
	BOOST_TEST(expected2 == actual2);
}