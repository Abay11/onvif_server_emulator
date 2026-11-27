#include "../onvif/RecordingArchive.h"

#include <boost/test/unit_test.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <filesystem>
#include <future>

namespace
{
using osrv::RecordingArchive;
using namespace std::chrono_literals;

struct ArchiveFile
{
	std::filesystem::path path = std::filesystem::temp_directory_path() /
															 ("onvif-archive-" + boost::uuids::to_string(boost::uuids::random_generator()()));
	~ArchiveFile()
	{
		std::error_code ignored;
		std::filesystem::remove(path, ignored);
	}
};

RecordingArchive::Source Source(const std::string& channel)
{
	RecordingArchive::Source source;
	source.channel = channel;
	source.profile = channel + "-profile";
	source.videoSource = channel + "-input";
	source.name = channel;
	source.address = "http://127.0.0.1:8080/onvif/device_service";
	source.video.put("Resolution.Width", 1280);
	return source;
}
} // namespace

BOOST_AUTO_TEST_CASE(recording_archive_serializes_starts_and_preserves_gaps)
{
	ArchiveFile fixture;
	auto now = RecordingArchive::Time{1s};
	auto clock = [&] { return now; };
	RecordingArchive archive(fixture.path.string(), clock);
	BOOST_CHECK(archive.Snapshot().empty());
	const auto one = Source("one");
	auto a = std::async(std::launch::async, [&] { return archive.Start(one); });
	const auto first = archive.Start(one);
	BOOST_CHECK_EQUAL(first.jobToken, a.get().jobToken);
	BOOST_REQUIRE_EQUAL(archive.Snapshot().size(), 1);
	BOOST_REQUIRE_EQUAL(archive.Snapshot().front().intervals.size(), 1);

	now += 2s;
	archive.Stop("one");
	archive.Stop("one");
	BOOST_CHECK(!archive.Job(first.jobToken)->active);
	BOOST_CHECK(archive.Recording(first.token)->intervals.at(0).end == now);
	now += 6s;
	archive.SetJobMode(first.jobToken, true, [](const auto&, const auto&) { return true; });
	BOOST_REQUIRE_EQUAL(archive.Recording(first.token)->intervals.size(), 2);
	BOOST_CHECK(archive.Recording(first.token)->intervals.back().start == now);
	now += 1s;
	archive.Start(Source("removed"));
	archive.Suspend();

	now += 15s;
	RecordingArchive restarted(fixture.path.string(), clock);
	restarted.Resume([](const auto& channel, const auto&) { return channel == "one"; });
	const auto resumed = restarted.Recording(first.token);
	BOOST_REQUIRE(resumed);
	BOOST_CHECK_EQUAL(resumed->jobToken, first.jobToken);
	BOOST_REQUIRE_EQUAL(resumed->intervals.size(), 3);
	BOOST_CHECK(resumed->intervals.back().start == now);
	BOOST_CHECK(resumed->intervals[1].end == now - 15s);
	const auto entries = restarted.Snapshot();
	BOOST_REQUIRE_EQUAL(entries.size(), 2);
	BOOST_CHECK_EQUAL(entries.back().source.channel, "removed");
	BOOST_CHECK(!entries.back().active);
	BOOST_CHECK(entries.back().intervals.back().end == now - 15s);
}
