#include "MetadataStream.h"

#include "../Logger.h"
#include "../onvif_services/EventSource.h"

#include <gst/app/gstappsrc.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace osrv::rtsp
{
namespace
{
constexpr const char* BridgeKey = "osrv-metadata-bridge";

class Bridge : public std::enable_shared_from_this<Bridge>
{
public:
	using Ptr = std::shared_ptr<Bridge>;
	Bridge(GstElement* element, GstElement* appsrc, std::shared_ptr<event::IEventSource> source, const ILogger& logger)
			: element_(element), appsrc_(appsrc), source_(std::move(source)), logger_(logger)
	{
		namespaces_ = source_->Namespaces();
		subscription_ = source_->Subscribe({}, false);
	}
	~Bridge()
	{
		gst_object_unref(appsrc_);
		gst_object_unref(element_);
	}
	void Start(GstRTSPMedia* media, GMainContext* context)
	{
		g_signal_connect(media, "prepared", G_CALLBACK(+[](GstRTSPMedia* media, gpointer) {
											 auto count = gst_rtsp_media_n_streams(media);
											 if (!count)
												 return;
											 auto stream = gst_rtsp_media_get_stream(media, count - 1);
											 if (auto session = gst_rtsp_stream_get_rtpsession(stream))
											 {
												 // Automatic bandwidth estimation can postpone SRs for sparse XML.
												 // Reserve control bandwidth while retaining the standard 5s minimum.
												 g_object_set(session, "rtcp-rs-bandwidth", 4000, "rtcp-rr-bandwidth", 12000, nullptr);
												 g_object_unref(session);
											 }
										 }),
										 nullptr);
		auto need = +[](GstElement*, guint, gpointer data) {
			auto self = *static_cast<Ptr*>(data);
			std::lock_guard lock(self->mutex_);
			if (self->stopped_)
				return;
			self->wants_data_ = true;
			if (!self->initialized_)
			{
				self->initialized_ = true;
				// Preparation must succeed before any event or PLAY request.
				self->Push(event::serialize_metadata({}, self->namespaces_));
			}
		};
		auto enough = +[](GstElement*, gpointer data) {
			auto self = *static_cast<Ptr*>(data);
			std::lock_guard lock(self->mutex_);
			self->wants_data_ = false;
		};
		auto destroy = +[](gpointer data, GClosure*) { delete static_cast<Ptr*>(data); };
		need_ = g_signal_connect_data(appsrc_, "need-data", G_CALLBACK(need), new Ptr(shared_from_this()), destroy,
																	G_CONNECT_DEFAULT);
		enough_ = g_signal_connect_data(appsrc_, "enough-data", G_CALLBACK(enough), new Ptr(shared_from_this()), destroy,
																		G_CONNECT_DEFAULT);
		g_signal_connect_data(media, "unprepared", G_CALLBACK(+[](GstRTSPMedia*, gpointer data) {
														auto self = *static_cast<Ptr*>(data);
														self->Stop();
													}),
													new Ptr(shared_from_this()), destroy, G_CONNECT_DEFAULT);
		g_object_set_data_full(
				G_OBJECT(media), BridgeKey, new Ptr(shared_from_this()), +[](gpointer data) {
					auto self = static_cast<Ptr*>(data);
					(*self)->Stop();
					delete self;
				});
		timer_ = g_timeout_source_new(100);
		g_source_set_callback(
				timer_,
				+[](gpointer data) -> gboolean {
					auto self = *static_cast<Ptr*>(data);
					try
					{
						self->Tick();
					}
					catch (const std::exception& error)
					{
						self->logger_.Error(std::string("RTSP metadata delivery failed: ") + error.what());
						self->Stop();
						return G_SOURCE_REMOVE;
					}
					return G_SOURCE_CONTINUE;
				},
				new Ptr(shared_from_this()), +[](gpointer data) { delete static_cast<Ptr*>(data); });
		g_source_attach(timer_, context);
	}
	void Synchronize()
	{
		synchronize_.store(true);
	}
	void Stop()
	{
		std::lock_guard lock(mutex_);
		if (stopped_)
			return;
		stopped_ = true;
		if (timer_)
		{
			g_source_destroy(timer_);
			g_source_unref(timer_);
			timer_ = nullptr;
		}
		if (need_)
			g_signal_handler_disconnect(appsrc_, std::exchange(need_, 0));
		if (enough_)
			g_signal_handler_disconnect(appsrc_, std::exchange(enough_, 0));
		subscription_->Close();
		source_.reset();
	}

private:
	void Push(const std::string& xml)
	{
		auto buffer = gst_buffer_new_allocate(nullptr, xml.size(), nullptr);
		gst_buffer_fill(buffer, 0, xml.data(), xml.size());
		GstClockTime timestamp = 0;
		if (auto clock = gst_element_get_clock(element_))
		{
			auto now = gst_clock_get_time(clock);
			auto base = gst_element_get_base_time(element_);
			if (GST_CLOCK_TIME_IS_VALID(base) && now >= base)
				timestamp = now - base;
			gst_object_unref(clock);
		}
		GST_BUFFER_PTS(buffer) = std::max(timestamp, last_pts_);
		last_pts_ = GST_BUFFER_PTS(buffer);
		GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer);
		auto result = gst_app_src_push_buffer(GST_APP_SRC(appsrc_), buffer);
		if (result != GST_FLOW_OK && result != GST_FLOW_FLUSHING)
			logger_.Warn("RTSP metadata appsrc returned " + std::string(gst_flow_get_name(result)));
		last_push_ = g_get_monotonic_time();
	}
	void Tick()
	{
		std::lock_guard lock(mutex_);
		if (stopped_ || !initialized_ || !wants_data_ ||
				gst_app_src_get_current_level_bytes(GST_APP_SRC(appsrc_)) >= 128 * 1024)
			return;
		if (synchronize_.exchange(false))
			source_->Synchronize(subscription_);
		auto batch = subscription_->Take(16);
		std::deque<event::NotificationMessage> messages;
		for (const auto& message : batch.messages)
			messages.push_back(*message);
		if (batch.dropped)
			logger_.Warn("RTSP metadata queue lost " + std::to_string(batch.dropped) + " notifications");
		if (batch.needs_synchronization)
			synchronize_.store(true);
		if (!messages.empty() || g_get_monotonic_time() - last_push_ >= G_USEC_PER_SEC)
			Push(event::serialize_metadata(messages, namespaces_));
	}

	GstElement* element_;
	GstElement* appsrc_;
	std::shared_ptr<event::IEventSource> source_;
	std::shared_ptr<event::EventSubscription> subscription_;
	std::map<std::string, std::string> namespaces_;
	const ILogger& logger_;
	std::recursive_mutex mutex_;
	std::atomic_bool synchronize_{true};
	bool stopped_ = false;
	bool initialized_ = false;
	bool wants_data_ = true;
	gulong need_ = 0;
	gulong enough_ = 0;
	GSource* timer_ = nullptr;
	gint64 last_push_ = 0;
	GstClockTime last_pts_ = 0;
};
} // namespace

