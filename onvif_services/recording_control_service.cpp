#include "recording_control_service.h"
#include "../Server.h"
#include "../onvif/OnvifRequest.h"
#include "../onvif/RecordingArchive.h"
#include "../utility/HttpHelper.h"
#include "../utility/MediaProfilesManager.h"
#include "../utility/MetadataConfiguration.h"
#include "../utility/SoapHelper.h"
#include "../utility/XmlParser.h"

#include <boost/property_tree/xml_parser.hpp>

namespace osrv
{
namespace
{
using Entry = RecordingArchive::Entry;
using Tree = RecordingArchive::Tree;

Tree SourceReference(const std::string& profile)
{
	Tree source;
	source.put("<xmlattr>.Type", "http://www.onvif.org/ver10/schema/Profile");
	source.put("tt:Token", profile);
	return source;
}

Tree TrackConfiguration(const std::string& type)
{
	Tree result;
	result.put("tt:TrackType", type);
	result.put("tt:Description", "Synthetic " + type);
	return result;
}

Tree JobConfiguration(const Entry& entry)
{
	Tree result, source;
	result.put("tt:RecordingToken", entry.token);
	result.put("tt:Mode", entry.active ? "Active" : "Idle");
	result.put("tt:Priority", 0);
	source.put_child("tt:SourceToken", SourceReference(entry.source.profile));
	for (const auto& track : RecordingTracks(entry.source))
	{
		Tree mapping;
		mapping.put("tt:SourceTag", track);
		mapping.put("tt:Destination", track);
		source.add_child("tt:Tracks", mapping);
	}
	result.put_child("tt:Source", source);
	return result;
}

Tree JobState(const Entry& entry)
{
	Tree result, source, tracks;
	const auto state = entry.active ? "Active" : "Idle";
	result.put("tt:RecordingToken", entry.token);
	result.put("tt:State", state);
	source.put_child("tt:SourceToken", SourceReference(entry.source.profile));
	source.put("tt:State", state);
	for (const auto& track : RecordingTracks(entry.source))
	{
		Tree item;
		item.put("tt:SourceTag", track);
		item.put("tt:Destination", track);
		item.put("tt:State", state);
		tracks.add_child("tt:Track", item);
	}
	source.put_child("tt:Tracks", tracks);
	result.put_child("tt:Sources", source);
	return result;
}

Tree Configuration(const Entry& entry)
{
	Tree result;
	result.put_child("tt:Source", RecordingSourceXml(entry.source));
	result.put("tt:Content", "Synthetic recording");
	result.put("tt:MaximumRetentionTime", "PT0S");
	return result;
}

class Request : public OnvifRequestBase
{
public:
	Request(const std::string& operation, auth::SECURITY_LEVELS access, const std::map<std::string, std::string>& ns,
					const std::shared_ptr<Tree>& config, IOnvifServer& server)
			: OnvifRequestBase(operation, access, ns, config), server_(server)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		Tree document;
		pt::read_xml(request->content, document);
		const auto prefix = "Envelope.Body." + name() + ".";
		auto argument = [&](const char* field) { return exns::find_hierarchy(prefix + field, document); };
		auto& archive = *server_.GetServerConfigs()->archive;
		Tree result;
		if (name() == "GetServiceCapabilities")
		{
			result.put("trc:Capabilities.<xmlattr>.DynamicRecordings", false);
			result.put("trc:Capabilities.<xmlattr>.DynamicTracks", false);
			result.put("trc:Capabilities.<xmlattr>.Options", false);
			result.put("trc:Capabilities.<xmlattr>.MetadataRecording", true);
		}
		else if (name() == "GetRecordings" || name() == "GetRecordingJobs")
		{
			for (const auto& entry : archive.Snapshot())
			{
				Tree item;
				if (name() == "GetRecordings")
				{
					item.put("tt:RecordingToken", entry.token);
					item.put_child("tt:Configuration", Configuration(entry));
					Tree tracks;
					for (const auto& type : RecordingTracks(entry.source))
					{
						Tree track;
						track.put("tt:TrackToken", type);
						track.put_child("tt:Configuration", TrackConfiguration(type));
						tracks.add_child("tt:Track", track);
					}
					item.put_child("tt:Tracks", tracks);
					result.add_child("trc:RecordingItem", item);
				}
				else
				{
					item.put("tt:JobToken", entry.jobToken);
					item.put_child("tt:JobConfiguration", JobConfiguration(entry));
					result.add_child("trc:JobItem", item);
				}
			}
		}
		else if (name() == "GetRecordingConfiguration" || name() == "GetTrackConfiguration")
		{
			const auto entry = archive.Recording(argument("RecordingToken"));
			if (!entry)
				throw metadata::Fault("NoRecording", "The recording does not exist");
			if (name() == "GetRecordingConfiguration")
				result.put_child("trc:RecordingConfiguration", Configuration(*entry));
			else
			{
				const auto track = argument("TrackToken");
				const auto tracks = RecordingTracks(entry->source);
				if (std::ranges::find(tracks, track) == tracks.end())
					throw metadata::Fault("NoTrack", "The track does not exist");
				result.put_child("trc:TrackConfiguration", TrackConfiguration(track));
			}
		}
		else
		{
			const auto entry = archive.Job(argument("JobToken"));
			if (!entry)
				throw metadata::Fault("NoRecordingJob", "The recording job does not exist");
			if (name() == "GetRecordingJobState")
				result.put_child("trc:State", JobState(*entry));
			else if (name() == "GetRecordingJobConfiguration")
				result.put_child("trc:JobConfiguration", JobConfiguration(*entry));
			else
			{
				const auto mode = argument("Mode");
				if (mode != "Idle" && mode != "Active")
					throw metadata::Fault("BadMode", "The requested recording job mode is invalid");
				try
				{
					archive.SetJobMode(entry->jobToken, mode == "Active", [&](const auto& channel, const auto& profile) {
						return server_.RecordingProfileAvailable(channel, profile);
					});
				}
				catch (const std::invalid_argument&)
				{
					throw configuration_conflict();
				}
			}
		}
		auto envelope = utility::soap::getEnvelopeTree(ns_);
		envelope.add_child("s:Body.trc:" + name() + "Response", result);
		Tree root;
		root.put_child("s:Envelope", envelope);
		std::ostringstream xml;
		pt::write_xml(xml, root);
		utility::http::fillResponseWithHeaders(*response, xml.str());
	}

private:
	IOnvifServer& server_;
};
} // namespace

RecordingControlService::RecordingControlService(const std::string& uri, const std::string& name,
																								 std::shared_ptr<IOnvifServer> server)
		: IOnvifService(uri, name, server)
{
	for (const auto* operation : {"GetRecordings", "GetRecordingConfiguration", "GetTrackConfiguration",
																"GetRecordingJobs", "GetRecordingJobConfiguration", "GetRecordingJobState"})
		requestHandlers_.push_back(std::make_shared<Request>(operation, auth::SECURITY_LEVELS::READ_MEDIA, xml_namespaces_,
																												 configs_ptree_, *server));
	requestHandlers_.push_back(std::make_shared<Request>("GetServiceCapabilities", auth::SECURITY_LEVELS::PRE_AUTH,
																											 xml_namespaces_, configs_ptree_, *server));
	requestHandlers_.push_back(std::make_shared<Request>("SetRecordingJobMode", auth::SECURITY_LEVELS::ACTUATE,
																											 xml_namespaces_, configs_ptree_, *server));
}
} // namespace osrv
