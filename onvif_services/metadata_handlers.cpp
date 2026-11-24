#include "metadata_handlers.h"
#include "../Server.h"
#include "../include/IOnvifServer.h"
#include "../onvif/OnvifRequest.h"
#include "../utility/HttpHelper.h"
#include "../utility/MetadataConfiguration.h"
#include "../utility/SoapHelper.h"
#include <boost/property_tree/xml_parser.hpp>

namespace osrv
{
namespace
{
struct MetadataHandler : OnvifRequestBase
{
	std::shared_ptr<metadata::Control> control;
	bool media2;
	MetadataHandler(const std::string& operation, const std::map<std::string, std::string>& ns,
									const std::shared_ptr<pt::ptree>& config, std::shared_ptr<metadata::Control> control, bool media2)
			: OnvifRequestBase(operation,
												 operation.starts_with("Get") ? auth::SECURITY_LEVELS::READ_MEDIA
																											: auth::SECURITY_LEVELS::ACTUATE,
												 ns, config),
				control(std::move(control)), media2(media2)
	{
	}
	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto ns = ns_;
		ns["wsnt"] = "http://docs.oasis-open.org/wsn/b-2";
		ns["ter"] = "http://www.onvif.org/ver10/error";
		auto envelope = utility::soap::getEnvelopeTree(ns);
		bool fault = false;
		try
		{
			envelope.add_child(std::string("s:Body.") + (media2 ? "tr2:" : "trt:") + name() + "Response",
												 control->Execute(name(), request->content.string(), media2, HttpServer::IsSecure(request)));
		}
		catch (const metadata::Fault& error)
		{
			fault = true;
			envelope.put("s:Body.s:Fault.s:Code.s:Value", "s:Sender");
			envelope.put("s:Body.s:Fault.s:Code.s:Subcode.s:Value", "ter:InvalidArgVal");
			envelope.put("s:Body.s:Fault.s:Code.s:Subcode.s:Subcode.s:Value", "ter:" + error.detail);
			envelope.put("s:Body.s:Fault.s:Reason.s:Text", error.what());
			envelope.put("s:Body.s:Fault.s:Reason.s:Text.<xmlattr>.xml:lang", "en");
		}
		pt::ptree document;
		document.add_child("s:Envelope", envelope);
		std::ostringstream out;
		pt::write_xml(out, document);
		if (fault)
			utility::http::fillResponseWithHeaders(*response, out.str(), utility::http::ClientErrorDefaultWriter);
		else
			utility::http::fillResponseWithHeaders(*response, out.str());
	}
};
} // namespace
void AddMetadataHandlers(std::vector<std::shared_ptr<OnvifRequestBase>>& handlers,
												 const std::map<std::string, std::string>& namespaces, const std::shared_ptr<pt::ptree>& config,
												 std::shared_ptr<IOnvifServer> server, bool media2)
{
	if (!server->GetServerConfigs()->rtsp_metadata_enabled_)
		return;
	auto add = [&](const std::string& operation) {
		handlers.push_back(std::make_shared<MetadataHandler>(operation, namespaces, config, server->Metadata(), media2));
	};
	for (const auto& operation : {"GetMetadataConfigurations", "GetMetadataConfigurationOptions",
																"SetMetadataConfiguration", "SetSynchronizationPoint", "GetStreamUri"})
		add(operation);
	if (!media2)
		for (const auto& operation : {"GetMetadataConfiguration", "GetCompatibleMetadataConfigurations",
																	"AddMetadataConfiguration", "RemoveMetadataConfiguration"})
			add(operation);
}
} // namespace osrv
