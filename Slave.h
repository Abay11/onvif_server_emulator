#pragma once

#include "Types.inl"

class Logger;

namespace osrv
{
	class ServerBase;

	class Slave : public std::enable_shared_from_this<Slave>
	{
	public:
		Slave(socket_t&& s, ServerBase& parent);
		~Slave();
		Slave(Slave&&) = delete;
		Slave(const Slave&) = delete;

		void Start()
		{
			do_read();
		}

	private:
		void do_read();

	private:

		buffer_t buffer_;
		socket_t socket_;

		ServerBase& parent_;
		Logger& log_;
	};

}