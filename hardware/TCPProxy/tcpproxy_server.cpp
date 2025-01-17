//
// tcpproxy_server_v03.cpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2007 Arash Partow (http://www.partow.net)
// URL: http://www.partow.net/programming/tcpproxy/index.html
//
// Distributed under the Boost Software License, Version 1.0.
//
//
// Modified for Domoticz
//
//

#include "stdafx.h"
#include "tcpproxy_server.h"

#if BOOST_VERSION >= 107000
#define GET_IO_SERVICE(s) ((boost::asio::io_context&)(s).get_executor().context())
#else
#define GET_IO_SERVICE(s) ((s).get_io_context())
#endif

namespace tcp_proxy
{
	bridge::bridge(boost::asio::io_context& ios)
      : downstream_socket_(ios),
        upstream_socket_(ios)
	{
	}

	boost::asio::ip::tcp::socket& bridge::downstream_socket()
	{
		return downstream_socket_;
	}

	boost::asio::ip::tcp::socket& bridge::upstream_socket()
	{
		return upstream_socket_;
	}

	void bridge::start(const std::string& upstream_host, const std::string& upstream_port)
	{
		boost::asio::ip::tcp::endpoint end;


		boost::asio::io_context &ios= GET_IO_SERVICE(downstream_socket_);
		boost::asio::ip::tcp::resolver resolver(ios);
		boost::asio::ip::tcp::resolver::query query(upstream_host, upstream_port, boost::asio::ip::resolver_query_base::numeric_service);
		auto i = resolver.resolve(query);
		if (i == boost::asio::ip::tcp::resolver::iterator())
		{
			end=boost::asio::ip::tcp::endpoint(
				boost::asio::ip::address::from_string(upstream_host),
				(unsigned short)atoi(upstream_port.c_str()));
		}
		else
			end=*i;

		upstream_socket_.async_connect(end, [p = shared_from_this()](auto &&err) { p->handle_upstream_connect(err); });
	}

	void bridge::handle_upstream_connect(const boost::system::error_code& error)
	{
		if (!error)
		{
			upstream_socket_.async_read_some(boost::asio::buffer(upstream_data_, max_data_length),
							 [p = shared_from_this()](auto &&err, auto bytes) { p->handle_upstream_read(err, bytes); });

			downstream_socket_.async_read_some(boost::asio::buffer(downstream_data_, max_data_length),
							   [p = shared_from_this()](auto &&err, auto bytes) { p->handle_downstream_read(err, bytes); });
		}
		else
		close();
	}

	void bridge::handle_downstream_write(const boost::system::error_code& error)
	{
		if (!error)
		{
			upstream_socket_.async_read_some(boost::asio::buffer(upstream_data_, max_data_length),
							 [p = shared_from_this()](auto &&err, auto bytes) { p->handle_upstream_read(err, bytes); });
		}
		else
		close();
	}

	void bridge::handle_downstream_read(const boost::system::error_code& error,const size_t& bytes_transferred)
	{
		if (!error)
		{
			//std::unique_lock<std::mutex> lock(mutex_);
			sDownstreamData(reinterpret_cast<unsigned char*>(&downstream_data_[0]),static_cast<size_t>(bytes_transferred));
			async_write(upstream_socket_, boost::asio::buffer(downstream_data_, bytes_transferred),
				    [p = shared_from_this()](auto &&err, auto bytes) { p->handle_downstream_read(err, bytes); });
		}
		else
			close();
	}

	void bridge::handle_upstream_write(const boost::system::error_code& error)
	{
		if (!error)
		{
			downstream_socket_.async_read_some(boost::asio::buffer(downstream_data_, max_data_length),
							   [p = shared_from_this()](auto &&err, auto bytes) { p->handle_downstream_read(err, bytes); });
		}
		else
			close();
	}

	void bridge::handle_upstream_read(const boost::system::error_code& error,
							const size_t& bytes_transferred)
	{
		if (!error)
		{
			//std::unique_lock<std::mutex> lock(mutex_);
			sUpstreamData(reinterpret_cast<unsigned char*>(&upstream_data_[0]),static_cast<size_t>(bytes_transferred));

			async_write(downstream_socket_, boost::asio::buffer(upstream_data_, bytes_transferred), [p = shared_from_this()](auto &&err, auto) { p->handle_downstream_write(err); });
		}
		else
			close();
	}

	void bridge::close()
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (downstream_socket_.is_open())
		{
			downstream_socket_.close();
		}
		if (upstream_socket_.is_open())
		{
			upstream_socket_.close();
		}
	}
//Acceptor Class
	acceptor::acceptor(const std::string &local_host, unsigned short local_port, const std::string &upstream_host, const std::string &upstream_port)
		: io_context_()
		, m_bDoStop(false)
		, localhost_address(boost::asio::ip::address_v4::from_string(local_host))
		, acceptor_(io_context_, boost::asio::ip::tcp::endpoint(localhost_address, local_port))
		, upstream_host_(upstream_host)
		, upstream_port_(upstream_port)
	{

	}

	bool acceptor::accept_connections()
	{
		try
		{
			session_ = std::make_shared<bridge>(io_context_);
			session_->sDownstreamData.connect([this](auto d, auto l) { OnDownstreamData(d, l); });
			session_->sUpstreamData.connect([this](auto d, auto l) { OnUpstreamData(d, l); });

			acceptor_.async_accept(session_->downstream_socket(), [this](auto &&err) { handle_accept(err); });
		}
		catch(...)
		{
			//std::cerr << "acceptor exception: " << e.what() << std::endl;
			return false;
		}
		return true;
	}
	bool acceptor::start()
	{
		m_bDoStop=false;

		accept_connections();
		// The io_context::run() call will block until all asynchronous operations
		// have finished. While the server is running, there is always at least one
		// asynchronous operation outstanding: the asynchronous accept call waiting
		// for new incoming connections.
		io_context_.run();
		return true;
	}
	bool acceptor::stop()
	{
		m_bDoStop=true;
		// Post a call to the stop function so that server::stop() is safe to call
		// from any thread.
		io_context_.post([this] { handle_stop(); });
		return true;
	}

	void acceptor::handle_stop()
	{
		// The server is stopped by canceling all outstanding asynchronous
		// operations. Once all operations have finished the io_context::run() call
		// will exit.
		acceptor_.close();
		//connection_manager_.stop_all();
	}

	void acceptor::handle_accept(const boost::system::error_code& error)
	{
		if (!error)
		{
			session_->start(upstream_host_,upstream_port_);
			if (!accept_connections())
			{
				//std::cerr << "Failure during call to accept." << std::endl;
			}
		}
		else
		{
			//if (!m_bDoStop)
				//std::cerr << "Error: " << error.message() << std::endl;
		}
	}

	void acceptor::OnUpstreamData(const unsigned char *pData, const size_t Len)
	{
		sOnDownstreamData(pData,Len);
	}
	void acceptor::OnDownstreamData(const unsigned char *pData, const size_t Len)
	{
		sOnUpstreamData(pData,Len);
	}

} // namespace tcp_proxy
