#include "recording_search_service.h"
#include "../Server.h"
#include "../onvif/OnvifRequest.h"
#include "../onvif/RecordingArchive.h"
#include "../utility/HttpHelper.h"
#include "../utility/MetadataConfiguration.h"
#include "../utility/MediaProfilesManager.h"
#include "../utility/SoapHelper.h"
#include "../utility/StreamTransport.h"
#include "../utility/XmlParser.h"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <algorithm>
#include <regex>
#include <set>

namespace osrv
{
namespace
{
using Tree = RecordingArchive::Tree;
using Entry = RecordingArchive::Entry;
using Time = RecordingArchive::Time;
using Steady = std::chrono::steady_clock;

Tree Information(const Entry& entry, Time now)
{
	Tree result;
	result.put("tt:RecordingToken", entry.token);
	result.put_child("tt:Source", RecordingSourceXml(entry.source));
	const auto begin = RecordingUtc(entry.intervals.front().start);
	const auto end = RecordingUtc(entry.intervals.back().end.value_or(now));
	result.put("tt:EarliestRecording", begin);
	result.put("tt:LatestRecording", end);
	result.put("tt:Content", "Synthetic recording");
	for (const auto& type : RecordingTracks(entry.source))
	{
		Tree track;
		track.put("tt:TrackToken", type);
		track.put("tt:TrackType", type);
		track.put("tt:Description", "Synthetic " + type);
		track.put("tt:DataFrom", begin);
		track.put("tt:DataTo", end);
		result.add_child("tt:Track", track);
	}
	result.put("tt:RecordingStatus", entry.active ? "Recording" : "Stopped");
	return result;
}

struct Session
{
	std::vector<Tree> results;
	std::size_t index = 0;
	Time at;
	Steady::time_point expires;
	Steady::duration lifetime;
};

class Search
{
public:
	explicit Search(RecordingArchive& archive) : archive_(archive) {}

	Tree Execute(const std::string& operation, const Tree& request)
	{
		const auto now = Steady::now();
		std::erase_if(sessions_, [&](const auto& item) { return item.second.expires <= now; });
		auto arg = [&](const char* key) {
			const auto value = exns::find(key, request);
			return value == request.not_found() ? std::string{} : value->second.data();
		};
		Tree result;
		if (operation == "GetServiceCapabilities")
		{
			result.put("tse:Capabilities.<xmlattr>.MetadataSearch", false);
			result.put("tse:Capabilities.<xmlattr>.GeneralStartEvents", false);
		}
		else if (operation == "GetRecordingSummary")
		{
			const auto entries = archive_.Snapshot();
			Time first{}, last{};
			for (const auto& entry : entries)
			{
				auto begin = entry.intervals.front().start;
				auto end = entry.intervals.back().end.value_or(archive_.Now());
				if (first == Time{} || begin < first)
					first = begin;
				last = std::max(last, end);
			}
			result.put("tse:Summary.tt:DataFrom", RecordingUtc(first));
			result.put("tse:Summary.tt:DataUntil", RecordingUtc(last));
			result.put("tse:Summary.tt:NumberRecordings", entries.size());
		}
		else if (operation == "GetRecordingInformation")
		{
			const auto entry = archive_.Recording(arg("RecordingToken"));
			if (!entry)
				throw metadata::Fault("InvalidToken", "The recording does not exist");
			result.put_child("tse:RecordingInformation", Information(*entry, archive_.Now()));
		}
		else if (operation == "FindRecordings")
		{
			const auto scope = exns::find("Scope", request);
			if (scope == request.not_found())
				throw well_formed();
			result.put("tse:SearchToken", Find(scope->second, arg("MaxMatches"), arg("KeepAliveTime"), now));
		}
		else
		{
			const auto found = sessions_.find(arg("SearchToken"));
			if (found == sessions_.end())
				throw metadata::Fault("InvalidToken", "The search session does not exist or has expired");
			auto& session = found->second;
			session.expires = now + session.lifetime;
			if (operation == "EndSearch")
			{
				result.put("tse:Endpoint", RecordingUtc(session.at));
				sessions_.erase(found);
			}
			else if (operation == "GetSearchState")
				result.put("tse:State", "Completed");
			else
			{
				const auto limit = Limit(arg("MaxResults"), session.results.size());
				const auto end = session.index + std::min(limit, session.results.size() - session.index);
				Tree list;
				list.put("tt:SearchState", "Completed");
				while (session.index < end)
					list.add_child("tt:RecordingInformation", session.results[session.index++]);
				result.put_child("tse:ResultList", list);
			}
		}
		return result;
	}

private:
	static std::size_t Limit(const std::string& text, std::size_t fallback)
	{
		if (text.empty())
			return fallback;
		try
		{
			std::size_t parsed = 0;
			auto limit = std::stoll(text, &parsed);
			if (limit < 0 || parsed != text.size())
				throw std::invalid_argument("limit");
			return static_cast<std::size_t>(limit);
		}
		catch (const std::logic_error&)
		{
			throw well_formed();
		}
	}

