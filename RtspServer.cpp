#include "RtspServer.h"
#include "Logger.h"

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <stdexcept>
#include <sstream>

namespace osrv
{
	namespace rtsp
	{
		Server::Server(ILogger* logger, const char* addr, const char* port)
			:logger_(logger),
			rtsp_addr_(addr),
			rtsp_port_(port)
		{
			gst_init(NULL, NULL);
					
			loop_ = g_main_loop_new(NULL, FALSE);

			server_ = gst_rtsp_server_new();

			//FIX_ME: address and port are hardcoded
			gst_rtsp_server_set_address(server_, addr);
			gst_rtsp_server_set_service(server_, port);

			mounts = gst_rtsp_server_get_mount_points(server_);
			factoryHighStream = gst_rtsp_media_factory_new();
			gst_rtsp_media_factory_set_launch(factoryHighStream,
				"(videotestsrc is-live=1 ! video/x-raw,width=1280,height=720 ! x264enc ! rtph264pay name=pay0 pt=96 )");
			
			factoryLowStream = gst_rtsp_media_factory_new();
			gst_rtsp_media_factory_set_launch(factoryLowStream,
				"(videotestsrc is-live=1 ! video/x-raw,width=640,height=480! x264enc ! rtph264pay name=pay0 pt=96 )");

			gst_rtsp_media_factory_set_shared(factoryHighStream, TRUE);
			gst_rtsp_media_factory_set_shared(factoryLowStream, TRUE);

			//FIX_ME: path is hardcoded
			gst_rtsp_mount_points_add_factory(mounts, "/Live&HighStream", factoryHighStream);
			gst_rtsp_mount_points_add_factory(mounts, "/Live&LowStream", factoryLowStream);

			g_object_unref(mounts);
		};

		Server::~Server()
		{
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

					int actually_used_port = gst_rtsp_server_get_bound_port(server_);
					if (stoi(rtsp_port_) != actually_used_port)
						logger_->Warn("RTSP Server port is binding on: " + std::to_string(actually_used_port));

					gchar* server_address = gst_rtsp_server_get_address(server_);
					std::stringstream first_uri;
					first_uri << "rtsp://" << server_address << ":" << actually_used_port << "/Live&HighStream";
					std::stringstream second_uri;
					second_uri << "rtsp://" << server_address << ":" << actually_used_port << "/Live&LowStream";

					g_free(server_address);

					logger_->Info("RTSP Server is running. URIs:\n" + first_uri.str()
						+ "\n" + second_uri.str());
					g_main_loop_run(loop_);
				}
			);
		};
	}
}