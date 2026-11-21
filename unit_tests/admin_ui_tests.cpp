#include "../Server.h"
#include "../Simple-Web-Server/client_http.hpp"
#include "../Simple-Web-Server/server_http.hpp"
#include "../include/LoggerFactories.h"
#include "../utility/AnalyticsRules.h"
#include "../utility/ConfigValidation.h"
#include "../utility/MediaProfilesManager.h"
#include "../web/AdminUi.h"
#include <atomic>
#include <boost/property_tree/json_parser.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <filesystem>
#include <future>
#include <thread>

namespace
{
namespace fs = std::filesystem;
using Tree = boost::property_tree::ptree;
using Headers = SimpleWeb::CaseInsensitiveMultimap;
struct TestServer : osrv::IOnvifServer
{
	TestServer(const std::string& path, std::function<void()> reboot)
			: IOnvifServer(path, std::shared_ptr<ILogger>(ConsoleLoggerFactory().GetLogger(ILogger::LVL_ERR)))
	{
		server_configs_ = osrv::read_server_configs(path + "/common.config");
		server_configs_->reboot = std::move(reboot);
	}
};
struct Endpoint
{
	std::string path =
			(fs::temp_directory_path() / ("onvif-admin-" + boost::uuids::to_string(boost::uuids::random_generator()())))
					.string();
	std::shared_ptr<TestServer> server;
	std::thread worker;
	std::unique_ptr<SimpleWeb::Client<SimpleWeb::HTTP>> client;
	std::atomic_uint reboots{0};
	Endpoint(bool available = true)
	{
		fs::copy("../../server_configs", path, fs::copy_options::recursive);
		Tree common;
		boost::property_tree::read_json(path + "/common.config", common);
		// Use a distinctive secret so accidental raw configuration exposure is visible.
		common.get_child("users").front().second.put("password", "admin-secret-must-not-leak");
		boost::property_tree::write_json(path + "/common.config", common);
		server = std::make_shared<TestServer>(path, available ? std::function<void()>([this] { ++reboots; })
																													: std::function<void()>{});
		osrv::RegisterAdminUi(*server);
		auto http = server->GetHttpServer();
		http->config.address = "127.0.0.1";
		http->config.port = 0;
		std::promise<unsigned short> port;
		auto ready = port.get_future();
		worker =
				std::thread([http, &port] { http->start([&port](unsigned short assigned) { port.set_value(assigned); }); });
		client = std::make_unique<SimpleWeb::Client<SimpleWeb::HTTP>>("127.0.0.1:" + std::to_string(ready.get()));
		client->config.timeout = 5;
	}
	~Endpoint()
	{
		server->GetHttpServer()->stop();
		if (worker.joinable())
			worker.join();
		fs::remove_all(path);
	}
	auto Request(const std::string& method, const std::string& target, const std::string& body = "", Headers headers = {})
	{
		headers.emplace("Connection", "close");
		return client->request(method, target, body, headers);
	}
	Tree Status()
	{
		auto response = Request("GET", "/admin/api/status");
		BOOST_REQUIRE_EQUAL(response->status_code, "200 OK");
		Tree tree;
		boost::property_tree::read_json(response->content, tree);
		return tree;
	}
};
const Headers RebootHeaders{{"Content-Type", "application/json"}, {"X-ONVIF-Admin", "reboot"}};
} // namespace

