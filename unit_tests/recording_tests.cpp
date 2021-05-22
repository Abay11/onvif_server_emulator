#include <boost/test/unit_test.hpp>

#include "../onvif/Recording.h"

namespace bpt = boost::posix_time;

BOOST_AUTO_TEST_CASE(RecordingTest0)
{
	//BOOST_TEST("" == e.what());

	// as we not specified recording data FROM and UNTIL
	// it's expected FROM will will be 24 hourc ago and UNTIL till now
	osrv::Recording recording("RecordingToken0");

	// just do rough comparison
	auto now = boost::posix_time::second_clock::universal_time();
	auto day_ago = now - boost::posix_time::hours(24);
	auto actual_from = recording.DataFrom();
	auto actual_until = recording.DataUntil();
	BOOST_TEST(true == (now <= actual_until && now > actual_from));
	BOOST_TEST(true == (day_ago <= actual_from && day_ago < actual_until));
}

BOOST_AUTO_TEST_CASE(RecordingTest1)
{
	auto expected = bpt::from_iso_string("20130528T075623");
	auto actual = osrv::Recording("RecordingToken0", "20130528T075623").DataFrom();
	BOOST_TEST(actual == expected);
}

BOOST_AUTO_TEST_CASE(RecordingTest2)
{
	std::string data = "20130528T075655";
	auto expected = bpt::from_iso_string(data);
	auto actual = osrv::Recording("RecordingToken0", "20130528T075623", data).DataUntil();
	BOOST_TEST(actual == expected);
}