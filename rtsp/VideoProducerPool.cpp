#include "VideoProducerPool.h"
#include "../Logger.h"

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video-event.h>

#include <map>
#include <mutex>
#include <stdexcept>

namespace osrv::rtsp
{
namespace
{
class Producer
{
public:
	explicit Producer(const std::string& launch)
	{
		GError* error = nullptr;
		pipeline_ =
				gst_parse_launch((launch + " ! appsink name=encoded sync=false max-buffers=4 drop=true").c_str(), &error);
		if (error || !pipeline_)
		{
			std::string message = error ? error->message : "Could not create video producer";
			g_clear_error(&error);
			if (pipeline_)
				gst_object_unref(pipeline_);
			throw std::runtime_error(message);
		}
		auto* sink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline_), "encoded"));
		GstAppSinkCallbacks callbacks{};
		callbacks.new_sample = +[](GstAppSink* sink, gpointer data) {
			auto* sample = gst_app_sink_pull_sample(sink);
			if (!sample)
				return GST_FLOW_FLUSHING;
			static_cast<Producer*>(data)->Publish(sample);
			gst_sample_unref(sample);
			return GST_FLOW_OK;
		};
		gst_app_sink_set_callbacks(sink, &callbacks, this, nullptr);
		gst_object_unref(sink);
	}

	~Producer()
	{
		gst_element_set_state(pipeline_, GST_STATE_NULL);
		gst_object_unref(pipeline_);
	}

	void Subscribe(GstAppSrc* source)
	{
		{
			std::lock_guard lock(mutex_);
			consumers_.try_emplace(source);
		}
		if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
		{
			Remove(source);
			throw std::runtime_error("Unable to start video producer");
		}
		auto* encoder = gst_bin_get_by_name(GST_BIN(pipeline_), "shared_encoder");
		auto* pad = gst_element_get_static_pad(encoder, "src");
		gst_pad_send_event(pad, gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, TRUE, 0));
		gst_object_unref(pad);
		gst_object_unref(encoder);
	}

	void Remove(GstAppSrc* source)
	{
		std::lock_guard lock(mutex_);
		consumers_.erase(source);
	}

private:
	struct Cursor
	{
		GstClockTime start = GST_CLOCK_TIME_NONE;
		bool needKey = true;
	};

	void Publish(GstSample* sample)
	{
		auto* input = gst_sample_get_buffer(sample);
		const auto time = GST_BUFFER_PTS(input);
		if (!GST_CLOCK_TIME_IS_VALID(time))
			return;
		const bool key = !GST_BUFFER_FLAG_IS_SET(input, GST_BUFFER_FLAG_DELTA_UNIT);
		std::lock_guard lock(mutex_);
		for (auto& [source, cursor] : consumers_)
		{
			if (gst_app_src_get_current_level_buffers(source) >= 8)
			{
				cursor.needKey = true;
				continue;
			}
			if (cursor.needKey && !key)
				continue;

			const bool discontinuity = cursor.needKey;
			if (!GST_CLOCK_TIME_IS_VALID(cursor.start))
				cursor.start = time;
			if (time < cursor.start)
				continue;

			auto* output = gst_buffer_copy(input);
			GST_BUFFER_PTS(output) = time - cursor.start;
			GST_BUFFER_DTS(output) = GST_BUFFER_DTS_IS_VALID(input) && GST_BUFFER_DTS(input) >= cursor.start
																	 ? GST_BUFFER_DTS(input) - cursor.start
																	 : GST_CLOCK_TIME_NONE;
			if (discontinuity)
			{
				gst_app_src_set_caps(source, gst_sample_get_caps(sample));
				GST_BUFFER_FLAG_SET(output, GST_BUFFER_FLAG_DISCONT);
			}
			cursor.needKey = gst_app_src_push_buffer(source, output) != GST_FLOW_OK;
		}
	}

	GstElement* pipeline_ = nullptr;
	std::mutex mutex_;
	std::map<GstAppSrc*, Cursor> consumers_;
};

struct Subscription
{
	std::shared_ptr<Producer> producer;
	GstAppSrc* source;
	~Subscription()
	{
		producer->Remove(source);
		gst_object_unref(source);
	}
};
} // namespace

struct VideoProducerPool::State
{
	explicit State(ILogger& logger) : logger(logger)
	{
	}

	std::shared_ptr<Producer> Acquire(const std::string& launch)
	{
		std::lock_guard lock(mutex);
		std::erase_if(producers, [](const auto& item) { return item.second.expired(); });
		auto& entry = producers[launch];
		auto producer = entry.lock();
		if (!producer)
		{
			producer = std::make_shared<Producer>(launch);
			entry = producer;
			logger.Debug("Created live video producer (" + std::to_string(producers.size()) + " active)");
		}
		return producer;
	}

	ILogger& logger;
	std::mutex mutex;
	std::map<std::string, std::weak_ptr<Producer>> producers;
};

VideoProducerPool::VideoProducerPool(ILogger& logger) : state_(std::make_shared<State>(logger))
{
}

void VideoProducerPool::Attach(GstRTSPMediaFactory* factory, std::string launch)
{
	using Binding = std::pair<std::shared_ptr<State>, std::string>;
	g_signal_connect_data(
			factory, "media-configure", G_CALLBACK((+[](GstRTSPMediaFactory*, GstRTSPMedia* media, gpointer data) {
				const auto& [state, launch] = *static_cast<Binding*>(data);
				auto* pipeline = gst_rtsp_media_get_element(media);
				auto* source = GST_APP_SRC(gst_bin_get_by_name_recurse_up(GST_BIN(pipeline), "shared_video"));
				gst_object_unref(pipeline);
				if (!source)
					return;
				try
				{
					auto producer = state->Acquire(launch);
					producer->Subscribe(source);
					g_object_set_data_full(
							G_OBJECT(media), "encoded-video-subscription", new Subscription{std::move(producer), source},
							+[](gpointer data) { delete static_cast<Subscription*>(data); });
					g_signal_connect(media, "unprepared", G_CALLBACK((+[](GstRTSPMedia* media, gpointer) {
														 g_object_set_data(G_OBJECT(media), "encoded-video-subscription", nullptr);
													 })),
													 nullptr);
				}
				catch (const std::exception& error)
				{
					state->logger.Error(error.what());
					gst_object_unref(source);
				}
			})),
			new Binding{state_, std::move(launch)}, +[](gpointer data, GClosure*) { delete static_cast<Binding*>(data); },
			G_CONNECT_DEFAULT);
}
} // namespace osrv::rtsp