BOOST_AUTO_TEST_SUITE(AdminUi)
BOOST_AUTO_TEST_CASE(embedded_assets_and_status_are_served_without_exposing_credentials)
{
	Endpoint endpoint;
	auto redirect = endpoint.Request("GET", "/admin");
	BOOST_TEST(redirect->status_code == "302 Found");
	BOOST_TEST(redirect->header.find("Location")->second == "/admin/");
	for (const auto* asset : {"/admin/", "/admin/app.css", "/admin/app.js"})
	{
		auto response = endpoint.Request("GET", asset);
		BOOST_TEST(response->status_code == "200 OK");
		BOOST_TEST(response->content.size() > 100u);
		BOOST_TEST(response->header.find("Cache-Control")->second == "no-store");
		BOOST_TEST(response->header.find("X-Content-Type-Options")->second == "nosniff");
		BOOST_CHECK(response->header.find("Content-Security-Policy") != response->header.end());
	}
	auto response = endpoint.Request("GET", "/admin/api/status");
	const auto json = response->content.string();
	BOOST_TEST(json.find("admin-secret-must-not-leak") == std::string::npos);
	BOOST_TEST(json.find("password") == std::string::npos);
	BOOST_TEST(json.find("\"users\"") == std::string::npos);
	auto status = endpoint.Status();
	BOOST_TEST(status.get<std::string>("status") == "running");
	BOOST_TEST(status.get<unsigned>("pid") > 0u);
	BOOST_TEST(status.get<bool>("reboot_available"));
	BOOST_TEST(!status.get<bool>("reboot_pending"));
	BOOST_TEST(status.get<std::string>("settings.network.bind_ipv4") == "0.0.0.0");
	BOOST_TEST(status.get<std::string>("settings.network.http_endpoint") ==
						 endpoint.server->GetServerConfigs()->HttpAddress());
	BOOST_TEST(status.get_child("profiles").size() == 2u);
	const auto instance = status.get<std::string>("instance_id");
	BOOST_TEST(endpoint.Status().get<std::string>("instance_id") == instance);
	// Unapplied disk edits must not masquerade as active application settings.
	Tree common;
	boost::property_tree::read_json(endpoint.path + "/common.config", common);
	common.put("addresses.http_port", "19999");
	boost::property_tree::write_json(endpoint.path + "/common.config", common);
	BOOST_TEST(endpoint.Status().get<std::string>("settings.network.http_port") == "8080");
	// SOAP profile changes share this authoritative tree and must appear immediately.
	endpoint.server->MediaProfilesManager()
			->SharedConfigs()
			->get_child("MediaProfiles")
			.front()
			.second.put("Name", "Updated profile");
	BOOST_TEST(endpoint.Status().get_child("profiles").front().second.get<std::string>("Name") == "Updated profile");
}

BOOST_AUTO_TEST_CASE(reboot_requires_explicit_same_origin_post_and_sends_acknowledgement)
{
	Endpoint endpoint;
	BOOST_TEST(endpoint.Request("GET", "/admin/api/reboot")->status_code == "405 Method Not Allowed");
	BOOST_TEST(endpoint.Request("POST", "/admin/api/reboot", "{\"confirm\":true}")->status_code == "403 Forbidden");
	auto headers = RebootHeaders;
	headers.emplace("Origin", "https://other.example");
	BOOST_TEST(endpoint.Request("POST", "/admin/api/reboot", "{\"confirm\":true}", headers)->status_code ==
						 "403 Forbidden");
	headers = RebootHeaders;
	headers.emplace("Sec-Fetch-Site", "cross-site");
	BOOST_TEST(endpoint.Request("POST", "/admin/api/reboot", "{\"confirm\":true}", headers)->status_code ==
						 "403 Forbidden");
	headers = {{"X-ONVIF-Admin", "reboot"}, {"Content-Type", "text/plain"}};
	BOOST_TEST(endpoint.Request("POST", "/admin/api/reboot", "{\"confirm\":true}", headers)->status_code ==
						 "415 Unsupported Media Type");
	for (const auto* body : {"{}", "{\"confirm\":false}", "invalid", "{\"confirm\":true,\"extra\":1}"})
		BOOST_TEST(endpoint.Request("POST", "/admin/api/reboot", body, RebootHeaders)->status_code == "400 Bad Request");
	BOOST_TEST(endpoint.reboots.load() == 0u);
	auto response = endpoint.Request("POST", "/admin/api/reboot", "{\"confirm\":true}", RebootHeaders);
	BOOST_TEST(response->status_code == "202 Accepted");
	Tree json;
	boost::property_tree::read_json(response->content, json);
	BOOST_TEST(json.get<std::string>("status") == "rebooting");
	endpoint.Status();
	BOOST_TEST(endpoint.reboots.load() == 1u);
	// A pending SOAP reboot and a WebUI reboot share the same guard.
	endpoint.server->GetServerConfigs()->reboot_pending->store(true);
	BOOST_TEST(endpoint.Status().get<bool>("reboot_pending"));
	BOOST_TEST(endpoint.Request("POST", "/admin/api/reboot", "{\"confirm\":true}", RebootHeaders)->status_code ==
						 "202 Accepted");
	endpoint.Status();
	BOOST_TEST(endpoint.reboots.load() == 1u);
	endpoint.server->GetServerConfigs()->reboot_pending->store(false);
}

