#include "RtspServer.h"
#include "Logger.h"
#include "Server.h"
#include "rtsp/MetadataStream.h"
#include "utility/MetadataConfiguration.h"
#include <gst/video/video-event.h>
#include <mutex>
#include <map>
#include <vector>

#include "../utility/AudioSourceReader.h"
#include "include/MediaFormats.h"

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

GST_DEBUG_CATEGORY_STATIC(onvif_server_debug);
#define GST_CAT_DEFAULT (onvif_server_debug)

#define MAKE_AND_ADD(var, pipe, name, label, elem_name)                                                                \
	G_STMT_START                                                                                                         \
	{                                                                                                                    \
		if (G_UNLIKELY(!(var = (gst_element_factory_make(name, elem_name)))))                                              \
		{                                                                                                                  \
			GST_ERROR("Could not create element %s", name);                                                                  \
			goto label;                                                                                                      \
		}                                                                                                                  \
		if (G_UNLIKELY(!gst_bin_add(GST_BIN_CAST(pipe), var)))                                                             \
		{                                                                                                                  \
			GST_ERROR("Could not add element %s", name);                                                                     \
			goto label;                                                                                                      \
		}                                                                                                                  \
	}                                                                                                                    \
	G_STMT_END

namespace osrv::rtsp
{

struct _ReplayBin
{
	GstBin parent;

	GstEvent* incoming_seek;
	GstEvent* outgoing_seek;
	GstClockTime trickmode_interval;

	GstSegment segment;
	const GstSegment* incoming_segment;
	gboolean sent_segment;
	GstClockTime ts_offset;
	gint64 remainder;
	GstClockTime min_pts;
};

G_DEFINE_TYPE(ReplayBin, replay_bin, GST_TYPE_BIN);

static void replay_bin_init(ReplayBin* self)
{
	self->incoming_seek = NULL;
	self->outgoing_seek = NULL;
	self->trickmode_interval = 0;
	self->ts_offset = 0;
	self->sent_segment = FALSE;
	self->min_pts = GST_CLOCK_TIME_NONE;
}

static void replay_bin_class_init(ReplayBinClass* klass)
{
}

static GstElement* replay_bin_new(void)
{
	return GST_ELEMENT(g_object_new(replay_bin_get_type(), NULL));
}

static GstElement* create_replay_bin(GstElement* parent)
{
	GstElement *ret, *src, *timestampOSD, *enc;
	GstPad* ghost = nullptr;
	GstPad* padTmp = nullptr;

	ret = replay_bin_new();
	if (!gst_bin_add(GST_BIN(parent), ret))
	{
		gst_object_unref(ret);
		goto fail;
	}

	MAKE_AND_ADD(src, ret, "videotestsrc", fail, NULL);
	MAKE_AND_ADD(timestampOSD, ret, "timeoverlay", fail, NULL);
	MAKE_AND_ADD(enc, ret, "x264enc", fail, NULL);

	static const int RUNNING_TIME = 2;
	g_object_set(timestampOSD, "time-mode", RUNNING_TIME, NULL);

	padTmp = gst_element_get_static_pad(enc, "src");
	if (!gst_element_add_pad(ret, gst_ghost_pad_new("src", padTmp)))
		goto fail;
	gst_object_unref(padTmp);

	if (!gst_element_link_many(src, timestampOSD, enc, NULL))
		goto fail;

done:
	return ret;

fail:
	ret = NULL;
	goto done;
}

struct _OnvifFactory
{
	GstRTSPOnvifMediaFactory parent;
};

G_DEFINE_TYPE(OnvifFactory, onvif_factory, GST_TYPE_RTSP_MEDIA_FACTORY);

static void onvif_factory_init(OnvifFactory* factory)
{
}

static GstElement* onvif_factory_create_element(GstRTSPMediaFactory* factory, const GstRTSPUrl* url)
{
	GstElement *replay_bin, *q1, /** parse,*/ *pay, *onvifts, *q2;
	GstElement* ret = gst_bin_new(NULL);
	GstElement* pbin = gst_bin_new("pay0");
	GstPad *sinkpad, *srcpad;

	if (!(replay_bin = create_replay_bin(ret)))
		goto fail;

	MAKE_AND_ADD(q1, pbin, "queue", fail, NULL);
	MAKE_AND_ADD(pay, pbin, "rtph264pay", fail, NULL);
	MAKE_AND_ADD(onvifts, pbin, "rtponviftimestamp", fail, NULL);
	MAKE_AND_ADD(q2, pbin, "queue", fail, NULL);

	gst_bin_add(GST_BIN(ret), pbin);

	if (!gst_element_link_many(q1, /*parse,*/ pay, onvifts, q2, NULL))
		goto fail;

	sinkpad = gst_element_get_static_pad(q1, "sink");
	gst_element_add_pad(pbin, gst_ghost_pad_new("sink", sinkpad));
	gst_object_unref(sinkpad);

	if (!gst_element_link(replay_bin, pbin))
		goto fail;

	srcpad = gst_element_get_static_pad(q2, "src");
	gst_element_add_pad(pbin, gst_ghost_pad_new("src", srcpad));
	gst_object_unref(srcpad);

	g_object_set(onvifts, "set-t-bit", TRUE, "set-e-bit", TRUE, "ntp-offset", G_GUINT64_CONSTANT(0),
							 "drop-out-of-segment", FALSE, NULL);

	gst_element_set_clock(onvifts, gst_system_clock_obtain());

done:
	return ret;

fail:
	gst_object_unref(ret);
	ret = NULL;
	goto done;
}

static void onvif_factory_class_init(OnvifFactoryClass* klass)
{
	GstRTSPMediaFactoryClass* mf_class = GST_RTSP_MEDIA_FACTORY_CLASS(klass);

	mf_class->create_element = onvif_factory_create_element;
}

static GstRTSPMediaFactory* onvif_factory_new(void)
{
	GstRTSPMediaFactory* result;

	result = GST_RTSP_MEDIA_FACTORY(g_object_new(onvif_factory_get_type(), NULL));

	return result;
}
} // namespace osrv::rtsp

