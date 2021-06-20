#include <boost/test/unit_test.hpp>

#include "../onvif/Recording.h"

#include <boost/property_tree/json_parser.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>

using namespace std::string_literals;

namespace bpt = boost::posix_time;
namespace pt = boost::property_tree;

const std::string config_file = "../../server_configs/Recording Search.config";

BOOST_AUTO_TEST_CASE(ConfigPathTest)
{
	std::ifstream ifile(config_file);
	BOOST_ASSERT(true == ifile.is_open());
}

BOOST_AUTO_TEST_CASE(RecordingTest0)
{
	// as we not specified recording data FROM and UNTIL
	// it's expected FROM will will be 24 hourc ago and UNTIL till now
	osrv::Recording recording("RecordingToken0", "VideoTrack0");

	// just do rough comparison
	auto now = boost::posix_time::second_clock::universal_time();
	auto day_ago = now - boost::posix_time::hours(24);
	auto actual_from = recording.DateFrom();
	auto actual_until = recording.DateUntil();
	BOOST_TEST(true == (now <= actual_until && now > actual_from));
	BOOST_TEST(true == (day_ago <= actual_from && day_ago < actual_until));
}

BOOST_AUTO_TEST_CASE(RecordingTest1)
{
	auto expected = bpt::from_iso_string("20130528T075623");
	auto actual = osrv::Recording("RecordingToken0", "VideoTrack0", "20130528T075623").DateFrom();
	BOOST_TEST(actual == expected);
}

BOOST_AUTO_TEST_CASE(RecordingTest2)
{
	std::string data = "20130528T075655";
	auto expected = bpt::from_iso_string(data);
	auto actual = osrv::Recording("RecordingToken0", "VideoTrack0", "20130528T075623", data).DateUntil();
	BOOST_TEST(actual == expected);
}

BOOST_AUTO_TEST_CASE(RecordingReaderFromConfig0)
{
	using namespace osrv;

	auto recordings = RecordingsReaderFromConfig(config_file).Recordings();
	BOOST_ASSERT(false == recordings.empty());

	BOOST_TEST(true == ("Recording_0"s == recordings[0]->Token()));
	BOOST_TEST(true == ("VideoTrack_0"s == recordings[0]->VideoTrackToken()));
	BOOST_TEST(true
		== (boost::posix_time::from_iso_string("20210523T000000") == recordings[0]->DateFrom()));
	BOOST_TEST(true
		== (boost::posix_time::from_iso_string("20210523T060000") == recordings[0]->DateUntil()));
}


BOOST_AUTO_TEST_CASE(RecordingManager0)
{
	std::string json_config_example = R"(
		{
			"Recordings":
			[
				{
					"Token":"Recording_0",
					"DataFrom":"20210523T000000",
					"DataUntil":"20210523T060000"
				}
			]
		})";

	std::istringstream is(json_config_example);
	pt::ptree config;
	pt::json_parser::read_json(is, config);

	auto recordings_it = config.find("Recordings");
	BOOST_ASSERT(recordings_it != config.not_found());

	const std::string expected_name = "Recording_0";
	auto rec_it = recordings_it->second.begin();
	BOOST_TEST(rec_it->second.get<std::string>("Token") == expected_name);
}

BOOST_AUTO_TEST_CASE(RecordingManager1)
{
	const std::string config_file = "../../server_configs/Recording Search.config";
	auto recordings = osrv::RecordingsMgr(config_file).Recordings();

	const std::string expected_name = "Recording_0";
	const bpt::ptime expected_date_from = bpt::from_iso_string("20210523T000000");
	const bpt::ptime expected_date_until = bpt::from_iso_string("20210523T060000");

	BOOST_ASSERT(recordings.size() == 1);
	BOOST_TEST(recordings.front()->Token() == expected_name);
	BOOST_TEST(recordings.front()->DateFrom() == expected_date_from);
	BOOST_TEST(recordings.front()->DateUntil() == expected_date_until);
}

BOOST_AUTO_TEST_CASE(EventsSearchSession0)
{
	using namespace osrv;

	std::string from{"20210523T000000"};
	std::string until{"20210523T060000"};

	auto  r = std::make_shared<osrv::Recording>("rtoken0", "vtoken", from, until);
	auto re = r->RecordingEvents();
	EventsSearchSessionFactory factory("SimpleEventsSearchSession");

	std::string searchStartPoint = "20210523T000000";
	std::string searchEndPoint = "20210523T060000";

	auto searchSession = re->NewSearchSession(searchStartPoint, searchEndPoint, factory);
	auto searchToken = searchSession->SearchToken();
	
	std::string expected = "SearchToken0";
	BOOST_TEST(searchToken == expected);

	auto resultEvents = searchSession->Events();
	
	BOOST_TEST(2 == resultEvents.size());
	BOOST_TEST(true == resultEvents[0].isDataPresent);
	BOOST_TEST(boost::posix_time::from_iso_string(from) == resultEvents[0].utcTime);
	
	BOOST_TEST(false == resultEvents[1].isDataPresent);
	BOOST_TEST(boost::posix_time::from_iso_string(until) == resultEvents[1].utcTime);
}

BOOST_AUTO_TEST_CASE(EventsSearchSession1)
{
	using namespace osrv;

	std::string from{"20210523T000000"};
	std::string until{"20210523T060000"};

	auto  r = std::make_shared<osrv::Recording>("rtoken0", "vtoken", from, until);
	auto re = r->RecordingEvents();
	EventsSearchSessionFactory factory("SimpleEventsSearchSession");

	std::string searchStartPoint = "20210523T030000";
	std::string searchEndPoint = "20210523T033000";

	auto searchSession = re->NewSearchSession(searchStartPoint, searchEndPoint, factory);
	auto searchToken = searchSession->SearchToken();
	
	std::string expected = "SearchToken0";
	BOOST_TEST(searchToken == expected);

	auto resultEvents = searchSession->Events();
	
	BOOST_TEST(2 == resultEvents.size());
	BOOST_TEST(true == resultEvents[0].isDataPresent);
	BOOST_TEST(boost::posix_time::from_iso_string(searchStartPoint) == resultEvents[0].utcTime);
	
	BOOST_TEST(false == resultEvents[1].isDataPresent);
	BOOST_TEST(boost::posix_time::from_iso_string(searchEndPoint) == resultEvents[1].utcTime);
}

BOOST_AUTO_TEST_CASE(EventsSearchSession2)
{
	using namespace osrv;

	std::string from{"20210523T000000"};
	std::string until{"20210523T060000"};

	auto  r = std::make_shared<osrv::Recording>("rtoken0", "vtoken", from, until);
	auto re = r->RecordingEvents();
	EventsSearchSessionFactory factory("SimpleEventsSearchSession");

	std::string searchStartPoint = "20210523T030000";
	std::string searchEndPoint = "20210523T033000";

	// NOTE: FOR SOME REASON VS COMPILATOR COMPLAINS ABOUT MEMORY LEAKS
	auto searchSession = re->NewSearchSession(searchStartPoint, searchEndPoint, factory);
	auto searchToken = searchSession->SearchToken();

	auto ss = re->SearchSession(searchToken);
	
	BOOST_ASSERT(!ss->Events().empty());
	BOOST_TEST(ss->SearchToken() == searchToken);
}
