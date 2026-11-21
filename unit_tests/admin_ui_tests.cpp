#include "../Server.h"
#include "../Simple-Web-Server/client_http.hpp"
#include "../Simple-Web-Server/server_http.hpp"
#include "../include/LoggerFactories.h"
#include "../utility/AnalyticsRules.h"
#include "../utility/ConfigValidation.h"
#include "../utility/MediaProfilesManager.h"
#include "../web/AdminUi.h"
#include "../web/WebAuthentication.h"
#include "../onvif_services/EventSource.h"
#include "../onvif_services/pullpoint/event_generators.h"
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
	std::string cookie;
	Endpoint(bool available = true, bool signed_in = true)
	{
		fs::copy("../../server_configs", path, fs::copy_options::recursive);
		Tree common;
		boost::property_tree::read_json(path + "/common.config", common);
		// Use a distinctive secret so accidental raw configuration exposure is visible.
		Tree users;
		boost::property_tree::read_json(path + "/users.config", users);
		users.get_child("users").front().second.put("password", "admin-secret-must-not-leak");
		if (!signed_in)
		{
			Tree other;
			other.put("login", "second-admin"); other.put("password", "second-secret");
			other.put("type", "administrator"); other.put("enabled", true);
			users.get_child("users").push_back({"", other});
			other.put("login", "disabled-admin"); other.put("enabled", false);
			users.get_child("users").push_back({"", other});
		}
		boost::property_tree::write_json(path + "/users.config", users);
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
		if (signed_in)
		{
			auto response = client->request("POST", "/admin/api/login",
				R"({"login":"a","password":"admin-secret-must-not-leak"})",
				{{"Content-Type", "application/json"}, {"X-ONVIF-Admin", "login"}, {"Connection", "close"}});
			BOOST_CHECK_EQUAL(response->status_code, "200 OK");
			if (auto stored = response->header.find("Set-Cookie"); stored != response->header.end())
				cookie = stored->second.substr(0, stored->second.find(';'));
		}
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
		if (!cookie.empty()) headers.emplace("Cookie", cookie);
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
BOOST_AUTO_TEST_CASE(administration_requires_enabled_administrator_and_revokes_sessions)
{
	Endpoint endpoint(true, false);
	auto page = endpoint.Request("GET", "/admin/");
	BOOST_TEST(page->status_code == "302 Found");
	BOOST_TEST(page->header.find("Location")->second == "/admin/login");
	BOOST_TEST(endpoint.Request("GET", "/admin/login")->content.string().find("autocomplete=\"current-password\"") != std::string::npos);
	for (const auto* path : {"/admin/api/status", "/admin/api/configuration", "/admin/api/detectors", "/admin/api/journal", "/admin/api/reboot"})
	{
		BOOST_TEST(endpoint.Request("GET", path)->status_code == "401 Unauthorized");
		if (std::string(path) != "/admin/api/journal")
			BOOST_TEST(endpoint.Request("POST", path, "{}")->status_code == "401 Unauthorized");
	}
	BOOST_TEST(endpoint.reboots == 0u);
	const Headers headers{{"Content-Type", "application/json"}, {"X-ONVIF-Admin", "login"}};
	for (const auto* body : {R"({"login":"a","password":"wrong"})", R"({"login":"o","password":"o1"})",
		R"({"login":"u","password":"u1"})", R"({"login":"disabled-admin","password":"second-secret"})"})
	{
		auto response = endpoint.Request("POST", "/admin/api/login", body, headers);
		BOOST_TEST(response->status_code == "401 Unauthorized");
		BOOST_TEST(response->header.count("Set-Cookie") == 0u);
	}
	BOOST_TEST(endpoint.Request("POST", "/admin/api/login", "{}", headers)->status_code == "400 Bad Request");
	auto crossSite = headers;
	crossSite.emplace("Origin", "http://untrusted.example");
	BOOST_TEST(endpoint.Request("POST", "/admin/api/login", R"({"login":"a","password":"admin-secret-must-not-leak"})", crossSite)->status_code == "403 Forbidden");
	std::string previous;
	for (const auto* body : {R"({"login":"a","password":"admin-secret-must-not-leak"})", R"({"login":"second-admin","password":"second-secret"})"})
	{
		auto response = endpoint.Request("POST", "/admin/api/login", body, headers);
		BOOST_REQUIRE_EQUAL(response->status_code, "200 OK");
		const auto cookie = response->header.find("Set-Cookie")->second;
		BOOST_TEST(cookie.find("HttpOnly") != std::string::npos);
		BOOST_TEST(cookie.find("SameSite=Strict") != std::string::npos);
		BOOST_TEST(cookie.find("Path=/admin") != std::string::npos);
		endpoint.cookie = cookie.substr(0, cookie.find(';'));
		BOOST_TEST(endpoint.Request("GET", "/admin/api/status")->status_code == "200 OK");
		BOOST_TEST(endpoint.Request("GET", "/admin/")->status_code == "200 OK");
		if (!previous.empty())
			BOOST_TEST(endpoint.client->request("GET", "/admin/api/status", "", {{"Cookie", previous}})->status_code == "401 Unauthorized");
		previous = endpoint.cookie;
	}
	BOOST_TEST(endpoint.Request("POST", "/admin/api/logout")->status_code == "403 Forbidden");
	auto logout = endpoint.Request("POST", "/admin/api/logout", "", {{"X-ONVIF-Admin", "logout"}});
	BOOST_TEST(logout->status_code == "200 OK");
	BOOST_TEST(logout->header.find("Set-Cookie")->second.find("Max-Age=0") != std::string::npos);
	BOOST_TEST(endpoint.Request("GET", "/admin/api/status")->status_code == "401 Unauthorized");
}

BOOST_AUTO_TEST_CASE(browser_sessions_expire_and_share_onvif_accounts)
{
	auto now = std::chrono::steady_clock::now();
	osrv::WebAuthentication sessions({{"admin-one", "secret-one", osrv::auth::USER_TYPE::ADMIN},
		{"admin-two", "secret-two", osrv::auth::USER_TYPE::ADMIN}, {"operator", "secret", osrv::auth::USER_TYPE::OPERATOR},
		{"disabled", "secret", osrv::auth::USER_TYPE::ADMIN, false}}, [&] { return now; });
	BOOST_TEST(sessions.Login("operator", "secret").empty());
	BOOST_TEST(sessions.Login("disabled", "secret").empty());
	BOOST_TEST(sessions.Login("admin-one", "wrong").empty());
	const auto one = sessions.Login("admin-one", "secret-one");
	const auto two = sessions.Login("admin-two", "secret-two");
	BOOST_REQUIRE(!one.empty()); BOOST_REQUIRE(!two.empty());
	BOOST_TEST(one != two);
	BOOST_TEST(sessions.Username(one) == "admin-one");
	BOOST_TEST(sessions.Username(two) == "admin-two");
	sessions.Logout(one);
	BOOST_TEST(sessions.Username(one).empty());
	BOOST_TEST(sessions.Username(two) == "admin-two");
	now += std::chrono::hours(9);
	BOOST_TEST(sessions.Username(two).empty());
	BOOST_TEST(!sessions.Login("admin-one", "secret-one").empty());
}

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


BOOST_AUTO_TEST_CASE(detector_settings_validate_before_persistence_and_apply_without_restart)
{
	Endpoint endpoint;
	auto configs = endpoint.server->GetServerConfigs();
	auto logger = std::shared_ptr<ILogger>(ConsoleLoggerFactory().GetLogger(ILogger::LVL_ERR));
	auto source = std::make_shared<osrv::event::EventSource>(*logger, configs->journal, configs->detectors);
	configs->event_source = source;
	auto generator = std::make_shared<osrv::event::MotionAlarmEventGenerator>("source", 3600,
		"tns1:VideoSource/MotionAlarm", source->GetIoContext(), *logger);
	source->AddGenerator(generator);
	source->ConfigureDetector("MotionAlarm", 3600, false, true);
	source->Run();
	auto post = [&](const std::string& body, const std::string& expected = "200 OK") {
		auto response = endpoint.Request("POST", "/admin/api/detectors", body,
			{{"Content-Type", "application/json"}, {"X-ONVIF-Admin", "detectors"}});
		BOOST_REQUIRE_EQUAL(response->status_code, expected);
		Tree result;
		boost::property_tree::read_json(response->content, result);
		return result;
	};
	auto bytes = [&]() {
		std::ifstream file(endpoint.path + "/event.config");
		return std::string((std::istreambuf_iterator<char>(file)), {});
	};
	const auto before = bytes();
	for (const auto* invalid : {"0", "-1", "2147483648", "\"invalid\""})
	{
		auto result = post(std::string(R"({"action":"settings","id":"MotionAlarm","settings":{"EventGenerationTimeout":)") +
			invalid + "}}", "422 Unprocessable Entity");
		BOOST_TEST(!result.get_child("validation_errors").empty());
		BOOST_TEST(bytes() == before);
	}
	post(R"({"action":"settings","id":"MotionAlarm","settings":{"Topic":"changed"}})", "400 Bad Request");
	BOOST_TEST(bytes() == before);
	post(R"({"action":"settings","id":"MotionAlarm","settings":{"Enabled":true,"AutoGenerateEvents":false,"EventGenerationTimeout":7}})");
	Tree saved;
	boost::property_tree::read_json(endpoint.path + "/event.config", saved);
	BOOST_TEST(saved.get<int>("MotionAlarm.EventGenerationTimeout") == 7);
	BOOST_TEST(!saved.get<bool>("MotionAlarm.AutoGenerateEvents"));
	BOOST_TEST(!saved.get_child("MotionAlarm").get_child_optional("GenerateEvents"));
	BOOST_TEST(configs->detectors->Enabled("MotionAlarm"));
	BOOST_TEST(!configs->detectors->Automatic("MotionAlarm"));
	auto client = source->Subscribe({}, false);
	post(R"({"action":"manual","id":"MotionAlarm"})");
	BOOST_TEST(client->Take(10).messages.size() == 1u);
	post(R"({"action":"settings","id":"MotionAlarm","settings":{"Enabled":false}})");
	post(R"({"action":"manual","id":"MotionAlarm"})", "409 Conflict");
	BOOST_TEST(client->Take(10).messages.empty());
	// The in-memory view and disk must agree after repeated partial settings saves.
	Tree view;
	auto response = endpoint.Request("GET", "/admin/api/detectors");
	boost::property_tree::read_json(response->content, view);
	for (const auto& [_, item] : view.get_child("simple"))
		if (item.get<std::string>("id") == "MotionAlarm")
		{
			BOOST_TEST(!item.get<bool>("settings.Enabled"));
			BOOST_TEST(!item.get<bool>("settings.AutoGenerateEvents"));
			BOOST_TEST(item.get<int>("settings.EventGenerationTimeout") == 7);
		}
	// An external file edit must not be overwritten by this server's cached view.
	saved.put("MotionAlarm.EventGenerationTimeout", 9);
	boost::property_tree::write_json(endpoint.path + "/event.config", saved);
	const auto external = bytes();
	post(R"({"action":"settings","id":"MotionAlarm","settings":{"Enabled":true}})", "409 Conflict");
	BOOST_TEST(bytes() == external);
	BOOST_TEST(!configs->detectors->Enabled("MotionAlarm"));

}


BOOST_AUTO_TEST_CASE(configuration_editor_validates_references_preserves_fixed_profiles_and_reports_restart)
{
	Endpoint endpoint;
	auto get = [&]() {
		auto response = endpoint.Request("GET", "/admin/api/configuration");
		BOOST_REQUIRE_EQUAL(response->status_code, "200 OK");
		BOOST_TEST(response->content.string().find("admin-secret-must-not-leak") == std::string::npos);
		Tree result;
		boost::property_tree::read_json(response->content, result);
		return result;
	};
	auto entry = [](const Tree& view, const std::string& name) {
		for (const auto& [_, file] : view.get_child("files")) if (file.get<std::string>("name") == name) return file;
		throw std::runtime_error("Missing file");
	};
	auto post = [&](const Tree& command, const std::string& expected = "200 OK") {
		std::ostringstream body;
		boost::property_tree::write_json(body, command);
		auto response = endpoint.Request("POST", "/admin/api/configuration", body.str(),
			{{"Content-Type", "application/json"}, {"X-ONVIF-Admin", "configuration"}});
		BOOST_REQUIRE_EQUAL(response->status_code, expected);
		Tree result;
		boost::property_tree::read_json(response->content, result);
		return result;
	};
	auto view = get();
	BOOST_TEST(view.get_child("files").size() == 15u);
	Tree command;
	command.put("file", "media_profiles.config");
	command.put("revision", entry(view, "media_profiles.config").get<std::string>("revision"));
	auto candidate = endpoint.server->MediaProfilesManager()->ReaderWriter()->ConfigsTree();
	auto original = candidate;
	Tree profile;
	profile.put("token", "WebProfile"); profile.put("Name", "Web profile"); profile.put("fixed", false);
	profile.put("VideoSource", "VideoSrcConfigToken0"); profile.put("Metadata", "MetadataToken0");
	candidate.get_child("MediaProfiles").push_back({"", profile});
	command.put("document", utility::media::MediaConfigJson(candidate));
	BOOST_TEST(!post(command).get<bool>("restart_required"));
	BOOST_TEST(endpoint.server->MediaProfilesManager()->GetProfileByToken("WebProfile").get<std::string>("Metadata") == "MetadataToken0");
	post(command, "409 Conflict");
	command.put("revision", entry(get(), "media_profiles.config").get<std::string>("revision"));
	auto invalid = candidate;
	invalid.get_child("MediaProfiles").back().second.put("Metadata", "missing");
	command.put("document", utility::media::MediaConfigJson(invalid));
	post(command, "400 Bad Request");
	invalid = candidate;
	invalid.get_child("MediaProfiles").pop_front();
	command.put("document", utility::media::MediaConfigJson(invalid));
	post(command, "400 Bad Request");
	invalid = candidate;
	invalid.get_child("MediaProfiles").back().second.erase("VideoSource");
	command.put("document", utility::media::MediaConfigJson(invalid));
	post(command, "400 Bad Request");
	invalid = candidate;
	invalid.get_child("Metadata").front().second.put("Name", "");
	command.put("document", utility::media::MediaConfigJson(invalid));
	post(command, "422 Unprocessable Entity");
	candidate.get_child("Metadata").front().second.put("Events", false);
	candidate.get_child("Metadata").front().second.put_child("Filters", Tree{});
	command.put("document", utility::media::MediaConfigJson(candidate));
	BOOST_TEST(!post(command).get<bool>("restart_required"));
	// Later native ONVIF mutations must preserve an empty Filters array.
	endpoint.server->MediaProfilesManager()->Create("Native profile after WebUI save");
	command.put("revision", entry(get(), "media_profiles.config").get<std::string>("revision"));
	candidate = endpoint.server->MediaProfilesManager()->ReaderWriter()->ConfigsTree();
	candidate.get_child("VideoSource").front().second.put("Name", "Updated source");
	command.put("document", utility::media::MediaConfigJson(candidate));
	BOOST_TEST(post(command).get<bool>("restart_required"));
	BOOST_TEST(get().get<bool>("restart_required"));
	BOOST_TEST(endpoint.Request("POST", "/admin/api/configuration", "{}")->status_code == "403 Forbidden");
}

BOOST_AUTO_TEST_CASE(configuration_passwords_are_write_only_and_boolean_types_survive_save)
{
	Endpoint endpoint;
	// Preserve the enabled flag as a JSON boolean while restoring redacted passwords.
	std::ifstream source("../../server_configs/users.config");
	std::string common((std::istreambuf_iterator<char>(source)), {});
	common.replace(common.find("\"a1\""), 4, "\"admin-secret-must-not-leak\"");
	std::ofstream(endpoint.path + "/users.config") << common;
	auto response = endpoint.Request("GET", "/admin/api/configuration");
	const auto body = response->content.string();
	BOOST_TEST(body.find("admin-secret-must-not-leak") == std::string::npos);
	Tree view, file;
	boost::property_tree::read_json(response->content, view);
	for (const auto& [_, item] : view.get_child("files")) if (item.get<std::string>("name") == "users.config") file = item;
	BOOST_TEST(file.get<std::string>("document").find("__UNCHANGED_SECRET__") != std::string::npos);
	Tree command;
	command.put("file", "users.config");
	command.put("revision", file.get<std::string>("revision"));
	command.put("document", file.get<std::string>("document"));
	std::ostringstream json;
	boost::property_tree::write_json(json, command);
	response = endpoint.Request("POST", "/admin/api/configuration", json.str(),
		{{"Content-Type", "application/json"}, {"X-ONVIF-Admin", "configuration"}});
	BOOST_REQUIRE_EQUAL(response->status_code, "200 OK");
	Tree result;
	boost::property_tree::read_json(response->content, result);
	BOOST_TEST(result.get<bool>("restart_required"));
	Tree saved;
	boost::property_tree::read_json(endpoint.path + "/users.config", saved);
	BOOST_TEST(saved.get_child("users").front().second.get<std::string>("password") == "admin-secret-must-not-leak");
	// The saved bytes must still satisfy the schema's boolean-only enabled fields.
	std::ifstream input(endpoint.path + "/users.config");
	utility::ValidateConfig("users.config", std::string((std::istreambuf_iterator<char>(input)), {}));
}


BOOST_AUTO_TEST_CASE(media_json_preserves_default_namespace_objects_and_empty_filter_arrays)
{
	Tree media;
	boost::property_tree::read_json("../../server_configs/media_profiles.config", media);
	Tree filter, namespaces, filters;
	filter.put("Kind", "TopicExpression");
	filter.put("Dialect", "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
	filter.put("Expression", "topic:RuleEngine//.");
	namespaces.push_back({"", Tree("urn:default")});
	namespaces.push_back({"topic", Tree("http://www.onvif.org/ver10/topics")});
	filter.put_child("Namespaces", namespaces);
	filters.push_back({"", filter});
	media.get_child("Metadata").front().second.put_child("Filters", filters);
	BOOST_CHECK_NO_THROW(utility::ValidateConfig("media_profiles.config", utility::media::MediaConfigJson(media)));
	media.get_child("Metadata").front().second.put_child("Filters", Tree{});
	BOOST_CHECK_NO_THROW(utility::ValidateConfig("media_profiles.config", utility::media::MediaConfigJson(media)));
}

BOOST_AUTO_TEST_SUITE_END()