BOOST_AUTO_TEST_CASE(reboot_unavailable_is_reported_without_a_false_acknowledgement)
{
	Endpoint endpoint(false);
	BOOST_TEST(!endpoint.Status().get<bool>("reboot_available"));
	BOOST_TEST(endpoint.Request("POST", "/admin/api/reboot", "{\"confirm\":true}", RebootHeaders)->status_code ==
						 "503 Service Unavailable");
	BOOST_TEST(endpoint.reboots.load() == 0u);
}
BOOST_AUTO_TEST_CASE(journal_http_filters_and_cursors_do_not_consume_history)
{
	Endpoint endpoint;
	auto journal = endpoint.server->GetServerConfigs()->journal;
	journal->Add("info", "system", "application", "Started");
	journal->Add("info", "detector", "Motion", "Rule=frontdoor State=true");
	journal->Add("warn", "client", "ONVIF", "Access rejected");
	auto response = endpoint.Request("GET", "/admin/api/journal?category=detector&q=FRONTDOOR&limit=1");
	BOOST_TEST(response->status_code == "200 OK");
	Tree result;
	boost::property_tree::read_json(response->content, result);
	BOOST_REQUIRE_EQUAL(result.get_child("entries").size(), 1u);
	BOOST_TEST(result.get_child("entries").front().second.get<std::string>("source") == "Motion");
	BOOST_TEST(result.get<unsigned>("cursor") == 2u);
	BOOST_TEST(result.get<bool>("has_more"));
	BOOST_TEST(journal->Read(0, 100, {}).entries.size() == 3u);
	for (const auto* query :
			 {"after=-1", "limit=0", "limit=201", "after=abc", "category=invalid", "level=debug", "limit=1&limit=2"})
		BOOST_TEST(endpoint.Request("GET", std::string("/admin/api/journal?") + query)->status_code == "400 Bad Request");
}

BOOST_AUTO_TEST_CASE(detector_catalog_and_rule_crud_use_the_shared_model)
{
	Endpoint endpoint;
	auto get = [&]() {
		auto response = endpoint.Request("GET", "/admin/api/detectors");
		BOOST_REQUIRE_EQUAL(response->status_code, "200 OK");
		Tree result;
		boost::property_tree::read_json(response->content, result);
		return result;
	};
	auto post = [&](const Tree& command, const std::string& expected = "200 OK") {
		std::ostringstream json;
		boost::property_tree::write_json(json, command);
		auto response = endpoint.Request("POST", "/admin/api/detectors", json.str(),
																		 {{"Content-Type", "application/json"}, {"X-ONVIF-Admin", "detectors"}});
		BOOST_REQUIRE_EQUAL(response->status_code, expected);
	};
	auto snapshot = get();
	BOOST_TEST(snapshot.get_child("simple").size() == 4u);
	BOOST_TEST(snapshot.get_child("families").size() == 6u);
	const auto cfg = snapshot.get_child("configurations").front().second;
	const auto token = cfg.get<std::string>("token");
	BOOST_TEST(cfg.get_child("templates").size() == 6u);
	// Every supported type can be created and removed from its canonical template.
	for (const auto& [_, initial] : cfg.get_child("templates"))
	{
		auto rule = initial;
		rule.put("Name", "UI test");
		Tree command;
		command.put("action", "create");
		command.put("configuration", token);
		command.put("name", "UI test");
		command.put_child("rule", rule);
		post(command);
		auto stored = get().get_child("configurations").front().second.get_child("rules").front().second;
		stored.erase("enabled");
		BOOST_TEST(stored.get<std::string>("Type") == rule.get<std::string>("Type"));
		command.put("action", "toggle");
		command.put("enabled", false);
		post(command);
		BOOST_TEST(!endpoint.server->GetServerConfigs()->detectors->RuleEnabled(token, "UI test"));
		command.put("action", "modify");
		command.put_child("previous", stored);
		command.put_child("rule", stored);
		post(command);
		auto stale = stored;
		stale.put("Name", "stale");
		command.put_child("previous", stale);
		post(command, "409 Conflict");
		command.put("action", "delete");
		command.put_child("previous", stored);
		post(command);
		BOOST_TEST(get().get_child("configurations").front().second.get_child("rules").empty());
		BOOST_TEST(endpoint.server->GetServerConfigs()->detectors->RuleEnabled(token, "UI test"));
	}
	Tree command;
	command.put("action", "detector");
	command.put("id", "MotionAlarm");
	command.put("enabled", true);
	post(command);
	BOOST_TEST(endpoint.server->GetServerConfigs()->detectors->Enabled("MotionAlarm"));
	BOOST_TEST(endpoint.Request("POST", "/admin/api/detectors", "{}")->status_code == "403 Forbidden");
}

