#include "AdminUi.h"
#include "../Server.h"
#include "../Simple-Web-Server/server_http.hpp"
#include "../include/onvif_services/service_configs.h"
#include "../onvif_services/IOnvifService.h"
#include "../utility/AnalyticsRules.h"
#include "../utility/ApplicationReboot.h"
#include "../utility/MediaProfilesManager.h"
#include "AdminAssets.h"
#include "DetectorUi.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <unistd.h>

namespace osrv
{
namespace
{
using Tree = boost::property_tree::ptree;
using Status = SimpleWeb::StatusCode;
using Response = std::shared_ptr<HttpServer::Response>;
using Request = std::shared_ptr<HttpServer::Request>;

SimpleWeb::CaseInsensitiveMultimap Headers(const std::string& content_type)
{
	return {{"Content-Type", content_type},
					{"Cache-Control", "no-store"},
					{"X-Content-Type-Options", "nosniff"},
					{"X-Frame-Options", "DENY"},
					{"Referrer-Policy", "no-referrer"},
					{"Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; "
																			"img-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'"}};
}

void Json(const Response& response, Status status, const std::string& body)
{
	response->close_connection_after_response = true;
	auto headers = Headers("application/json; charset=utf-8");
	headers.emplace("Content-Length", std::to_string(body.size()));
	headers.emplace("Connection", "close");
	response->write(status, body, headers);
}

std::string Encode(const Tree& tree)
{
	std::ostringstream json;
	boost::property_tree::write_json(json, tree, false);
	return json.str();
}

std::string EncodeArray(const Tree& tree)
{
	std::string result = "[";
	for (const auto& [_, item] : tree)
	{
		if (result.size() > 1)
			result += ',';
		result += Encode(item);
	}
	return result + "]";
}

std::string Authentication(AUTH_SCHEME scheme)
{
	switch (scheme)
	{
	case AUTH_SCHEME::DIGEST:
		return "digest";
	case AUTH_SCHEME::WSS:
		return "ws-security";
	case AUTH_SCHEME::DIGEST_WSS:
		return "digest/ws-security";
	default:
		return "none";
	}
}

// Explicit field selection keeps account credentials and arbitrary config content
// out of this unauthenticated API. Files are sampled at registration, never on GET.
Tree Settings(IOnvifServer& server)
{
	const auto cfg = server.GetServerConfigs();
	const auto common = static_cast<std::shared_ptr<Tree>>(ServiceConfigs("common", server.ConfigsPath()));
	const auto discovery = static_cast<std::shared_ptr<Tree>>(ServiceConfigs("discovery", server.ConfigsPath()));
	const auto events = static_cast<std::shared_ptr<Tree>>(ServiceConfigs("event", server.ConfigsPath()));
	Tree settings;
	settings.put("application.configuration_directory", server.ConfigsPath());
	settings.put("application.logging_level", server.Logger()->GetLogLevel());
	settings.put("application.logging_output", common->get<std::string>("loggingOutput"));
	settings.put("application.onvif_authentication", Authentication(cfg->auth_scheme_));
	settings.put("application.web_authentication", "none");
	settings.put("network.bind_ipv4", cfg->ipv4_address_);
	settings.put("network.http_port", cfg->http_port_);
	settings.put("network.rtsp_port", cfg->rtsp_port_);
	settings.put("network.advertised_ipv4", cfg->advertised_ipv4_);
	settings.put("network.http_endpoint", cfg->HttpAddress());
	settings.put("network.rtsp_endpoint", cfg->RtspAddress());
	settings.put("network.rtp_port_min", cfg->rtp_port_min_);
	settings.put("network.rtp_port_max", cfg->rtp_port_max_);
	settings.put("network.http_port_forwarding", cfg->enabled_http_port_forwarding);
	settings.put("network.rtsp_port_forwarding", cfg->enabled_rtsp_port_forwarding);
	settings.put("discovery.enabled", discovery->get<bool>("enabled", true));
	std::string interface = discovery->get<std::string>("interface_ipv4", "");
	if (const char* value = std::getenv("ONVIF_DISCOVERY_INTERFACE_IPV4"))
		interface = value;
	settings.put("discovery.interface_ipv4", interface.empty() ? "System default" : interface);
	settings.put("discovery.multicast_endpoint", "239.255.255.250:3702");
	settings.put("events.pullpoint_endpoint",
							 cfg->PullPointAddress(events->get<unsigned short>("PullPoint.Port", 5550),
																		 events->get<bool>("PullPoint.UseHttpServerPort", true)));
	settings.put("events.pullpoint_timeout_seconds", events->get<unsigned>("PullPoint.Timeout", 60));
	settings.put("media.metadata_enabled", cfg->rtsp_metadata_enabled_);
	settings.put("media.file_streaming_enabled", !cfg->rtsp_streaming_file_.empty());
	settings.put("simulation.network_delay_ms", cfg->network_delay_simulation_);
	settings.put("simulation.multichannel_enabled", cfg->multichannel_enabled_);
	settings.put("simulation.channel_count", static_cast<unsigned>(cfg->channels_count_));
	const auto device = server.DeviceService()->Configs();
	for (const auto* key : {"Manufacturer", "Model", "FirmwareVersion", "SerialNumber", "HardwareId"})
		settings.put(std::string("device.") + key,
								 device->get<std::string>(std::string("GetDeviceInformation.") + key, ""));
	Tree services;
	for (const auto& [_, service] : device->get_child("GetServices"))
	{
		Tree entry;
		entry.put("namespace", service.get<std::string>("namespace"));
		entry.put("endpoint", cfg->HttpAddress() + service.get<std::string>("XAddr"));
		entry.put("advertised", service.get<bool>("Enabled", true));
		services.push_back({"", entry});
	}
	settings.put_child("services", services);
	return settings;
}

Tree Profiles(const Tree& profiles)
{
	Tree result;
	if (auto list = profiles.get_child_optional("MediaProfiles"))
		for (const auto& [_, profile] : *list)
		{
			Tree item;
			for (const auto* key : {"token", "Name", "fixed", "VideoSource", "VideoEncoder", "AudioSource", "AudioEncoder",
															"Metadata", "Analytics", "PTZ"})
				item.put(key, profile.get<std::string>(key, ""));
			result.push_back({"", item});
		}
	return result;
}

std::string JournalPage(const SystemJournal& journal, const Request& request)
{
	if (request->query_string.size() > 2048)
		throw std::invalid_argument("Query is too long");
	std::uint64_t after = 0, limit = 100;
	SystemJournal::Filter filter;
	auto number = [](const std::string& text) {
		std::uint64_t value = 0;
		auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
		if (error != std::errc{} || end != text.data() + text.size() || text.empty())
			throw std::invalid_argument("Invalid journal cursor or limit");
		return value;
	};
	const auto query = request->parse_query_string();
	for (const auto& [key, value] : query)
	{
		if (query.count(key) != 1)
			throw std::invalid_argument("Duplicate parameter");
		if (key == "after")
			after = number(value);
		else if (key == "limit")
			limit = number(value);
		else if (key == "category" && (value.empty() || value == "system" || value == "client" || value == "detector"))
			filter.category = value;
		else if (key == "level" && (value.empty() || value == "info" || value == "warn" || value == "error"))
			filter.level = value;
		else if (key == "q" && value.size() <= 256)
			filter.text = value;
		else
			throw std::invalid_argument("Invalid journal filter");
	}
	if (!limit || limit > 200)
		throw std::invalid_argument("Invalid journal limit");
	const auto batch = journal.Read(after, static_cast<std::size_t>(limit), filter);
	std::string result = "{\"instance_id\":\"" + journal.Instance() + "\",\"cursor\":" + std::to_string(batch.cursor) +
											 ",\"dropped\":" + std::to_string(batch.dropped) +
											 ",\"has_more\":" + (batch.more ? "true" : "false") +
											 ",\"reset\":" + (batch.reset ? "true" : "false") + ",\"entries\":[";
	bool first = true;
	for (const auto& entry : batch.entries)
	{
		Tree fields;
		fields.put("time", entry.time);
		fields.put("level", entry.level);
		fields.put("category", entry.category);
		fields.put("source", entry.source);
		fields.put("message", entry.message);
		if (!first)
			result += ',';
		first = false;
		result += "{\"id\":" + std::to_string(entry.id) + "," + Encode(fields).substr(1);
	}
	return result + "]}";
}

bool SameOrigin(const Request& request, const std::string& action = "reboot")
{
	// A custom header prevents cross-site HTML forms from triggering a reboot.
	const auto marker = request->header.find("X-ONVIF-Admin");
	if (marker == request->header.end() || marker->second != action)
		return false;
	const auto site = request->header.find("Sec-Fetch-Site");
	if (site != request->header.end() && site->second != "same-origin" && site->second != "none")
		return false;
	const auto origin = request->header.find("Origin");
	if (origin == request->header.end())
		return true;
	const auto host = request->header.find("Host");
	return host != request->header.end() &&
				 (origin->second == "http://" + host->second || origin->second == "https://" + host->second);
}
} // namespace

void RegisterAdminUi(IOnvifServer& server)
{
	const auto http = server.GetHttpServer();
	const auto cfg = server.GetServerConfigs();
	const auto logger = server.Logger();
	const auto settings = Settings(server);
	const auto profiles = server.MediaProfilesManager()->SharedConfigs();
	const auto started = std::chrono::steady_clock::now();
	const auto instance = boost::uuids::to_string(boost::uuids::random_generator()());
	const auto status_body = [settings, profiles, cfg, started, instance] {
		const auto& snapshot = settings;
		// Profile mutations and this handler execute on the same HTTP event loop.
		const auto profile_list = Profiles(*profiles);
		const auto uptime =
				std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
		return "{\"status\":\"running\",\"instance_id\":\"" + instance + "\",\"pid\":" + std::to_string(getpid()) +
					 ",\"uptime_seconds\":" + std::to_string(uptime) +
					 ",\"reboot_available\":" + (cfg->reboot ? "true" : "false") +
					 ",\"reboot_pending\":" + (cfg->reboot_pending->load() ? "true" : "false") +
					 ",\"profiles\":" + EncodeArray(profile_list) + ",\"settings\":" + Encode(snapshot) + "}";
	};
	http->resource["^/admin$"]["GET"] = [](Response response, Request) {
		auto headers = Headers("text/plain; charset=utf-8");
		headers.emplace("Location", "/admin/");
		response->write(Status::redirection_found, "", headers);
	};
	const auto asset = [http, cfg](const std::string& path, std::string_view contents, const std::string& type) {
		http->resource[path]["GET"] = [contents, type, cfg, page = path == "^/admin/$"](Response response,
																																										Request request) {
			if (page)
				cfg->journal->Client("WebUI", request->remote_endpoint().address().to_string(), "unauthenticated");
			response->write(Status::success_ok, std::string(contents), Headers(type));
		};
	};
	asset("^/admin/$", admin_assets::Html, "text/html; charset=utf-8");
	asset(R"(^/admin/app\.css$)", admin_assets::Css, "text/css; charset=utf-8");
	asset(R"(^/admin/app\.js$)", admin_assets::Js, "text/javascript; charset=utf-8");
	asset(R"(^/admin/detectors\.js$)", admin_assets::Detectors, "text/javascript; charset=utf-8");
	auto detector_ui = std::make_shared<DetectorUi>(server);
	http->resource["^/admin/api/detectors$"]["GET"] = [detector_ui](Response response, Request) {
		try
		{
			Json(response, Status::success_ok, detector_ui->Read());
		}
		catch (const std::exception&)
		{
			Json(response, Status::server_error_internal_server_error, "{\"error\":\"Detector settings unavailable\"}");
		}
	};
	http->resource["^/admin/api/detectors$"]["POST"] = [detector_ui](Response response, Request request) {
		if (!SameOrigin(request, "detectors"))
		{
			Json(response, Status::client_error_forbidden, "{\"error\":\"Same-origin admin request required\"}");
			return;
		}
		auto type = request->header.find("Content-Type");
		if (type == request->header.end() || type->second != "application/json")
		{
			Json(response, Status::client_error_unsupported_media_type, "{\"error\":\"Expected application/json\"}");
			return;
		}
		try
		{
			const auto body = request->content.string();
			if (body.size() > 32768)
				throw std::invalid_argument("Request too large");
			Tree command;
			std::istringstream input(body);
			boost::property_tree::read_json(input, command);
			detector_ui->Change(command);
			Json(response, Status::success_ok, "{\"status\":\"updated\"}");
		}
		catch (const utility::RuleFault& e)
		{
			Tree error;
			error.put("error", e.detail);
			Json(response, e.receiver ? Status::client_error_conflict : Status::client_error_bad_request, Encode(error));
		}
		catch (const std::exception&)
		{
			Json(response, Status::client_error_bad_request, "{\"error\":\"Invalid detector settings\"}");
		}
	};
	http->resource["^/admin/api/journal$"]["GET"] = [journal = cfg->journal](Response response, Request request) {
		try
		{
			Json(response, Status::success_ok, JournalPage(*journal, request));
		}
		catch (const std::invalid_argument&)
		{
			Json(response, Status::client_error_bad_request, "{\"error\":\"Invalid journal query\"}");
		}
	};
	http->resource["^/admin/api/status$"]["GET"] = [status_body, logger](Response response, Request) {
		try
		{
			Json(response, Status::success_ok, status_body());
		}
		catch (const std::exception& e)
		{
			logger->Error(std::string("Admin status failed: ") + e.what());
			Json(response, Status::server_error_internal_server_error, "{\"error\":\"Status is unavailable\"}");
		}
	};
	http->resource["^/admin/api/reboot$"]["POST"] = [cfg, logger](Response response, Request request) {
		if (!SameOrigin(request))
		{
			Json(response, Status::client_error_forbidden, "{\"error\":\"A same-origin admin request is required\"}");
			return;
		}
		const auto content_type = request->header.find("Content-Type");
		if (content_type == request->header.end() || content_type->second != "application/json")
		{
			Json(response, Status::client_error_unsupported_media_type, "{\"error\":\"Expected application/json\"}");
			return;
		}
		try
		{
			const auto body = request->content.string();
			Tree confirmation;
			std::istringstream input(body);
			if (body.size() > 1024)
				throw std::runtime_error("Confirmation too large");
			boost::property_tree::read_json(input, confirmation);
			if (confirmation.size() != 1 || !confirmation.get<bool>("confirm", false))
				throw std::runtime_error("Confirmation required");
		}
		catch (const std::exception&)
		{
			Json(response, Status::client_error_bad_request, "{\"error\":\"Expected {\\\"confirm\\\":true}\"}");
			return;
		}
		if (!cfg->reboot)
		{
			Json(response, Status::server_error_service_unavailable, "{\"error\":\"Application reboot is unavailable\"}");
			return;
		}
		Json(response, Status::success_accepted, "{\"status\":\"rebooting\"}");
		RebootAfterResponse(response, cfg, logger);
	};
	for (const auto& [path, method] : {std::pair{"^/admin/api/reboot$", "GET"}, {"^/admin/api/status$", "POST"}})
		http->resource[path][method] = [method](Response response, Request) {
			auto headers = Headers("application/json; charset=utf-8");
			headers.emplace("Allow", std::string(method) == "GET" ? "POST" : "GET");
			response->write(Status::client_error_method_not_allowed, "{\"error\":\"Method not allowed\"}", headers);
		};
}
} // namespace osrv
