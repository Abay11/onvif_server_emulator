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

std::shared_ptr<osrv::RecordingEvents> osrv::Recording::RecordingEvents()
{
	return std::make_shared<osrv::RecordingEvents>(shared_from_this());
}


std::vector<std::shared_ptr<osrv::Recording>> osrv::RecordingsMgr::Recordings()
{
	return RecordingsReaderFromConfig(file_).Recordings();
}

std::shared_ptr<osrv::IEventsSearchSession> osrv::RecordingEvents::NewSearchSession(boost::posix_time::ptime from, boost::posix_time::ptime until, EventsSearchSessionFactory& factory)
{
	std::shared_ptr<osrv::IEventsSearchSession> ss = factory.NewSession(SearchToken(), from, until, relatedRecording_);
	searchSessions_.insert(ss);
	return ss;
}

std::shared_ptr<osrv::IEventsSearchSession> osrv::RecordingEvents::NewSearchSession(std::string stringUTCFrom, std::string stringUTCUntil, EventsSearchSessionFactory& factory)
{
	return NewSearchSession(bptime::from_iso_string(stringUTCFrom), bptime::from_iso_string(stringUTCUntil), factory);
}

std::shared_ptr<osrv::IEventsSearchSession> osrv::RecordingEvents::NewSearchSession(osrv::EventsSearchSessionFactory& factory)
{
	return NewSearchSession(relatedRecording_->DateFrom(), relatedRecording_->DateUntil(), factory);
}

std::shared_ptr<osrv::IEventsSearchSession> osrv::RecordingEvents::SearchSession(std::string searchToken)
{
	if (auto ss = std::find_if(searchSessions_.begin(), searchSessions_.end(),
		[searchToken](auto ss) { return ss->SearchToken() == searchToken; }); ss != searchSessions_.end())
		return *ss;

	throw std::runtime_error("Not found search session with token: " + searchToken);
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

std::vector<std::shared_ptr<osrv::Recording>> osrv::RecordingsReaderFromConfig::Recordings()
{
	pt::ptree configTree;
	pt::json_parser::read_json(configFile_, configTree);

	std::vector<std::shared_ptr<osrv::Recording>> recordings;

	for (auto r : configTree.get_child("Recordings"))
	{
		recordings.push_back(std::make_shared<Recording>
			(
			r.second.get<std::string>("Token"),
			r.second.get<std::string>("VideoTrack"),
			// TODO: make optional next two with default value
			r.second.get<std::string>("DateFrom"),
			r.second.get<std::string>("DateUntil")
			)
		);
	}

	return recordings;
}