gchar* check_requirements_callback(GstRTSPClient* self, GstRTSPContext* ctx, char** arr, gpointer user_data)
{
	// TODO implement correct check
	GString* unsupported = g_string_new("");

	return g_string_free(unsupported, FALSE);
}

namespace
{
using JournalHandle = std::shared_ptr<osrv::SystemJournal>;
void release_journal(gpointer data, GClosure*)
{
	delete static_cast<JournalHandle*>(data);
}
struct ClientJournal
{
	JournalHandle journal;
	std::string peer;
};
void release_client_journal(gpointer data, GClosure*)
{
	delete static_cast<ClientJournal*>(data);
}
void journal_client_closed(GstRTSPClient*, gpointer data)
{
	auto& client = *static_cast<ClientJournal*>(data);
	client.journal->Add("info", "client", "RTSP", "RTSP client disconnected: " + client.peer);
}
} // namespace

void client_connected_callback(GstRTSPServer* self, GstRTSPClient* object, gpointer user_data)
{
	if (user_data)
	{
		auto journal = *static_cast<JournalHandle*>(user_data);
		auto connection = gst_rtsp_client_get_connection(object);
		const char* peer = connection ? gst_rtsp_connection_get_ip(connection) : nullptr;
		journal->Add("info", "client", "RTSP", std::string("RTSP client connected: ") + (peer ? peer : "unknown"));
		g_signal_connect_data(object, "closed", G_CALLBACK(journal_client_closed),
													new ClientJournal{journal, peer ? peer : "unknown"}, release_client_journal,
													GConnectFlags(0));
	}
	g_signal_connect(object, "check-requirements", G_CALLBACK(check_requirements_callback), NULL);
	g_signal_connect(object, "play-request", G_CALLBACK(osrv::rtsp::MetadataStreams::OnPlay), nullptr);
}

