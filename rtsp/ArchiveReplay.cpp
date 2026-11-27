#include "ArchiveReplay.h"
#include "../utility/AudioSourceReader.h"

#include <gst/app/gstappsrc.h>
#include <gst/rtsp/gstrtsprange.h>

#include <atomic>

#include <algorithm>
#include <format>

namespace osrv::rtsp
{
namespace
{
constexpr std::uint64_t NtpEpoch = 2208988800ULL * GST_SECOND;
using Nano = std::chrono::nanoseconds;
using Time = RecordingArchive::Time;
using Archive = std::shared_ptr<RecordingArchive>;

void ConfigureMetadata(GstRTSPMedia* media)
{
	auto* root = gst_rtsp_media_get_element(media);
	auto* app = GST_APP_SRC(gst_bin_get_by_name_recurse_up(GST_BIN(root), "recorded_metadata"));
	gst_object_unref(root);
	if (!app)
		return;
	using Position = std::atomic<GstClockTime>;
	GstAppSrcCallbacks callbacks{};
	callbacks.seek_data = +[](GstAppSrc*, guint64 offset, gpointer data) -> gboolean {
		static_cast<Position*>(data)->store(offset);
		return TRUE;
	};
	callbacks.need_data = +[](GstAppSrc* app, guint, gpointer data) {
		static constexpr std::string_view document = R"(<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema"/>)";
		auto* buffer = gst_buffer_new_allocate(nullptr, document.size(), nullptr);
		gst_buffer_fill(buffer, 0, document.data(), document.size());
		GST_BUFFER_PTS(buffer) = static_cast<Position*>(data)->fetch_add(GST_SECOND);
		GST_BUFFER_DURATION(buffer) = GST_SECOND;
		gst_app_src_push_buffer(app, buffer);
	};
	gst_app_src_set_callbacks(
			app, &callbacks, new Position(0), +[](gpointer data) { delete static_cast<Position*>(data); });
	gst_object_unref(app);
}

GstRTSPStatusCode ReplayRange(GstRTSPClient*, GstRTSPContext* context, gpointer data)
{
	const auto* token =
			context->media ? static_cast<const char*>(g_object_get_data(G_OBJECT(context->media), "onvif-archive-recording"))
										 : nullptr;
	if (!token)
		return GST_RTSP_STS_OK;
	const auto& archive = *static_cast<Archive*>(data);
	const auto entry = archive->Recording(token);
	if (!entry)
		return GST_RTSP_STS_NOT_FOUND;

	Time start = entry->intervals.front().start;
	Time stop = Time::max();
	gchar* value = nullptr;
	if (gst_rtsp_message_get_header(context->request, GST_RTSP_HDR_RANGE, &value, 0) == GST_RTSP_OK)
	{
		GstRTSPTimeRange* parsed = nullptr;
		if (gst_rtsp_range_parse(value, &parsed) != GST_RTSP_OK)
			return GST_RTSP_STS_INVALID_RANGE;
		std::unique_ptr<GstRTSPTimeRange, decltype(&gst_rtsp_range_free)> range(parsed, gst_rtsp_range_free);
		GstClockTime from, to;
		if (!gst_rtsp_range_get_times(range.get(), &from, &to))
			return GST_RTSP_STS_INVALID_RANGE;
		if (range->unit == GST_RTSP_RANGE_CLOCK)
		{
			if (!GST_CLOCK_TIME_IS_VALID(from) || from < NtpEpoch || (GST_CLOCK_TIME_IS_VALID(to) && to < NtpEpoch))
				return GST_RTSP_STS_INVALID_RANGE;
			start = Time{Nano{from - NtpEpoch}};
			if (GST_CLOCK_TIME_IS_VALID(to))
				stop = Time{Nano{to - NtpEpoch}};
		}
		else if (range->unit != GST_RTSP_RANGE_NPT || from != 0 || GST_CLOCK_TIME_IS_VALID(to))
			return GST_RTSP_STS_INVALID_RANGE;
	}
	if (gst_rtsp_message_get_header(context->request, GST_RTSP_HDR_SCALE, &value, 0) == GST_RTSP_OK)
	{
		const std::string_view scale(value);
		if (scale != "1" && scale != "1.0")
			return GST_RTSP_STS_OPTION_NOT_SUPPORTED;
	}

	const auto now = archive->Now();
	for (const auto& interval : entry->intervals)
	{
		const auto begin = std::max(interval.start, start);
		const auto end = std::min(interval.end.value_or(now), stop);
		if (end <= begin)
			continue;
		const auto seconds = std::chrono::duration<double>(end - begin).count();
		const auto range = std::format("npt=0-{:.9f}", seconds);
		gst_rtsp_message_remove_header(context->request, GST_RTSP_HDR_RANGE, -1);
		gst_rtsp_message_add_header(context->request, GST_RTSP_HDR_RANGE, range.c_str());
		auto* root = gst_rtsp_media_get_element(context->media);
		const auto offset = std::chrono::duration_cast<Nano>(begin.time_since_epoch()).count() + NtpEpoch;
		for (const auto* name : {"archive_stamp", "archive_audio_stamp", "archive_metadata_stamp"})
			if (auto* stamp = gst_bin_get_by_name_recurse_up(GST_BIN(root), name))
			{
				g_object_set(stamp, "ntp-offset", static_cast<guint64>(offset), nullptr);
				gst_object_unref(stamp);
			}
		gst_object_unref(root);
		return GST_RTSP_STS_OK;
	}
	return GST_RTSP_STS_INVALID_RANGE;
}
struct ArchiveMediaFactory
{
	GstRTSPMediaFactory parent;
};
struct ArchiveMediaFactoryClass
{
	GstRTSPMediaFactoryClass parent;
};
G_DEFINE_TYPE(ArchiveMediaFactory, archive_media_factory, GST_TYPE_RTSP_MEDIA_FACTORY)

void archive_media_factory_init(ArchiveMediaFactory*)
{
}

GstElement* CreateMedia(GstRTSPMediaFactory* factory, const GstRTSPUrl*)
{
	const auto& tracks =
			*static_cast<const std::vector<std::string>*>(g_object_get_data(G_OBJECT(factory), "onvif-archive-tracks"));
	auto* root = gst_bin_new(nullptr);
	for (std::size_t index = 0; index < tracks.size(); ++index)
	{
		GError* error = nullptr;
		const auto description = tracks[index] + " ! queue";
		auto* track = gst_parse_bin_from_description(description.c_str(), TRUE, &error);
		if (error || !track)
		{
			g_clear_error(&error);
			if (track)
				gst_object_unref(track);
			gst_object_unref(root);
			return nullptr;
		}
		gst_object_set_name(GST_OBJECT(track), ("pay" + std::to_string(index)).c_str());
		gst_bin_add(GST_BIN(root), track);
	}
	return GST_ELEMENT(root);
}

void archive_media_factory_class_init(ArchiveMediaFactoryClass* klass)
{
	GST_RTSP_MEDIA_FACTORY_CLASS(klass)->create_element = CreateMedia;
}

} // namespace

