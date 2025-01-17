#include "stdafx.h"
#include "ASyncTCP.h"
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>     // for error_code
#include "../main/Logger.h"

struct hostent;

#define MAX_TCP_BUFFER_SIZE 4096

#ifndef WIN32
	#include <unistd.h> //gethostbyname
#endif

#define STATUS_OK(err) !err

ASyncTCP::ASyncTCP(const bool secure) :
	mTcpwork(boost::asio::make_work_guard(mIoc))
#ifdef WWW_ENABLE_SSL
	,mSecure(secure)
#endif
{
	m_pRXBuffer = new uint8_t[MAX_TCP_BUFFER_SIZE];
#ifdef WWW_ENABLE_SSL
	mContext.set_verify_mode(boost::asio::ssl::verify_none);
	if (mSecure) 
	{
		mSslSocket.reset(new boost::asio::ssl::stream<boost::asio::ip::tcp::socket>(mIoc, mContext));
	}
#endif
}

ASyncTCP::~ASyncTCP()
{
	assert(mTcpthread == nullptr);
	mIsTerminating = true;
	if (mTcpthread)
	{
		//This should never happen. terminate() never called!!
		_log.Log(LOG_ERROR, "ASyncTCP: Workerthread not closed. terminate() never called!!!");
		mIoc.stop();
		if (mTcpthread)
		{
			mTcpthread->join();
			mTcpthread.reset();
		}
	}
	if (m_pRXBuffer != nullptr)
		delete[] m_pRXBuffer;
}

void ASyncTCP::SetReconnectDelay(int32_t Delay)
{
	mReconnectDelay = Delay;
}

void ASyncTCP::connect(const std::string& ip, uint16_t port)
{
	assert(!mSocket.is_open());
	if (mSocket.is_open())
	{
		_log.Log(LOG_ERROR, "ASyncTCP: connect called while socket is still open. !!!");
		terminate();
	}

	// RK: We restart mIoc here because it might have been stopped in terminate()
	mIoc.restart();
	// RK: After the reset, we need to provide it work anew
	mTcpwork.reset();
	mTcpwork.emplace(boost::asio::make_work_guard(mIoc));
	if (!mTcpthread)
		mTcpthread = std::make_shared<std::thread>([p = &mIoc] { p->run(); });

	mIp = ip;
	mPort = port;
	std::string port_str = std::to_string(port);
	timeout_start_timer();
	mResolver.async_resolve(ip, port_str, [this](auto &&err, auto endpoints) { cb_resolve_done(err, endpoints); });
}

void ASyncTCP::cb_resolve_done(const boost::system::error_code& error, boost::asio::ip::tcp::resolver::results_type endpoints)
{
	if (mIsTerminating) return;

	if (STATUS_OK(error))
	{
		connect_start(endpoints);
	}
	else
	{
		process_error(error);
	}
}

void ASyncTCP::connect_start(boost::asio::ip::tcp::resolver::results_type& endpoints)
{
	if (mIsConnected) return;

	timeout_start_timer();
#ifdef WWW_ENABLE_SSL
	if (mSecure)
	{
		// we reset the ssl socket, because the ssl context needs to be reinitialized after a reconnect
		mSslSocket.reset(new boost::asio::ssl::stream<boost::asio::ip::tcp::socket>(mIoc, mContext));
		boost::asio::async_connect(mSslSocket->lowest_layer(), endpoints, [this](auto &&err, auto &endpoint) mutable { cb_connect_done(err, endpoint); });
	}
	else
#endif
	{
		boost::asio::async_connect(mSocket, endpoints, [this](auto &&err, auto &endpoint) mutable { cb_connect_done(err, endpoint); });
	}
}

void ASyncTCP::cb_connect_done(const boost::system::error_code& error, const boost::asio::ip::tcp::endpoint &)
{
	if (mIsTerminating) return;

	if (STATUS_OK(error))
	{
#ifdef WWW_ENABLE_SSL
		if (mSecure) 
		{
			timeout_start_timer();
			mSslSocket->async_handshake(boost::asio::ssl::stream_base::client, [this](auto &&err) { cb_handshake_done(err); });
		}
		else
#endif
		{
			process_connection();
		}
	}
	else 
	{
		process_error(error);
	}
}

#ifdef WWW_ENABLE_SSL
void ASyncTCP::cb_handshake_done(const boost::system::error_code& error)
{
	if (mIsTerminating) return;

	if (STATUS_OK(error))
	{
		process_connection();
	}
	else
	{
		process_error(error);
	}
}
#endif

void ASyncTCP::reconnect_start_timer()
{
	if (mIsReconnecting) return;

	if (mReconnectDelay != 0)
	{
		mIsReconnecting = true;

		mReconnectTimer.expires_from_now(boost::posix_time::seconds(mReconnectDelay));
		mReconnectTimer.async_wait([this](auto &&err) { cb_reconnect_start(err); });
	}
}

void ASyncTCP::cb_reconnect_start(const boost::system::error_code& error)
{
	mIsReconnecting = false;
	mReconnectTimer.cancel();
	mTimeoutTimer.cancel();

	if (mIsConnected) return;
	if (error) return; // timer was cancelled

	do_close();
	connect(mIp, mPort);
}


