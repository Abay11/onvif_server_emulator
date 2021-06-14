#include "Recording.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <fstream>

namespace bptime = boost::posix_time;
namespace pt = boost::property_tree;

osrv::Recording::Recording(const std::string& token, const std::string& videoToken, const std::string& from, const std::string& until)
	: token_(token), videoTrackToken_(videoToken), from_(from), until_(until)
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
std::unique_ptr<osrv::RecordingEvents> osrv::Recording::RecordingEvents()
{
	return std::make_unique<osrv::RecordingEvents>(shared_from_this());
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
			r.second.get<std::string>("VideoTrack"),
			r.second.get<std::string>("DateFrom"),
			r.second.get<std::string>("DateUntil"));
	}

	return recordings;
}

std::unique_ptr<osrv::IEventsSearchSession> osrv::RecordingEvents::SearchSession(boost::posix_time::ptime from, boost::posix_time::ptime until, EventsSearchSessionFactory& factory)
{
	return factory.NewSession(SearchToken(), from, until, relatedRecording_);
}

std::unique_ptr<osrv::IEventsSearchSession> osrv::RecordingEvents::SearchSession(std::string stringUTCFrom, std::string stringUTCUntil, EventsSearchSessionFactory& factory)
{
	return SearchSession(bptime::from_iso_string(stringUTCFrom), bptime::from_iso_string(stringUTCUntil), factory);
}

std::unique_ptr<osrv::IEventsSearchSession> osrv::RecordingEvents::SearchSession(EventsSearchSessionFactory& factory)
{
	return SearchSession(relatedRecording_->DateFrom(), relatedRecording_->DateUntil(), factory);
}

std::string osrv::RecordingEvents::SearchToken()
{
	return "SearchToken" + std::to_string(searchToken_++);
}



std::vector<osrv::RecordingEvent> osrv::SimpleEventsSearchSessionImpl::Events()
{
	const std::string TOPIC = "tns1:RecordingHistory/Track/State";
	std::vector<RecordingEvent> result;
	result.push_back({ relatedRecording_->Token(), relatedRecording_->VideoTrackToken(),
		TOPIC, searchStartPoint_, true });
	result.push_back({ relatedRecording_->Token(), relatedRecording_->VideoTrackToken(),
		TOPIC, searchEndPoint_, false });
	return result;
}
