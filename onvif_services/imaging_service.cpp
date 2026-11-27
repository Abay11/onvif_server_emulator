#include "imaging_service.h"

#include "../Logger.h"
#include "../Server.h"

#include "../onvif/OnvifRequest.h"
#include "../utility/ChannelTopology.h"
#include "../utility/MediaProfilesManager.h"
#include "../utility/MetadataConfiguration.h"

#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"
#include "../utility/XmlParser.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace pt = boost::property_tree;

namespace osrv
{

namespace imaging
{
// a list of implemented methods
const std::string GetImagingSettings = "GetImagingSettings";
const std::string GetMoveOptions = "GetMoveOptions";
const std::string GetOptions = "GetOptions";
const std::string Move = "Move";
const std::string SetImagingSettings = "SetImagingSettings";
const std::string Stop = "Stop";

const std::string CONFIGS_FILE = "imaging.config";

void do_handle_request(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);

struct ImagingRequest : OnvifRequestBase
{
	ImagingRequest(const std::string& operation, auth::SECURITY_LEVELS access,
								 const std::map<std::string, std::string>& xs,
								 const std::shared_ptr<pt::ptree>& configs, const utility::media::MediaProfilesManager& manager)
		: OnvifRequestBase(operation, access, xs, configs), manager_(manager) {}

	void ValidateSource(HttpServer::Request& request) const
	{
		if (const auto* topology = manager_.Topology())
		{
			pt::ptree xml;
			pt::read_xml(request.content, xml);
			const auto token = exns::find_hierarchy("Envelope.Body." + name() + ".VideoSourceToken", xml);
			if (topology->Owner("VideoSources", token).empty())
				throw metadata::Fault("NoSource", "The requested video source does not exist");
		}
	}

	const utility::media::MediaProfilesManager& manager_;
};

struct GetImagingSettingsHandler : public ImagingRequest
{
	GetImagingSettingsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
										const utility::media::MediaProfilesManager& manager)
			: ImagingRequest(GetImagingSettings, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs, manager)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		ValidateSource(*request);
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		envelope_tree.add("s:Body.timg:GetImagingSettingsResponse.timg:ImagingSettings", "");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetMoveOptionsHandler : public ImagingRequest
{
	GetMoveOptionsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
										const utility::media::MediaProfilesManager& manager)
			: ImagingRequest(GetMoveOptions, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs, manager)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		ValidateSource(*request);
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		envelope_tree.add("s:Body.timg:GetMoveOptionsResponse.timg:MoveOptions.tt:Continuous.tt:Speed.tt:Min", 1.00);
		envelope_tree.add("s:Body.timg:GetMoveOptionsResponse.timg:MoveOptions.tt:Continuous.tt:Speed.tt:Max", 5.00);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetOptionsHandler : public ImagingRequest
{
	GetOptionsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
										const utility::media::MediaProfilesManager& manager)
			: ImagingRequest(GetOptions, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs, manager)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		ValidateSource(*request);
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		envelope_tree.add("s:Body.timg:GetOptionsResponse.timg:ImagingOptions", "");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct MoveHandler : public ImagingRequest
{
	MoveHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
										const utility::media::MediaProfilesManager& manager)
			: ImagingRequest(Move, auth::SECURITY_LEVELS::ACTUATE, xs, configs, manager)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		ValidateSource(*request);
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		envelope_tree.add("s:Body.timg:MoveResponse", "");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct SetImagingSettingsHandler : public ImagingRequest
{
	SetImagingSettingsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
										const utility::media::MediaProfilesManager& manager)
			: ImagingRequest(SetImagingSettings, auth::SECURITY_LEVELS::ACTUATE, xs, configs, manager)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		ValidateSource(*request);
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		envelope_tree.add("s:Body.timg:SetImagingSettingsResponse", "");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct StopHandler : public ImagingRequest
{
	StopHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
										const utility::media::MediaProfilesManager& manager)
			: ImagingRequest(Stop, auth::SECURITY_LEVELS::ACTUATE, xs, configs, manager)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		ValidateSource(*request);
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		envelope_tree.add("s:Body.timg:StopResponse", "");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};
} // namespace imaging

ImagingService::ImagingService(const std::string& service_uri, const std::string& service_name,
															 std::shared_ptr<IOnvifServer> srv)
		: IOnvifService(service_uri, service_name, srv)
{
	requestHandlers_.push_back(std::make_shared<imaging::GetImagingSettingsHandler>(xml_namespaces_, configs_ptree_, *srv->MediaProfilesManager()));
	requestHandlers_.push_back(std::make_shared<imaging::GetMoveOptionsHandler>(xml_namespaces_, configs_ptree_, *srv->MediaProfilesManager()));
	requestHandlers_.push_back(std::make_shared<imaging::GetOptionsHandler>(xml_namespaces_, configs_ptree_, *srv->MediaProfilesManager()));
	requestHandlers_.push_back(std::make_shared<imaging::MoveHandler>(xml_namespaces_, configs_ptree_, *srv->MediaProfilesManager()));
	requestHandlers_.push_back(std::make_shared<imaging::SetImagingSettingsHandler>(xml_namespaces_, configs_ptree_, *srv->MediaProfilesManager()));
	requestHandlers_.push_back(std::make_shared<imaging::StopHandler>(xml_namespaces_, configs_ptree_, *srv->MediaProfilesManager()));
}

} // namespace osrv