GstRTSPMediaFactory* ArchiveFactory(const RecordingArchive::Entry& entry)
{
	const auto& video = entry.source.video;
	auto profile = video.get<std::string>("Profile");
	std::ranges::transform(profile, profile.begin(), [](unsigned char letter) { return std::tolower(letter); });
	auto tracks = std::make_unique<std::vector<std::string>>();
	tracks->push_back(std::format(
			"videotestsrc ! video/x-raw,format=I420,width={},height={},framerate={}/1 ! timeoverlay ! "
			"x264enc speed-preset=ultrafast tune=zerolatency bitrate={} key-int-max={} ! video/x-h264,profile={} ! "
			"h264parse config-interval=-1 ! rtph264pay pt=96 config-interval=-1 ! "
			"rtponviftimestamp name=archive_stamp ntp-offset=0 drop-out-of-segment=false",
			video.get<int>("Resolution.Width"), video.get<int>("Resolution.Height"),
			static_cast<int>(video.get<double>("RateControl.FrameRateLimit")), video.get<int>("RateControl.BitrateLimit"),
			video.get<int>("GovLength"), profile));
	if (entry.source.audio)
	{
		const auto& audio = *entry.source.audio;
		const auto rate = audio.get<unsigned>("SampleRate") * 1000;
		auto setup = utility::AudioSetupFactory().AudioSetup(audio.get<std::string>("Encoding"),
																												 audio.get<unsigned>("Bitrate") * 1000, rate);
		tracks->push_back(std::format("audiotestsrc ! audioconvert ! audioresample ! audio/x-raw,rate={} ! {} ! "
																	"{} pt={} ! rtponviftimestamp name=archive_audio_stamp "
											"ntp-offset=0 drop-out-of-segment=false",
																	rate, setup->Encoding(), setup->PayloadPluginName(),
																	setup->PayloadNum() < 96 ? setup->PayloadNum() : 97));
	}
	if (entry.source.metadata)
		tracks->push_back("appsrc name=recorded_metadata format=time stream-type=seekable "
											"caps=application/x-onvif-metadata ! rtponvifmetadatapay pt=107 ! rtponviftimestamp "
											"name=archive_metadata_stamp "
									"ntp-offset=0 drop-out-of-segment=false");
	auto* factory = GST_RTSP_MEDIA_FACTORY(g_object_new(archive_media_factory_get_type(), nullptr));
	g_object_set_data_full(
			G_OBJECT(factory), "onvif-archive-tracks", tracks.release(),
			+[](gpointer data) { delete static_cast<std::vector<std::string>*>(data); });
	gst_rtsp_media_factory_set_eos_shutdown(factory, TRUE);
	g_signal_connect_data(
			factory, "media-configure", G_CALLBACK((+[](GstRTSPMediaFactory*, GstRTSPMedia* media, gpointer data) {
				g_object_set_data_full(G_OBJECT(media), "onvif-archive-recording", g_strdup(static_cast<const char*>(data)),
															 g_free);
				ConfigureMetadata(media);
			})),
			g_strdup(entry.token.c_str()), +[](gpointer data, GClosure*) { g_free(data); }, G_CONNECT_DEFAULT);
	return factory;
}

void AttachArchiveReplay(GstRTSPServer* server, Archive archive)
{
	g_signal_connect_data(
			server, "client-connected", G_CALLBACK((+[](GstRTSPServer*, GstRTSPClient* client, gpointer data) {
				g_signal_connect_data(
						client, "pre-play-request", G_CALLBACK(ReplayRange), new Archive(*static_cast<Archive*>(data)),
						+[](gpointer data, GClosure*) { delete static_cast<Archive*>(data); }, G_CONNECT_DEFAULT);
			})),
			new Archive(std::move(archive)), +[](gpointer data, GClosure*) { delete static_cast<Archive*>(data); },
			G_CONNECT_DEFAULT);
}
} // namespace osrv::rtsp