BOOST_AUTO_TEST_CASE(schema_failure_reports_field_and_keeps_file_and_rule_model_unchanged)
{
	Endpoint endpoint;
	auto reader = endpoint.server->MediaProfilesManager()->ReaderWriter();
	std::ifstream original(endpoint.path + "/media_profiles.config");
	const std::string bytes((std::istreambuf_iterator<char>(original)), {});
	auto response = endpoint.Request("GET", "/admin/api/detectors");
	Tree catalog;
	boost::property_tree::read_json(response->content, catalog);
	const auto config = catalog.get_child("configurations").front().second;
	const auto rule = config.get_child("templates").front().second;
	// Simulate a previously loaded invalid setting outside the edited rule.
	// Validating just the request/rule would miss this configuration error.
	reader->ConfigsTree().get_child("VideoEncoder").front().second.put("RateControl.BitrateLimit", "bad-number");
	Tree command;
	command.put("action", "create");
	command.put("configuration", config.get<std::string>("token"));
	command.put("name", rule.get<std::string>("Name"));
	command.put_child("rule", rule);
	std::ostringstream body;
	boost::property_tree::write_json(body, command);
	auto rejected = endpoint.Request("POST", "/admin/api/detectors", body.str(),
																	 {{"Content-Type", "application/json"}, {"X-ONVIF-Admin", "detectors"}});
	BOOST_REQUIRE_EQUAL(rejected->status_code, "422 Unprocessable Entity");
	Tree error;
	boost::property_tree::read_json(rejected->content, error);
	BOOST_TEST(error.get<std::string>("error") == "ConfigValidationFailed");
	BOOST_REQUIRE(!error.get_child("validation_errors").empty());
	BOOST_TEST(error.get_child("validation_errors").front().second.get<std::string>("file") == "media_profiles.config");
	BOOST_TEST(error.get_child("validation_errors").front().second.get<std::string>("path").find("BitrateLimit") !=
						 std::string::npos);
	std::ifstream unchanged(endpoint.path + "/media_profiles.config");
	BOOST_TEST(std::string((std::istreambuf_iterator<char>(unchanged)), {}) == bytes);
	BOOST_TEST(reader->ConfigsTree().get_child("Analytics").front().second.get_child("rules").empty());
	BOOST_TEST(endpoint.server->Rules()->Snapshot().get_child("Analytics").front().second.get_child("rules").empty());
	for (const auto& entry : fs::directory_iterator(endpoint.path))
		BOOST_TEST(!entry.path().filename().string().starts_with("media_profiles.config."));
}

BOOST_AUTO_TEST_SUITE_END()