struct MetadataStreams::State
{
	std::shared_ptr<event::IEventSource> source;
	const ILogger& logger;
	GMainContext* context;
	std::mutex mutex;
	bool stopped = false;
	std::vector<std::weak_ptr<Bridge>> streams;
	State(std::shared_ptr<event::IEventSource> source, const ILogger& logger, GMainContext* context)
			: source(std::move(source)), logger(logger), context(g_main_context_ref(context))
	{
	}
	~State()
	{
		g_main_context_unref(context);
	}
};

MetadataStreams::MetadataStreams(std::shared_ptr<event::IEventSource> source, const ILogger& logger,
																 GMainContext* context)
		: state_(std::make_shared<State>(std::move(source), logger, context))
{
}

MetadataStreams::~MetadataStreams()
{
	Stop();
}

void MetadataStreams::Attach(GstRTSPMediaFactory* factory)
{
	g_signal_connect_data(
			factory, "media-configure", G_CALLBACK(+[](GstRTSPMediaFactory*, GstRTSPMedia* media, gpointer data) {
				auto state = *static_cast<std::shared_ptr<State>*>(data);
				std::lock_guard lock(state->mutex);
				if (state->stopped)
					return;
				auto element = gst_rtsp_media_get_element(media);
				auto source = gst_bin_get_by_name_recurse_up(GST_BIN(element), "metadata_src");
				if (!source)
				{
					gst_object_unref(element);
					return;
				}
				Bridge::Ptr bridge;
				try
				{
					bridge = std::make_shared<Bridge>(element, source, state->source, state->logger);
					bridge->Start(media, state->context);
					std::erase_if(state->streams, [](const auto& weak) { return weak.expired(); });
					state->streams.push_back(bridge);
				}
				catch (const std::exception& error)
				{
					if (bridge)
						bridge->Stop();
					else
					{
						gst_object_unref(source);
						gst_object_unref(element);
					}
					state->logger.Error(std::string("Cannot configure RTSP metadata: ") + error.what());
				}
			}),
			new std::shared_ptr<State>(state_),
			+[](gpointer data, GClosure*) { delete static_cast<std::shared_ptr<State>*>(data); }, G_CONNECT_DEFAULT);
}

void MetadataStreams::Stop()
{
	std::lock_guard lock(state_->mutex);
	state_->stopped = true;
	for (const auto& weak : state_->streams)
		if (auto stream = weak.lock())
			stream->Stop();
	state_->streams.clear();
	state_->source.reset();
}

void MetadataStreams::OnPlay(GstRTSPClient*, GstRTSPContext* context, gpointer)
{
	auto media = context->media;
	if (!media && context->sessmedia)
		media = gst_rtsp_session_media_get_media(context->sessmedia);
	if (media)
		if (auto holder = static_cast<Bridge::Ptr*>(g_object_get_data(G_OBJECT(media), BridgeKey)))
		{
			auto count = gst_rtsp_media_n_streams(media);
			if (count && context->sessmedia && gst_rtsp_session_media_get_transport(context->sessmedia, count - 1))
				(*holder)->Synchronize();
		}
}
} // namespace osrv::rtsp
