#include "../Server.h"
#include "../utility/MediaProfilesManager.h"
#include "../utility/MetadataConfiguration.h"
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <filesystem>

namespace
{
using boost::property_tree::ptree;
const std::map<std::string, std::string> Namespaces{{"tns1", "http://www.onvif.org/ver10/topics"}};
ptree Filter(const std::string& kind, const std::string& expression)
{
	ptree config, filter, filters;
	config.put("Events", true);
	filter.put("Kind", kind);
	filter.put("Expression", expression);
	filter.put("Dialect", kind == "TopicExpression" ? "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet"
																									: "http://www.onvif.org/ver10/tev/messageContentFilter/ItemFilter");
	filter.put("Namespaces.alias", "http://www.onvif.org/ver10/topics");
	filter.put("Namespaces.x", "http://www.onvif.org/ver10/schema");
	filters.push_back({"", filter});
	config.add_child("Filters", filters);
	return config;
}
struct Fixture
{
	std::string directory =
			"stage4-configs-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	std::unique_ptr<utility::media::MediaProfilesManager> manager;
	osrv::ServerConfigs server;
	std::unique_ptr<osrv::metadata::Control> control;
	Fixture()
	{
		std::filesystem::create_directory(directory);
		std::filesystem::copy_file("../../server_configs/media_profiles.config", directory + "/media_profiles.config");
		manager = std::make_unique<utility::media::MediaProfilesManager>(directory + "/media_profiles.config");
		server.rtsp_metadata_enabled_ = true;
		server.rtsp_port_ = "8554";
		control = std::make_unique<osrv::metadata::Control>(*manager, server);
		manager->ReaderWriter()->OnSave([this] { control->Refresh(); });
	}
	~Fixture()
	{
		std::filesystem::remove_all(directory);
	}
	ptree Call(const std::string& operation, const std::string& body = "", bool media2 = false)
	{
		auto uri = media2 ? "http://www.onvif.org/ver20/media/wsdl" : "http://www.onvif.org/ver10/media/wsdl";
		return control->Execute(operation,
														std::string("<s:Envelope xmlns:s='http://www.w3.org/2003/05/soap-envelope' xmlns:m='") +
																uri +
																"' xmlns:tt='http://www.onvif.org/ver10/schema' "
																"xmlns:wsnt='http://docs.oasis-open.org/wsn/b-2'><s:Body><m:" +
																operation + ">" + body + "</m:" + operation + "></s:Body></s:Envelope>",
														media2);
	}
	std::string Configuration(const std::string& events)
	{
		return "<m:Configuration token='MetadataToken0'><tt:Name>Changed &amp; "
					 "filtered</tt:Name><tt:UseCount>999</tt:UseCount>" +
					 events +
					 "<tt:Analytics>false</tt:Analytics><tt:Multicast><tt:Address><tt:Type>IPv4</"
					 "tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address></tt:Address><tt:Port>0</tt:Port><tt:TTL>1</"
					 "tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast><tt:SessionTimeout>PT60S</tt:SessionTimeout></"
					 "m:Configuration>";
	}
};
} // namespace
BOOST_AUTO_TEST_SUITE(MetadataConfiguration)
BOOST_AUTO_TEST_CASE(filters_match_topics_by_namespace_and_enforce_event_selection)
{
	osrv::event::NotificationMessage message;
	message.topic = "tns1:RuleEngine/CellMotionDetector/Motion";
	auto settings =
			osrv::metadata::Compile(Filter("TopicExpression", "alias:Device//.|alias:RuleEngine/CellMotionDetector//."));
	BOOST_TEST(settings->accepts(message, Namespaces));
	message.topic = "tns1:RuleEngine/AudioDetector/Detection";
	BOOST_TEST(!settings->accepts(message, Namespaces));
	auto config = Filter("TopicExpression", "alias:RuleEngine//.");
	config.put("Events", false);
	BOOST_TEST(!osrv::metadata::Compile(config)->accepts(message, Namespaces));
	BOOST_CHECK_THROW(osrv::metadata::Compile(Filter("TopicExpression", "unknown:RuleEngine//.")), osrv::metadata::Fault);
	BOOST_CHECK_THROW(osrv::metadata::Compile(Filter("TopicExpression", "alias:RuleEngine/*")), osrv::metadata::Fault);
}
BOOST_AUTO_TEST_CASE(item_filter_boolean_precedence_namespaces_and_element_items)
{
	osrv::event::NotificationMessage message;
	message.data_name = "State";
	message.data_value = "false";
	message.source_item_descriptions.emplace_back("Rule", "MotionRule");
	auto config = Filter("MessageContent",
											 "boolean(//x:SimpleItem[@Name='State' and @Value='true']) or "
											 "boolean(//x:SimpleItem[@Name='Rule']) and not(boolean(//x:ElementItem[@Name='Image']))");
	auto settings = osrv::metadata::Compile(config);
	BOOST_TEST(settings->accepts(message, Namespaces));
	ptree item;
	item.put("<xmlattr>.Name", "Image");
	message.data_element_items.push_back(item);
	BOOST_TEST(!settings->accepts(message, Namespaces));
	message.data_value = "true";
	BOOST_TEST(settings->accepts(message, Namespaces));
	for (const auto& invalid : {"true()", "boolean(//x:SimpleItem)", "boolean(//bad:SimpleItem[@Name='a'])",
															"boolean(//x:SimpleItem[@Other='a'])"})
		BOOST_CHECK_THROW(osrv::metadata::Compile(Filter("MessageContent", invalid)), osrv::metadata::Fault);
}
BOOST_FIXTURE_TEST_CASE(configuration_persists_associations_and_publishes_immutable_snapshots, Fixture)
{
	auto shared = manager->SharedConfigs();
	Call("AddMetadataConfiguration",
			 "<m:ProfileToken>ProfileToken0</m:ProfileToken><m:ConfigurationToken>MetadataToken0</m:ConfigurationToken>");
	Call("AddMetadataConfiguration",
			 "<m:ProfileToken>ProfileToken0</m:ProfileToken><m:ConfigurationToken>MetadataToken1</m:ConfigurationToken>");
	BOOST_TEST(shared->get_child("MediaProfiles").front().second.count("Metadata") == 1);
	BOOST_TEST(shared->get_child("MediaProfiles").front().second.get<std::string>("Metadata") == "MetadataToken1");
	Call("AddMetadataConfiguration",
			 "<m:ProfileToken>ProfileToken1</m:ProfileToken><m:ConfigurationToken>MetadataToken0</m:ConfigurationToken>");
	std::shared_ptr<const osrv::metadata::Settings> snapshot;
	std::string requestedPath, synchronized;
	control->Bind(
			[&](const auto& path, const auto&, bool, auto settings) {
				requestedPath = path;
				snapshot = settings;
			},
			[&](const auto& path) { synchronized = path; });
	auto uri = control->Uri("ProfileToken1");
	auto old = snapshot;
	BOOST_TEST(old->events);
	Call("SetMetadataConfiguration", Configuration("") + "<m:ForcePersistence>false</m:ForcePersistence>");
	BOOST_TEST(control->Uri("ProfileToken1") == uri);
	BOOST_TEST(!snapshot->events);
	BOOST_TEST(old->events);
	Call("SetSynchronizationPoint", "<m:ProfileToken>ProfileToken1</m:ProfileToken>", true);
	BOOST_TEST(synchronized == requestedPath);
	manager->ReaderWriter()->Read();
	BOOST_TEST(shared.get() == &manager->ReaderWriter()->ConfigsTree());
	BOOST_TEST(!manager->GetConfigByToken("MetadataToken0", "Metadata").get<bool>("Events"));
	auto response = Call("GetMetadataConfiguration", "<m:ConfigurationToken>MetadataToken0</m:ConfigurationToken>");
	BOOST_TEST(response.get<int>("trt:Configuration.tt:UseCount") == 1);
	BOOST_TEST(!response.get_child("trt:Configuration").get_child_optional("tt:Events"));
	Call("RemoveMetadataConfiguration", "<m:ProfileToken>ProfileToken1</m:ProfileToken>");
	Call("RemoveMetadataConfiguration", "<m:ProfileToken>ProfileToken1</m:ProfileToken>");
	BOOST_CHECK_THROW(Call("GetMetadataConfiguration", "<m:ConfigurationToken>missing</m:ConfigurationToken>"),
										osrv::no_config);
	BOOST_CHECK_THROW(Call("GetCompatibleMetadataConfigurations", "<m:ProfileToken>missing</m:ProfileToken>"),
										osrv::no_such_profile);
}
BOOST_FIXTURE_TEST_CASE(configuration_rejects_unsupported_options_without_mutation, Fixture)
{
	const auto before = manager->GetConfigByToken("MetadataToken0", "Metadata").get<std::string>("Name");
	BOOST_CHECK_THROW(Call("SetMetadataConfiguration",
												 Configuration("<tt:Events><tt:Filter><wsnt:MessageContent "
																			 "Dialect='unsupported'>true()</wsnt:MessageContent></tt:Filter></tt:Events>"),
												 true),
										osrv::metadata::Fault);
	BOOST_CHECK_THROW(Call("SetMetadataConfiguration",
												 Configuration("<tt:Events/>") + "<m:ForcePersistence>invalid</m:ForcePersistence>"),
										osrv::metadata::Fault);
	BOOST_TEST(manager->GetConfigByToken("MetadataToken0", "Metadata").get<std::string>("Name") == before);
	auto options = Call("GetMetadataConfigurationOptions", "", true);
	BOOST_TEST(options.get<int>("tr2:Options.<xmlattr>.MaxContentFilterSize") == 4096);
}
BOOST_FIXTURE_TEST_CASE(incompatible_profiles_and_invalid_metadata_removal_preserve_associations, Fixture)
{
	manager->MetadataAvailable(false);
	BOOST_CHECK_THROW(manager->AddConfiguration("ProfileToken1", "Metadata", "MetadataToken0"), osrv::no_config);
	BOOST_TEST(manager->GetProfileByToken("ProfileToken1").get<std::string>("Metadata", "").empty());
	manager->MetadataAvailable(true);
	manager->Create("Empty");
	auto profile = manager->Back().get<std::string>("token");
	BOOST_TEST(Call("GetCompatibleMetadataConfigurations", "<m:ProfileToken>" + profile + "</m:ProfileToken>").empty());
	BOOST_CHECK_THROW(manager->AddConfiguration(profile, "Metadata", "MetadataToken0"), osrv::configuration_conflict);
	manager->AddConfiguration("ProfileToken1", "Metadata", "MetadataToken0");
	BOOST_CHECK_THROW(manager->RemoveConfiguration("ProfileToken1", "Metadata", "wrong"), osrv::no_config);
	BOOST_CHECK_THROW(manager->RemoveConfiguration("ProfileToken1", "VideoSource"), osrv::configuration_conflict);
	BOOST_TEST(manager->GetProfileByToken("ProfileToken1").get<std::string>("Metadata") == "MetadataToken0");
	BOOST_CHECK_THROW(Call("GetCompatibleMetadataConfigurations"), osrv::no_such_profile);
	BOOST_CHECK_THROW(Call("GetMetadataConfiguration", "<tt:ConfigurationToken>MetadataToken0</tt:ConfigurationToken>"),
										osrv::metadata::Fault);
	BOOST_CHECK_THROW(Call("SetMetadataConfiguration",
												 Configuration("<tt:Events/><tt:Events/>") + "<m:ForcePersistence>true</m:ForcePersistence>"),
										osrv::metadata::Fault);
}
BOOST_AUTO_TEST_CASE(item_filter_advertised_size_and_absent_element_value_semantics)
{
	auto expression = std::string("boolean(//x:SimpleItem[@Name='State'])");
	expression.resize(4096, ' ');
	osrv::event::NotificationMessage message;
	message.data_name = "State";
	BOOST_TEST(osrv::metadata::Compile(Filter("MessageContent", expression))->accepts(message, Namespaces));
	expression += ' ';
	BOOST_CHECK_THROW(osrv::metadata::Compile(Filter("MessageContent", expression)), osrv::metadata::Fault);
	ptree item;
	item.put("<xmlattr>.Name", "Image");
	message.data_element_items.push_back(item);
	BOOST_TEST(!osrv::metadata::Compile(Filter("MessageContent", "boolean(//x:ElementItem[@Value=''])"))
									->accepts(message, Namespaces));
}
BOOST_FIXTURE_TEST_CASE(stable_uri_respects_media2_length_limit_for_maximum_tokens, Fixture)
{
	const std::string first(64, 'a'), second(64, 'b');
	auto profile = manager->GetProfileByToken("ProfileToken0");
	profile.put("token", first);
	manager->ReaderWriter()->ConfigsTree().get_child("MediaProfiles").push_back({"", profile});
	profile.put("token", second);
	manager->ReaderWriter()->ConfigsTree().get_child("MediaProfiles").push_back({"", profile});
	control->Bind([](const auto&, const auto&, bool, auto) {}, {});
	auto uri = control->Uri(first);
	BOOST_TEST(uri.size() <= 128);
	BOOST_TEST(control->Uri(first) == uri);
	BOOST_TEST(control->Uri(second) != uri);
}
BOOST_FIXTURE_TEST_CASE(persistence_failure_restores_profile_state, Fixture)
{
	manager->Create("Delete candidate");
	auto token = manager->Back().get<std::string>("token");
	auto shared = manager->SharedConfigs();
	auto count = shared->get_child("MediaProfiles").size();
	std::filesystem::rename(directory, directory + ".hidden");
	try
	{
		BOOST_CHECK_THROW(manager->AddConfiguration("ProfileToken1", "Metadata", "MetadataToken0"), std::system_error);
		BOOST_TEST(manager->GetProfileByToken("ProfileToken1").get<std::string>("Metadata", "").empty());
		BOOST_CHECK_THROW(manager->Create("Cannot persist"), std::system_error);
		BOOST_TEST(shared->get_child("MediaProfiles").size() == count);
		BOOST_CHECK_THROW(manager->Delete(token), std::system_error);
		BOOST_TEST(manager->GetProfileByToken(token).get<std::string>("Name") == "Delete candidate");
	}
	catch (...)
	{
		std::filesystem::rename(directory + ".hidden", directory);
		throw;
	}
	std::filesystem::rename(directory + ".hidden", directory);
	manager->ReaderWriter()->Read();
	BOOST_TEST(shared->get_child("MediaProfiles").size() == count);
}
BOOST_FIXTURE_TEST_CASE(metadata_configuration_batches_validate_final_profile_and_commit_atomically, Fixture)
{
	manager->Create("Batch profile", {{"Metadata", "MetadataToken0"},
																		{"VideoEncoder", "VideoEncoderToken0"},
																		{"VideoSource", "VideoSrcConfigToken0"}});
	auto token = manager->Back().get<std::string>("token");
	BOOST_TEST(manager->Back().get<std::string>("Metadata") == "MetadataToken0");
	const auto count = manager->SharedConfigs()->get_child("MediaProfiles").size();
	BOOST_CHECK_THROW(manager->Create("Invalid batch", {{"Metadata", "MetadataToken0"}, {"VideoSource", "missing"}}),
										osrv::invalid_token);
	BOOST_TEST(manager->SharedConfigs()->get_child("MediaProfiles").size() == count);
	BOOST_CHECK_THROW(
			manager->AddConfigurations("ProfileToken1", {{"Metadata", "MetadataToken0"}, {"VideoSource", "missing"}}),
			osrv::invalid_token);
	BOOST_TEST(manager->GetProfileByToken("ProfileToken1").get<std::string>("Metadata", "").empty());
	manager->RemoveConfigurations(token, {{"VideoSource", ""}, {"Metadata", "MetadataToken0"}, {"VideoEncoder", ""}});
	BOOST_TEST(manager->GetProfileByToken(token).get<std::string>("Metadata", "").empty());
}
BOOST_FIXTURE_TEST_CASE(compression_none_round_trips_through_media_and_media2, Fixture)
{
	for (bool media2 : {false, true})
	{
		auto configuration = Configuration("<tt:Events/>");
		configuration.insert(configuration.find("token="), "CompressionType='None' ");
		Call("SetMetadataConfiguration", configuration + (media2 ? "" : "<m:ForcePersistence>true</m:ForcePersistence>"),
				 media2);
		auto result = Call(media2 ? "GetMetadataConfigurations" : "GetMetadataConfiguration",
											 "<m:ConfigurationToken>MetadataToken0</m:ConfigurationToken>", media2);
		auto prefix = media2 ? "tr2:Configurations" : "trt:Configuration";
		BOOST_TEST(result.get<std::string>(std::string(prefix) + ".<xmlattr>.CompressionType", "") == "None");
		configuration.replace(configuration.find("'None'"), 6, "'GZIP'");
		BOOST_CHECK_THROW(Call("SetMetadataConfiguration",
													 configuration + (media2 ? "" : "<m:ForcePersistence>true</m:ForcePersistence>"), media2),
											osrv::metadata::Fault);
	}
}
BOOST_AUTO_TEST_SUITE_END()