	std::string Find(const Tree& scope, const std::string& maxMatches, const std::string& keepAlive, Steady::time_point now)
	{
		static const std::regex duration(R"(^PT(?:(\d{1,9})H)?(?:(\d{1,9})M)?(?:(\d{1,9}(?:\.\d+)?)S)?$)");
		std::smatch parts;
		if (!std::regex_match(keepAlive, parts, duration) || keepAlive == "PT")
			throw well_formed();
		auto seconds = [&](int index) { return parts[index].matched ? std::stod(parts[index].str()) : 0.; };
		const double value = std::clamp(seconds(1) * 3600 + seconds(2) * 60 + seconds(3), 0.1, 3600.);
		Session session;
		session.at = archive_.Now();
		session.lifetime = std::chrono::duration_cast<Steady::duration>(std::chrono::duration<double>(value));
		session.expires = now + session.lifetime;

		std::set<std::string> recordings, sources;
		for (const auto& [field, item] : scope)
		{
			const auto name = exns::get_element_without_ns(field);
			if (name == "IncludedRecordings")
				recordings.insert(item.data());
			else if (name == "IncludedSources")
			{
				const auto token = exns::find("Token", item);
				if (token == item.not_found())
					throw metadata::Fault("InvalidSource", "The search source must have a token");
				sources.insert(token->second.data());
			}
			else if (name == "RecordingInformationFilter" && !item.data().empty())
				throw metadata::Fault("InvalidFilterFault", "Recording information XPath filters are unavailable");
		}

		const auto snapshot = archive_.Snapshot();
		const auto limit = Limit(maxMatches, snapshot.size());
		for (const auto& token : recordings)
			if (std::ranges::none_of(snapshot, [&](const auto& entry) { return entry.token == token; }))
				throw metadata::Fault("InvalidToken", "The recording does not exist");
		for (const auto& entry : snapshot)
		{
			if (session.results.size() == limit)
				break;
			if (!recordings.empty() && !recordings.contains(entry.token))
				continue;
			if (!sources.empty() && !sources.contains(entry.source.profile) && !sources.contains(entry.source.videoSource))
				continue;
			session.results.push_back(Information(entry, session.at));
		}
		if (sessions_.size() >= 256)
			throw std::runtime_error("Too many recording search sessions");
		const auto token = boost::uuids::to_string(boost::uuids::random_generator()());
		sessions_.emplace(token, std::move(session));
		return token;
	}

	RecordingArchive& archive_;
	std::map<std::string, Session> sessions_;
};

struct Handler : OnvifRequestBase
{
	Handler(const std::string& operation, const std::map<std::string, std::string>& ns,
					const std::shared_ptr<Tree>& config, const std::shared_ptr<Search>& search)
		: OnvifRequestBase(operation, operation == "GetServiceCapabilities" ? auth::SECURITY_LEVELS::PRE_AUTH :
																																	auth::SECURITY_LEVELS::READ_MEDIA, ns, config),
			search_(search)
	{
	}
	void operator()(std::shared_ptr<HttpServer::Response> response,
									std::shared_ptr<HttpServer::Request> request) override
	{
		Tree input;
		pt::read_xml(request->content, input);
		auto envelope = utility::soap::getEnvelopeTree(ns_);
		envelope.add_child("s:Body.tse:" + name() + "Response", search_->Execute(name(), StreamOperation(input, name())));
		Tree root;
		root.put_child("s:Envelope", envelope);
		std::ostringstream out;
		pt::write_xml(out, root);
		utility::http::fillResponseWithHeaders(*response, out.str());
	}

	std::shared_ptr<Search> search_;
};
} // namespace

RecordingSearchService::RecordingSearchService(const std::string& uri, const std::string& name,
																							 std::shared_ptr<IOnvifServer> server)
	: IOnvifService(uri, name, server)
{
	auto search = std::make_shared<Search>(*server->GetServerConfigs()->archive);
	for (const auto* operation : {"GetServiceCapabilities", "GetRecordingSummary", "GetRecordingInformation",
																 "FindRecordings", "GetRecordingSearchResults", "GetSearchState", "EndSearch"})
		requestHandlers_.push_back(std::make_shared<Handler>(operation, xml_namespaces_, configs_ptree_, search));
}
} // namespace osrv
