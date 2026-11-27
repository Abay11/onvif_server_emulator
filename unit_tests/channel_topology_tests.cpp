#include "../utility/ChannelTopology.h"
#include "../utility/MediaProfilesManager.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <filesystem>

using boost::property_tree::ptree;
using utility::media::ChannelTopology;

namespace
{
const char* Bundled = "../../server_configs/channels.config";

ptree TopologyWithVideoOnlyChannels(unsigned count)
{
	ptree document;
	boost::property_tree::read_json(Bundled, document);
	auto& channels = document.get_child("channels");
	while (channels.size() < count)
	{
		const auto suffix = std::to_string(channels.size());
		auto channel = channels.front().second;
		channel.put("token", "Channel" + suffix);
		channel.put("videoSource.token", "VideoSource" + suffix);
		auto& media = channel.get_child("media");
		auto& source = media.get_child("VideoSource").front().second;
		source.put("token", "SourceConfig" + suffix);
		source.put("SourceToken", "VideoSource" + suffix);
		source.put_child("CompatiblePtzNodes", ptree{});
		source.erase("CompatibleAnalyticsConfigurations");

		auto profile = media.get_child("MediaProfiles").front().second;
		profile.put("token", "Profile" + suffix);
		profile.put("VideoSource", "SourceConfig" + suffix);
		profile.put("VideoEncoder", "Encoder" + suffix);
		for (const auto* type : {"AudioSource", "AudioEncoder", "Analytics", "PTZ", "Metadata"})
			profile.erase(type);
		auto& profiles = media.get_child("MediaProfiles");
		profiles.clear();
		profiles.push_back({"", profile});
		for (const auto* catalog : {"VideoEncoder", "VideoEncoderConfigurations"})
		{
			auto& encoders = media.get_child(catalog);
			auto encoder = encoders.front().second;
			encoder.put("token", "Encoder" + suffix);
			encoders.clear();
			encoders.push_back({"", encoder});
		}
		for (const auto* catalog : {"AudioSources", "AudioSource", "AudioEncoder", "PTZ", "PTZConfigurationOptions"})
			media.put_child(catalog, ptree{});
		media.erase("Analytics");
		media.erase("Metadata");
		channels.push_back({"", channel});
	}
	return document;
}
} // namespace

BOOST_AUTO_TEST_SUITE(ChannelTopologyTests)

BOOST_AUTO_TEST_CASE(BundledChannels)
{
	const auto topology = ChannelTopology::Load(Bundled);
	BOOST_REQUIRE_EQUAL(topology.Channels().size(), 1);
	BOOST_CHECK_EQUAL(topology.Owner("VideoSources", "VideoSource0"), "Channel0");
	BOOST_CHECK_EQUAL(topology.Owner("MediaProfiles", "ProfileToken1"), "Channel0");
}

BOOST_AUTO_TEST_CASE(OwnershipAndChannelCount)
{
	auto document = TopologyWithVideoOnlyChannels(32);
	auto& channels = document.get_child("channels");
	const ChannelTopology topology(document);
	BOOST_CHECK_EQUAL(topology.Channels().size(), 32);
	BOOST_CHECK_EQUAL(topology.Media().get_child("MediaProfiles").size(), 33);
	BOOST_CHECK_EQUAL(topology.Owner("MediaProfiles", "Profile31"), "Channel31");
	channels.back().second.put("name", "Renamed camera");
	const ChannelTopology renamed(document);
	BOOST_CHECK_EQUAL(renamed.Owner("VideoSources", "VideoSource31"), "Channel31");

	auto& profile = channels.back().second.get_child("media.MediaProfiles").front().second;
	profile.put("AudioSource", "AudioSrcCfg0");
	BOOST_CHECK_THROW(ChannelTopology{document}, std::invalid_argument);
	profile.erase("AudioSource");
	profile.put("VideoEncoder", "unknown");
	BOOST_CHECK_THROW(ChannelTopology{document}, std::invalid_argument);
	profile.put("VideoEncoder", "Encoder31");
	channels.push_back(channels.back());
	BOOST_CHECK_THROW(ChannelTopology{document}, std::invalid_argument);
	channels.clear();
	BOOST_CHECK_THROW(ChannelTopology{document}, std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(ProfileOwnershipPersistsAfterDetach)
{
	namespace fs = std::filesystem;
	struct Fixture
	{
		fs::path path =
				fs::temp_directory_path() / ("onvif-channels-" + boost::uuids::to_string(boost::uuids::random_generator()()));
		Fixture()
		{
			fs::copy_file(Bundled, path);
		}
		~Fixture()
		{
			std::error_code ignored;
			fs::remove(path, ignored);
		}
	} fixture;
	const auto path = fixture.path.string();
	const char* media = "../../server_configs/media_profiles.config";
	utility::media::MediaProfilesManager manager(media);
	manager.ActivateTopology(path);
	manager.Create("Pending");
	const auto token = manager.Back().get<std::string>("token");
	manager.AddConfiguration(token, "VideoEncoder", "VideoEncoderToken1");
	manager.RemoveConfiguration(token, "VideoEncoder");

	utility::media::MediaProfilesManager reloaded(media);
	reloaded.ActivateTopology(path);
	const auto& profile = reloaded.GetProfileByToken(token);
	BOOST_CHECK_EQUAL(profile.get<std::string>("channel"), "Channel0");
	BOOST_CHECK(!profile.get_child_optional("VideoEncoder"));
	BOOST_CHECK(reloaded.Compatible(profile, "VideoEncoder", "VideoEncoderToken0"));
	BOOST_CHECK(!reloaded.Compatible(profile, "VideoEncoder", "unknown"));
}

BOOST_AUTO_TEST_SUITE_END()
