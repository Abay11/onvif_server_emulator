#include "Server.h"
#include "RtspServer.h"
#include "Logger.h"

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <stdexcept>
#include <sstream>

GST_DEBUG_CATEGORY_STATIC(onvif_server_debug);
#define GST_CAT_DEFAULT (onvif_server_debug)




#define MAKE_AND_ADD(var, pipe, name, label, elem_name) \
G_STMT_START { \
  if (G_UNLIKELY (!(var = (gst_element_factory_make (name, elem_name))))) { \
    GST_ERROR ("Could not create element %s", name); \
    goto label; \
  } \
  if (G_UNLIKELY (!gst_bin_add (GST_BIN_CAST (pipe), var))) { \
    GST_ERROR ("Could not add element %s", name); \
    goto label; \
  } \
} G_STMT_END

namespace osrv::rtsp{

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

	static void
		replay_bin_init(ReplayBin* self)
	{
		self->incoming_seek = NULL;
		self->outgoing_seek = NULL;
		self->trickmode_interval = 0;
		self->ts_offset = 0;
		self->sent_segment = FALSE;
		self->min_pts = GST_CLOCK_TIME_NONE;
	}

	static void
		replay_bin_class_init(ReplayBinClass* klass)
	{
	}

	static GstElement*
		replay_bin_new(void)
	{
		return GST_ELEMENT(g_object_new(replay_bin_get_type(), NULL));
	}

	static GstElement*
		create_replay_bin(GstElement* parent)
	{
		GstElement* ret, *src, *demux, *enc;
		GstPad* ghost;

		ret = replay_bin_new();
		if (!gst_bin_add(GST_BIN(parent), ret)) {
			gst_object_unref(ret);
			goto fail;
		}

		MAKE_AND_ADD(src, ret, "videotestsrc", fail, NULL);
		MAKE_AND_ADD(enc, ret, "x264enc", fail, NULL);

		auto* padTmp = gst_element_get_static_pad(enc, "src");
		if (!gst_element_add_pad(ret, gst_ghost_pad_new("src", padTmp)))
			goto fail;
		gst_object_unref(padTmp);
		//ghost = gst_ghost_pad_new("src", (GstPad*)src);
		//gst_element_add_pad(ret, ghost);

		//gst_pad_set_event_function(ghost, replay_bin_event_func);
		//gst_pad_add_probe(ghost, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
		//	replay_bin_event_probe, NULL, NULL);
		//gst_pad_add_probe(ghost, GST_PAD_PROBE_TYPE_BUFFER, replay_bin_buffer_probe,
		//	NULL, NULL);
		//gst_pad_set_query_function(ghost, replay_bin_query_func);

		if (!gst_element_link(src, enc))
			goto fail;

		//g_object_set(src, "location", filename, NULL);
		//g_signal_connect(demux, "pad-added", G_CALLBACK(demux_pad_added_cb), ghost);

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

	static void
		onvif_factory_init(OnvifFactory* factory)
	{
	}

	static GstElement*
		onvif_factory_create_element(GstRTSPMediaFactory* factory,
			const GstRTSPUrl* url)
	{
		GstElement* replay_bin, * q1, /** parse,*/ * pay, *onvifts, * q2;
		GstElement* ret = gst_bin_new(NULL);
		GstElement* pbin = gst_bin_new("pay0");
		GstPad* sinkpad, * srcpad;

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

		g_object_set(onvifts, "set-t-bit", TRUE, "set-e-bit", TRUE, "ntp-offset",
			G_GUINT64_CONSTANT(0), "drop-out-of-segment", FALSE, NULL);

		gst_element_set_clock(onvifts, gst_system_clock_obtain());

	done:
		return ret;

	fail:
		gst_object_unref(ret);
		ret = NULL;
		goto done;
	}

	static void
		onvif_factory_class_init(OnvifFactoryClass* klass)
	{
		GstRTSPMediaFactoryClass* mf_class = GST_RTSP_MEDIA_FACTORY_CLASS(klass);

		mf_class->create_element = onvif_factory_create_element;
	}

	static GstRTSPMediaFactory*
		onvif_factory_new(void)
	{
		GstRTSPMediaFactory* result;

		result =
			GST_RTSP_MEDIA_FACTORY(g_object_new(onvif_factory_get_type(), NULL));

		return result;
	}
}


namespace osrv
{
	namespace rtsp
	{
		Server::Server(ILogger* logger, ServerConfigs& server_configs)
			:logger_(logger),
			server_configs_(&server_configs)
		{

			gst_init(NULL, NULL);
					
			loop_ = g_main_loop_new(NULL, FALSE);

			/* create a server instance */
			server_ = gst_rtsp_server_new();
			g_object_set(server_, "service", "8554", NULL);

			mounts_ = gst_rtsp_server_get_mount_points(server_);
			factory_ = onvif_factory_new();
			gst_rtsp_media_factory_set_media_gtype(factory_, GST_TYPE_RTSP_ONVIF_MEDIA);
			
			gst_rtsp_mount_points_add_factory(mounts_, "/test", factory_);
			g_object_unref(mounts_);

			
		};

		Server::~Server()
		{
			if (!worker_thread)
				return;

			try
			{
				worker_thread->join();
			}
			catch (const std::exception&) {}
		};

		void Server::run()
		{
			//TODO: stop GSt main loop by signal
			worker_thread = new std::thread(
				[this]() {

					/* attach the server to the default maincontext */
					gst_rtsp_server_attach(server_, NULL);
					gchar* service = gst_rtsp_server_get_service(server_);
					g_print("stream ready at rtsp://127.0.0.1:%s/test\n", service);
					g_free(service);
					g_main_loop_run(loop_);
				}
			);
		};
	}
}