void ASyncTCP::terminate(const bool silent)
{
	mIsTerminating = true;
	disconnect(silent);
	mTcpwork.reset();
	mIoc.stop();
	if (mTcpthread)
	{
		mTcpthread->join();
		mTcpthread.reset();
	}
	mIsReconnecting = false;
	mIsConnected = false;
	mWriteQ.clear();
	mIsTerminating = false;
}

void ASyncTCP::disconnect(const bool silent)
{
	mReconnectTimer.cancel();
	mTimeoutTimer.cancel();
	if (!mTcpthread) return;

	try
	{
		boost::asio::post(mIoc, [this] { do_close(); });
	}
	catch (...)
	{
		if (silent == false)
		{
			throw;
		}
	}
}

void ASyncTCP::do_close()
{
	if (mIsReconnecting) {
		return;
	}
	mReconnectTimer.cancel();
	mTimeoutTimer.cancel();
	boost::system::error_code ec;
#ifdef WWW_ENABLE_SSL
	if (mSecure)
	{
		if (mSslSocket->lowest_layer().is_open())
		{
			mSslSocket->lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
			mSslSocket->lowest_layer().close(ec);
		}
	}
	else
#endif
	{
		if (mSocket.is_open())
		{
			mSocket.close(ec);
		}
	}
}

void ASyncTCP::do_read_start()
{
	if (mIsTerminating) return;
	if (!mIsConnected) return;

	timeout_start_timer();
#ifdef WWW_ENABLE_SSL
	if (mSecure)
	{
		mSslSocket->async_read_some(boost::asio::buffer(m_pRXBuffer, MAX_TCP_BUFFER_SIZE), [this](auto &&err, auto bytes) { cb_read_done(err, bytes); });
	}
	else
#endif
	{
		mSocket.async_read_some(boost::asio::buffer(m_pRXBuffer, MAX_TCP_BUFFER_SIZE), [this](auto &&err, auto bytes) { cb_read_done(err, bytes); });
	}
}

void ASyncTCP::cb_read_done(const boost::system::error_code& error, size_t bytes_transferred)
{
	if (mIsTerminating) return;

	if (STATUS_OK(error))
	{
		OnData(m_pRXBuffer, bytes_transferred);
		do_read_start();
	}
	else
	{
		process_error(error);
	}
}

void ASyncTCP::write(const uint8_t* pData, size_t length)
{
	write(std::string((const char*)pData, length));
}

void ASyncTCP::write(const std::string& msg)
{
	if (!mTcpthread) return;

	boost::asio::post(mSendStrand, [this, msg]() { cb_write_queue(msg); });
}

void ASyncTCP::cb_write_queue(const std::string& msg)
{
	mWriteQ.push_back(msg);

	if (mWriteQ.size() == 1)
		do_write_start();
}

void ASyncTCP::do_write_start()
{
	if (mIsTerminating) return;
	if (!mIsConnected) return;
	if (mWriteQ.empty())
		return;

	timeout_start_timer();
#ifdef WWW_ENABLE_SSL
	if (mSecure) 
	{
		boost::asio::async_write(*mSslSocket, boost::asio::buffer(mWriteQ.front()), [this](auto &&err, auto) { cb_write_done(err); });
	}
	else
#endif
	{
		boost::asio::async_write(mSocket, boost::asio::buffer(mWriteQ.front()), [this](auto &&err, auto) { cb_write_done(err); });
	}
}

void ASyncTCP::cb_write_done(const boost::system::error_code& error)
{
	if (mIsTerminating) return;

	if (STATUS_OK(error))
	{
		mWriteQ.pop_front();
		do_write_start();
	}
	else
	{
		process_error(error);
	}
}

void ASyncTCP::process_connection()
{
	mIsConnected = true;
#ifdef WWW_ENABLE_SSL

	if (!mSecure)
#endif
	{
		// RK: only if non-secure
		boost::asio::socket_base::keep_alive option(true);
		mSocket.set_option(option);
	}
	OnConnect();
	do_read_start();
	do_write_start();
}

void ASyncTCP::process_error(const boost::system::error_code& error)
{
	do_close();
	if (mIsConnected)
	{
		mIsConnected = false;
		OnDisconnect();
	}

	if (boost::asio::error::operation_aborted == error)
		return;

	OnError(error);
	reconnect_start_timer();
}

/* timeout methods */
void ASyncTCP::timeout_start_timer()
{
	if (0 == mTimeoutDelay) {
		return;
	}
	timeout_cancel_timer();
	mTimeoutTimer.expires_from_now(boost::posix_time::seconds(mTimeoutDelay));
	mTimeoutTimer.async_wait([this](auto &&err) { timeout_handler(err); });
}

void ASyncTCP::timeout_cancel_timer()
{
	mTimeoutTimer.cancel();
}

void ASyncTCP::timeout_handler(const boost::system::error_code& error)
{
	if (error) {
		// timer was cancelled on time
		return;
	}
	boost::system::error_code err = make_error_code(boost::system::errc::timed_out);
	process_error(err);
}

void ASyncTCP::SetTimeout(const uint32_t Timeout)
{
	mTimeoutDelay = Timeout;
}
