#pragma once

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

namespace osrv
{
	namespace rtsp
	{
		class Server
		{
		public:
			Server();
			~Server();
			void run();

		private:
		};
	}
}