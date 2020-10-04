#pragma once

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <string>
#include <thread>

class Logger;

namespace osrv
{
	namespace rtsp
	{
		class Server
		{
		public:
			Server(Logger* logger, const char* addr = "127.0.0.1", const char* port = "8554");
			~Server();
			void run();

		private:
			GMainLoop* loop_;
			GstRTSPServer* server_;
			GstRTSPMountPoints* mounts;
			GstRTSPMediaFactory* factoryHighStream;
			GstRTSPMediaFactory* factoryLowStream;

			std::string rtsp_addr_;
			std::string rtsp_port_;

			std::thread* worker_thread = nullptr;

			Logger* logger_;
		};
	}
}