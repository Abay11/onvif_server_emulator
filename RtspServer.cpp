#include "Server.h"
#include "RtspServer.h"
#include "Logger.h"

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

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

		if (!gst_element_link(src, enc))
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

	static GstElement* onvif_factory_create_element(GstRTSPMediaFactory* factory,
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

	static void onvif_factory_class_init(OnvifFactoryClass* klass)
	{
		GstRTSPMediaFactoryClass* mf_class = GST_RTSP_MEDIA_FACTORY_CLASS(klass);

		mf_class->create_element = onvif_factory_create_element;
	}

	static GstRTSPMediaFactory* onvif_factory_new(void)
	{
		GstRTSPMediaFactory* result;

		result =
			GST_RTSP_MEDIA_FACTORY(g_object_new(onvif_factory_get_type(), NULL));

		return result;
	}
}

gchar* check_requirements_callback(GstRTSPClient* self,	GstRTSPContext* ctx, char** arr, gpointer user_data)
{
	// TODO implement correct check
	GString* unsupported = g_string_new("");

	return g_string_free(unsupported, FALSE);
}

void client_connected_callback(GstRTSPServer* self,
	GstRTSPClient* object,
	gpointer user_data)
{
	g_signal_connect(object, "check-requirements", G_CALLBACK(check_requirements_callback), NULL);
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
			gst_rtsp_server_set_address(server_, server_configs_->ipv4_address_.c_str());
			gst_rtsp_server_set_service(server_, server_configs_->rtsp_port_.c_str());

			g_signal_connect(server_, "client-connected", G_CALLBACK(client_connected_callback), NULL);

			mounts_ = gst_rtsp_server_get_mount_points(server_);

			factoryHighStream_ = gst_rtsp_media_factory_new();
			gst_rtsp_media_factory_set_launch(factoryHighStream_,
				"(videotestsrc is-live=1 ! timeoverlay ! video/x-raw,width=1280,height=720 ! x264enc ! rtph264pay name=pay0 pt=96 )");
			gst_rtsp_media_factory_set_shared(factoryHighStream_, TRUE);
			
			factoryLowStream_ = gst_rtsp_media_factory_new();
			gst_rtsp_media_factory_set_launch(factoryLowStream_,
				"(videotestsrc is-live=1 ! timeoverlay ! video/x-raw,width=640,height=320 ! x264enc ! rtph264pay name=pay0 pt=96 )");
			gst_rtsp_media_factory_set_shared(factoryLowStream_, TRUE);

			replayFactory_ = onvif_factory_new();
			gst_rtsp_media_factory_set_media_gtype(replayFactory_, GST_TYPE_RTSP_ONVIF_MEDIA);
			
			gst_rtsp_mount_points_add_factory(mounts_, "/Live&HighStream", factoryHighStream_);
			gst_rtsp_mount_points_add_factory(mounts_, "/Live&LowStream", factoryLowStream_);
			gst_rtsp_mount_points_add_factory(mounts_, "/Recording0", replayFactory_);

			g_object_unref(mounts_);
		};

		Server::~Server()
		{
			if (!worker_thread_)
				return;

			try
			{
				worker_thread_->join();
			}
			catch (const std::exception&) {}
		};

		void Server::run()
		{
			//TODO: stop GSt main loop by signal
			worker_thread_ = new std::thread(
				[this]() {

					/* attach the server to the default maincontext */
					gst_rtsp_server_attach(server_, NULL);

					//gchar* service = gst_rtsp_server_get_service(server_);
					int actually_used_port = gst_rtsp_server_get_bound_port(server_);
					if (stoi(server_configs_->rtsp_port_) != actually_used_port)
						logger_->Warn("RTSP Server port is binding on: " + std::to_string(actually_used_port));

					gchar* server_address = gst_rtsp_server_get_address(server_);
					std::stringstream uris;
					uris << "rtsp://" << server_address << ":" << actually_used_port << "/Live&HighStream";
					uris << "\nrtsp://" << server_address << ":" << actually_used_port << "/Live&LowStream";
					uris << "\nrtsp://" << server_address << ":" << actually_used_port << "/Recording0";

					g_free(server_address);

					logger_->Info("RTSP Server is running. Available URIs:\n" + uris.str());

					g_main_loop_run(loop_);
				}
			);
		};
	}
}