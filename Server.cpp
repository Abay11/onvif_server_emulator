#include "Server.h"
#include "Slave.h"

namespace osrv
{

void Server::run()
{
	ip::tcp::resolver resolver_(ctx_);
	boost::asio::ip::tcp::endpoint endpoint =
		*resolver_.resolve(MASTER_ADDR, MASTER_PORT).begin();

	acceptor_.open(endpoint.protocol());
	
	acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
	acceptor_.bind(endpoint);
	acceptor_.listen();
	
	log_.Info("Server is successfully started");

	do_accept();
		
	ctx_.run();
}

void Server::do_accept()
{
	using namespace std::placeholders;
	acceptor_.async_accept(std::bind(&Server::accept_handler, this, _1, _2));
}

void Server::accept_handler(boost::system::error_code ec,
			boost::asio::ip::tcp::socket socket)
{
			// Check whether the server was stopped by a signal before this
			// completion handler had a chance to run.
			if (!acceptor_.is_open())
			{
				return;
			}

			if (ec)
			{
				log_.Error("Can't accept a new client");
				return;
			}

			do_accept();

			auto it = slaves_.insert(std::make_shared<Slave>(std::move(socket), *this));
			auto& slave = *(it.first);
			slave->Start();
}

}
