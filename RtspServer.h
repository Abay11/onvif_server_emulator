#pragma once

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <string>
#include <thread>

class ILogger;

namespace osrv
{
	struct ServerConfigs;
	namespace rtsp
	{
		class Server
		{
		public:
			Server(ILogger* /*logger*/, ServerConfigs& /*server_configs*/);
			~Server();
			void run();

		private:
			GMainLoop* loop_;
			GstRTSPServer* server_;
			GstRTSPMountPoints* mounts;
			GstRTSPMediaFactory* factoryHighStream;
			GstRTSPMediaFactory* factoryLowStream;

			ServerConfigs* server_configs_ = nullptr;

			std::thread* worker_thread = nullptr;

			ILogger* logger_;
		};
	}
}