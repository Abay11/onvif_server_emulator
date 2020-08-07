#include "Slave.h"
#include "Server.h"

namespace osrv
{

	Slave::Slave(socket_t&& s, ServerBase& parent)
		: socket_(std::move(s)),
		parent_(parent),
		log_(parent.getLogger())
	{
	}

	Slave::~Slave()
	{
		boost::system::error_code ec;
		using namespace boost::asio;
		socket_.shutdown(socket_base::shutdown_type::shutdown_both, ec);
		socket_.close(ec);

		log_.Debug("Destroying a slave");
	}

	void Slave::do_read()
	{
		socket_.async_read_some(
			boost::asio::buffer(buffer_),
			[this](boost::system::error_code ec, std::size_t bytes_transferred)
			{

				if (ec)
				{
					log_.Error(std::string("Can't read a client: ") + ec.message());
					parent_.FinalizeSlave(shared_from_this());
					return;
				}

				std::string info("Read some data has a size: ");
				info += std::to_string(bytes_transferred);
				log_.Info(info);

				do_read();
			}
		);
	}

}