namespace osrv
{
namespace rtsp
{
struct Server::Profiles
{
	struct Entry
	{
		std::string identity;
		std::vector<std::shared_ptr<GWeakRef>> media;
	};
	std::mutex mutex;
	std::map<std::string, Entry> entries;
};

static std::string metadata_branch(int index)
{
	return " appsrc name=metadata_src is-live=true format=time block=false max-bytes=262144 "
		"caps=application/x-onvif-metadata ! rtponvifmetadatapay name=pay" + std::to_string(index) + " pt=107 mtu=1200 ";
}

Server::Server(ILogger* logger, ServerConfigs& server_configs, const AudioInfo& ainfo,
							 std::shared_ptr<event::IEventSource> event_source, std::shared_ptr<scene::ISource> scenes)
		: logger_(logger), event_source_(std::move(event_source)), server_configs_(&server_configs)
{
	gst_init(NULL, NULL);

	if (server_configs_->rtsp_metadata_enabled_)
	{
		if (!event_source_) throw std::runtime_error("RTSP metadata requires an event source");
		auto plugin = gst_element_factory_find("rtponvifmetadatapay");
		if (!plugin) throw std::runtime_error("RTSP metadata enabled but rtponvifmetadatapay is unavailable");
		gst_object_unref(plugin);
	}
	context_ = g_main_context_new();
	loop_ = g_main_loop_new(context_, FALSE);
	if (server_configs_->rtsp_metadata_enabled_)
		metadata_ = std::make_unique<MetadataStreams>(event_source_, *logger_, context_, std::move(scenes));

	/* create a server instance */
	server_ = gst_rtsp_server_new();
	g_object_set(server_, "service", "8554", NULL);
	gst_rtsp_server_set_address(server_, server_configs_->ipv4_address_.c_str());
	gst_rtsp_server_set_service(server_, server_configs_->rtsp_port_.c_str());

	g_signal_connect_data(server_, "client-connected", G_CALLBACK(client_connected_callback),
		new JournalHandle(server_configs_->journal), release_journal, GConnectFlags(0));

	mounts_ = gst_rtsp_server_get_mount_points(server_);

	factoryHighStream_ = gst_rtsp_media_factory_new();

	std::stringstream mediadescr;

	auto asetup = utility::AudioSetupFactory().AudioSetup(ainfo.codec, ainfo.bitrate, ainfo.samplerate);
	test_audio_pipeline_ = " audiotestsrc is-live=1 ! audioconvert ! audioresample ! audio/x-raw,rate=8000 ! " +
		asetup->Encoding() + " ! " + asetup->PayloadPluginName() + " name=pay1 pt=" + std::to_string(asetup->PayloadNum());

	if (server_configs_->rtsp_streaming_file_.empty())
	{
		mediadescr << "("
							 << " videotestsrc is-live=1 ! timeoverlay ! video/x-raw,width=640,height=320 ! x264enc ! rtph264pay "
									"name=pay0 pt=97"
							 << " audiotestsrc is-live=1 ! audioconvert ! audioresample ! audio/x-raw,rate=8000 ! "
							 << asetup->Encoding() << " ! " << asetup->PayloadPluginName()
							 << " name=pay1 pt= " << std::to_string(asetup->PayloadNum());
	}
	else
	{
		mediadescr << "("
							 << " multifilesrc location=\"" << server_configs_->rtsp_streaming_file_
							 << "\" loop=true stop-index=-1 ! decodebin name=demuxer"
							 << " demuxer. ! autovideoconvert ! x264enc ! rtph264pay name=pay0 pt=96"
							 << " demuxer. ! audioconvert ! audioresample ! audio/x-raw,rate=8000 ! " << asetup->Encoding() << " ! "
							 << asetup->PayloadPluginName() << " name=pay1 pt= " << std::to_string(asetup->PayloadNum());
	}
	high_pipeline_ = mediadescr.str();
	auto audio_start = high_pipeline_.find(server_configs_->rtsp_streaming_file_.empty() ? " audiotestsrc" : " demuxer. ! audioconvert");
	audio_pipeline_ = high_pipeline_.substr(audio_start);
	high_pipeline_.resize(audio_start);
	mediadescr << " )";

	gst_rtsp_media_factory_set_launch(factoryHighStream_, mediadescr.str().c_str());
	gst_rtsp_media_factory_set_shared(factoryHighStream_, TRUE);

	factoryLowStream_ = gst_rtsp_media_factory_new();
	std::string low_pipeline = "(videotestsrc is-live=1 ! timeoverlay ! video/x-raw,width=640,height=320 ! "
			"x264enc ! rtph264pay name=pay0 pt=96 ";
	low_pipeline_ = low_pipeline;
	profiles_ = std::make_shared<Profiles>();
	gst_rtsp_media_factory_set_launch(factoryLowStream_, (low_pipeline + " )").c_str());
	gst_rtsp_media_factory_set_shared(factoryLowStream_, TRUE);

	replayFactory_ = onvif_factory_new();
	gst_rtsp_media_factory_set_media_gtype(replayFactory_, GST_TYPE_RTSP_ONVIF_MEDIA);

	auto pool = gst_rtsp_address_pool_new();
	if (!gst_rtsp_address_pool_add_range(pool, GST_RTSP_ADDRESS_POOL_ANY_IPV4,
		GST_RTSP_ADDRESS_POOL_ANY_IPV4, server_configs_->rtp_port_min_, server_configs_->rtp_port_max_, 0))
	{
		g_object_unref(pool);
		throw std::runtime_error("Cannot configure RTP/RTCP UDP port range");
	}
	for (auto factory : {factoryHighStream_, factoryLowStream_, replayFactory_})
	{
		gst_rtsp_media_factory_set_address_pool(factory, pool);
	}
	g_object_unref(pool);

	gst_rtsp_mount_points_add_factory(mounts_, "/Live&HighStream", factoryHighStream_);
	gst_rtsp_mount_points_add_factory(mounts_, "/Live&LowStream", factoryLowStream_);
	gst_rtsp_mount_points_add_factory(mounts_, "/Recording0", replayFactory_);

	g_object_unref(mounts_);
};

void Server::ConfigureProfile(const std::string& path, const std::string& encoder,
															std::shared_ptr<const metadata::Settings> settings, bool audio)
{
	if (settings && !metadata_)
		throw std::logic_error("Metadata transport is disabled");
	std::lock_guard lock(profiles_->mutex);
	auto identity = encoder + (audio ? ":audio:" : ":") + (settings ? settings->identity : "");
	auto& entry = profiles_->entries[path];
	if (entry.identity == identity)
		return;
	bool high = encoder == "VideoEncoderToken0";
	auto pipeline = encoder.empty() ? "( " : high ? high_pipeline_ : low_pipeline_;
	if (audio)
	{
		auto branch = high ? audio_pipeline_ : test_audio_pipeline_;
		if (encoder.empty())
			branch.replace(branch.find("name=pay1"), 9, "name=pay0");
		pipeline += branch;
	}
	if (settings)
		pipeline += metadata_branch((encoder.empty() ? 0 : 1) + (audio ? 1 : 0));
	pipeline += " )";
	auto factory = gst_rtsp_media_factory_new();
	gst_rtsp_media_factory_set_launch(factory, pipeline.c_str());
	gst_rtsp_media_factory_set_shared(factory, TRUE);
	if (settings)
		metadata_->Attach(factory, settings);
	auto pool = gst_rtsp_media_factory_get_address_pool(factoryHighStream_);
	gst_rtsp_media_factory_set_address_pool(factory, pool);
	g_object_unref(pool);
	using Attachment = std::pair<std::shared_ptr<Profiles>, std::string>;
	g_signal_connect_data(
			factory, "media-configure", G_CALLBACK((+[](GstRTSPMediaFactory*, GstRTSPMedia* media, gpointer data) {
				auto& [profiles, path] = *static_cast<Attachment*>(data);
				auto weak = std::shared_ptr<GWeakRef>(new GWeakRef, [](auto ref) {
					g_weak_ref_clear(ref);
					delete ref;
				});
				g_weak_ref_init(weak.get(), media);
				std::lock_guard lock(profiles->mutex);
				auto& streams = profiles->entries[path].media;
				std::erase_if(streams, [](const auto& weak) {
					auto object = g_weak_ref_get(weak.get());
					if (object)
						g_object_unref(object);
					return !object;
				});
				streams.push_back(std::move(weak));
			})),
			new Attachment(profiles_, path), +[](gpointer data, GClosure*) { delete static_cast<Attachment*>(data); },
			G_CONNECT_DEFAULT);
	auto mounts = gst_rtsp_server_get_mount_points(server_);
	// Mount replacement keeps already prepared media alive for their existing sessions.
	gst_rtsp_mount_points_add_factory(mounts, path.c_str(), factory);
	g_object_unref(mounts);
	entry.identity = identity;
}

void Server::RetainProfiles(const std::vector<std::string>& paths)
{
	std::lock_guard lock(profiles_->mutex);
	auto mounts = gst_rtsp_server_get_mount_points(server_);
	std::erase_if(profiles_->entries, [&](const auto& entry) {
		if (std::ranges::find(paths, entry.first) != paths.end())
			return false;
		gst_rtsp_mount_points_remove_factory(mounts, entry.first.c_str());
		return true;
	});
	g_object_unref(mounts);
}

void Server::SynchronizeProfile(const std::string& path)
{
	std::vector<GstRTSPMedia*> streams;
	{
		std::lock_guard lock(profiles_->mutex);
		auto entry = profiles_->entries.find(path);
		if (entry == profiles_->entries.end())
			return;
		for (const auto& weak : entry->second.media)
			if (auto media = g_weak_ref_get(weak.get()))
				streams.push_back(GST_RTSP_MEDIA(media));
	}
	for (auto media : streams)
	{
		MetadataStreams::Synchronize(media);
		auto element = gst_rtsp_media_get_element(media);
		gst_element_send_event(element, gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, TRUE, 0));
		gst_object_unref(element);
		g_object_unref(media);
	}
}

Server::~Server()
{
	if (worker_thread_.joinable())
	{
		// An attached source always runs on the worker, even when stop races its
		// initial entry to g_main_loop_run(). A direct quit could lose that race.
		auto stop = g_idle_source_new();
		g_source_set_callback(
				stop,
				+[](gpointer data) -> gboolean {
					auto self = static_cast<Server*>(data);
					self->Shutdown();
					g_main_loop_quit(self->loop_);
					return G_SOURCE_REMOVE;
				},
				this, nullptr);
		g_source_attach(stop, context_);
		g_source_unref(stop);
		worker_thread_.join();
	}
	else
		Shutdown();
	g_object_unref(server_);
	g_main_loop_unref(loop_);
	g_main_context_unref(context_);
}

void Server::Shutdown()
{
	if (session_cleanup_)
	{
		g_source_destroy(session_cleanup_);
		g_source_unref(session_cleanup_);
		session_cleanup_ = nullptr;
	}
	if (listener_)
	{
		if (auto source = g_main_context_find_source_by_id(context_, listener_)) g_source_destroy(source);
		listener_ = 0;
	}
	if (metadata_) metadata_->Stop();
	auto clients = gst_rtsp_server_client_filter(server_,
			+[](GstRTSPServer*, GstRTSPClient*, gpointer) { return GST_RTSP_FILTER_REF; }, nullptr);
	for (auto entry = clients; entry; entry = entry->next)
	{
		gst_rtsp_client_close(GST_RTSP_CLIENT(entry->data));
		g_object_unref(entry->data);
	}
	g_list_free(clients);
	auto pool = gst_rtsp_server_get_session_pool(server_);
	gst_rtsp_session_pool_filter(pool,
			+[](GstRTSPSessionPool*, GstRTSPSession*, gpointer) { return GST_RTSP_FILTER_REMOVE; }, nullptr);
	g_object_unref(pool);
}

unsigned short Server::Port() const
{
	return static_cast<unsigned short>(gst_rtsp_server_get_bound_port(server_));
}

void Server::run()
{
	if (worker_thread_.joinable()) throw std::logic_error("RTSP server is already running");
	listener_ = gst_rtsp_server_attach(server_, context_);
	if (!listener_) throw std::runtime_error("Cannot bind RTSP server listener");
	session_cleanup_ = g_timeout_source_new_seconds(5);
	g_source_set_callback(session_cleanup_, +[](gpointer data) -> gboolean {
		gst_rtsp_session_pool_cleanup(GST_RTSP_SESSION_POOL(data));
		return G_SOURCE_CONTINUE;
	}, gst_rtsp_server_get_session_pool(server_), +[](gpointer data) { g_object_unref(data); });
	g_source_attach(session_cleanup_, context_);
	logger_->Info("RTSP Server is running. Available URIs:\n" + server_configs_->RtspAddress() +
			"Live&HighStream\n" + server_configs_->RtspAddress() + "Live&LowStream\n" +
			server_configs_->RtspAddress() + "Recording0");
	worker_thread_ = std::thread([this]() {
		g_main_context_push_thread_default(context_);
		g_main_loop_run(loop_);
		g_main_context_pop_thread_default(context_);
	});
}
} // namespace rtsp
} // namespace osrv
