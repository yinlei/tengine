#include "channel.hpp"

#include "context.hpp"
#include "service.hpp"
#include "executor.hpp"
#include "dispatch.hpp"

#include "asio/ts/executor.hpp"

#include <iostream>

namespace tengine
{
	constexpr int Channel::CHANNEL_KEY;

	Channel::Channel(Service *s)
		: ServiceProxy(s)
		, io_service_(s->context().net_executor().io_service())
		, socket_(io_service_)
		, address_()
		, port_()
		, read_message_()
		, write_messages_()
	{

	}

	Channel::~Channel()
	{
		close();
	}

	void Channel::connect(const char *address, const char *port)
	{
		this->address_.assign(address);
		this->port_.assign(port);

		asio::ip::tcp::resolver resolver(io_service_);
		auto endpoint_iterator = resolver.resolve({ address_, port_ });
		do_connect(endpoint_iterator);
	}

	void Channel::async_connect(const char *address, const char *port)
	{
		this->address_.assign(address);
		this->port_.assign(port);

		asio::ip::tcp::resolver resolver(io_service_);
		auto endpoint_iterator = resolver.resolve({ address_, port_ });
		do_connect(endpoint_iterator);
	}

	void Channel::write(const char *data, size_t size)
	{
		Message msg;
		msg.body_length((uint16_t)size);
		std::memcpy(msg.body(), data, msg.body_length());
		msg.encode_header();
		write(msg);
	}

	void Channel::write(const Message& message)
	{
		asio::post(io_service_,
			[this, message]()
		{
			bool write_in_progress = !write_messages_.empty();
			write_messages_.push_back(message);
			if (!write_in_progress)
			{
				do_write();
			}
		});
	}

	void Channel::close()
	{
		if (!socket_.is_open())
			return;

		asio::error_code ignored_ec;
		socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
		socket_.close(ignored_ec);
	}

	bool Channel::is_open()
	{
		return socket_.is_open();
	}

	void Channel::do_connect(
		const asio::ip::tcp::resolver::iterator& endpoint_iterator)
	{
		auto self(this->shared_from_this());

		asio::async_connect(socket_, endpoint_iterator,
			[this, self](std::error_code ec, asio::ip::tcp::resolver::iterator)
		{
			if (!ec)
			{
				asyncNotifyConnected();

				do_read_header();
			}
			else
			{
				asyncNotifyClosed(ec.message().c_str());
			}
		});
	}

	void Channel::do_read_header()
	{
		auto self(this->shared_from_this());

		asio::async_read(socket_,
			asio::buffer(read_message_.data(), Message::header_length),
			make_custom_alloc_handler(allocator_,
				[this, self](std::error_code ec, std::size_t /*length*/)
		{
			if (!ec && read_message_.decode_header())
			{
				do_read_body();
			}
			else
			{
				close();
				asyncNotifyClosed(ec.message().c_str());
			}
		}));
	}

	void Channel::do_read_body()
	{
		auto self(this->shared_from_this());

		asio::async_read(socket_,
			asio::buffer(read_message_.body(), read_message_.body_length()),
			[this, self](std::error_code ec, std::size_t /*length*/)
		{
			if (!ec)
			{
				asyncNotifyRead(read_message_.body(),
					read_message_.body_length());
				do_read_header();
			}
			else
			{
				//socket_.close();
				close();
				asyncNotifyClosed(ec.message().c_str());
			}
		});
	}

	void Channel::do_write()
	{
		auto self(this->shared_from_this());

		asio::async_write(socket_,
			asio::buffer(write_messages_.front().data(),
				write_messages_.front().length()),
			[this, self](std::error_code ec, std::size_t /*length*/)
		{
			if (!ec)
			{
				write_messages_.pop_front();
				if (!write_messages_.empty())
				{
					do_write();
				}
			}
			else
			{
				socket_.close();
				asyncNotifyClosed(ec.message().c_str());
			}
		});
	}

	void Channel::asyncNotifyConnected()
	{
		dispatch<MessageType::kMessageChannelConnected, SandBox>(
			host(), host(), (void*)this);
	}

	void Channel::asyncNotifyRead(const char *data, std::size_t size)
	{
		const char *tmp = (char*)ccmalloc(size);

		if (tmp)
		{
			memcpy((void*)tmp, data, size);

			dispatch<MessageType::kMessageChannelRead, SandBox>(
				host(), host(), (void*)this, tmp, size);
		}
	}

	void Channel::asyncNotifyClosed(const char *error)
	{
		const char *tmp = (char*)ccmalloc(strlen(error));
		if (tmp)
		{
			dispatch<MessageType::kMessageChannelClosed, SandBox>(
				host(), host(), (void*)this, tmp);
		}
	}

	///////////////////////////////////////////////////////////////////////////

	constexpr int UdpChannel::UDPCHANNEL_KEY;

	UdpChannel::UdpChannel(Service *s, const std::string& address,
		const std::string& port)
		: ServiceProxy(s)
		, io_service_(s->context().net_executor().io_service())
		, socket_(io_service_, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0))
		, endpoint_()
		, message_holder_()
	{
		asio::ip::udp::resolver resolver(io_service_);
		asio::ip::udp::resolver::query query(
			asio::ip::udp::v4(), address, port);
		asio::ip::udp::resolver::iterator itr = resolver.resolve(query);

		endpoint_ = *itr;

		do_receive();
	}

	UdpChannel::~UdpChannel()
	{
		close();
	}

	void UdpChannel::do_receive()
	{
		socket_.async_receive(asio::null_buffers(),
			[this](const asio::error_code& error, unsigned int)
		{
			if (error)
			{
				std::cerr << error.message() << std::endl;
			}
			else
			{
				size_t bytes_available = socket_.available();

				auto receive_buffer =
					std::make_shared<std::vector<char>>(bytes_available);
				auto sender_endpoint =
					std::make_shared<asio::ip::udp::endpoint>();

				socket_.async_receive_from(
					asio::buffer(receive_buffer->data(),
						receive_buffer->size()), *sender_endpoint,
					[this, receive_buffer, sender_endpoint]
				(const asio::error_code& error, size_t bytes_recvd)
				{
					if (error)
					{
						std::cerr << error.message() << std::endl;
					}
					else
					{
						//this->handle_message({ receive_buffer->data(), receive_buffer->data() + bytes_recvd }, *sender_endpoint);
						//std::string message = { receive_buffer->data(), receive_buffer->data() + bytes_recvd };

						const std::string& address =
							sender_endpoint->address().to_string();
						uint16_t port = sender_endpoint->port();

						dispatch<MessageType::kMessageUdpChannelRead, SandBox>(
							host(), host(), (void*)this, address, port,
							(const char*)receive_buffer->data(), bytes_recvd);

						do_receive();
					}
				});
			}
		});
	}

	int UdpChannel::send_to(const char* data, std::size_t size)
	{
		return send_to({data, size});
	}

	int UdpChannel::send_to(const std::string& data)
	{
		return socket_.send_to(asio::buffer(data), endpoint_);
	}

	void UdpChannel::do_write()
	{
		if (!message_holder_.empty())
		{
			socket_.async_send_to(
				asio::buffer(message_holder_.front().data), endpoint_,
				[this](const asio::error_code& error,
					std::size_t bytes_transferred)
				{
					if (!error)
					{
						message_holder_.pop_front();

						do_receive();

						do_write();
					}

				}
			);
		}
	}

	void UdpChannel::do_write(const MessageData& data)
	{
		message_holder_.push_back(data);
		do_write();
	}

	int UdpChannel::async_send_to(const char* data, std::size_t size)
	{
		return async_send_to({ data, size });
	}

	int UdpChannel::async_send_to(const std::string& data)
	{
		io_service_.post([=]
		{
			do_write({ data });
		});

		return 0;
	}

	void UdpChannel::close()
	{
		socket_.close();
	}

	///////////////////////////////////////////////////////////////////////////
	constexpr int UdpSender::UDPSENDER_KEY;

	UdpSender::UdpSender(Service *s)
		: ServiceProxy(s)
		, io_service_(s->context().net_executor().io_service())
		, socket_(io_service_, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0))
		, message_holder_()
	{
		do_receive();
	}

	UdpSender::~UdpSender()
	{
		close();
	}

	void UdpSender::do_receive()
	{
		socket_.async_receive(asio::null_buffers(),
			[this](const asio::error_code& error, unsigned int)
		{
			if (error)
			{
				std::cerr << error.message() << std::endl;
			}
			else
			{
				size_t bytes_available = socket_.available();

				auto receive_buffer =
					std::make_shared<std::vector<char>>(bytes_available);
				auto sender_endpoint =
					std::make_shared<asio::ip::udp::endpoint>();

				socket_.async_receive_from(
					asio::buffer(receive_buffer->data(),
					receive_buffer->size()), *sender_endpoint,
					[this, receive_buffer, sender_endpoint]
				(const asio::error_code& error, size_t bytes_recvd)
				{
					if (error)
					{
						std::cerr << error.message() << std::endl;
					}
					else
					{
						//this->handle_message({ receive_buffer->data(), receive_buffer->data() + bytes_recvd }, *sender_endpoint);
						//std::string message = { receive_buffer->data(), receive_buffer->data() + bytes_recvd };

						const std::string& address =
							sender_endpoint->address().to_string();
						uint16_t port = sender_endpoint->port();

						dispatch<MessageType::kMessageUdpSenderRead, SandBox>(
							host(), host(), (void*)this, address, port,
							(const char*)receive_buffer->data(), bytes_recvd);

						do_receive();
					}
				});
			}
		});
	}

	int UdpSender::send_to(const char* data, std::size_t size,
		const std::string& address, uint16_t port)
	{
		asio::ip::udp::endpoint endpoint =
			{ asio::ip::address::from_string(address), port };

		return send_to({ data, size }, endpoint);
	}

	int UdpSender::send_to(
		const std::string& data, asio::ip::udp::endpoint& endpoint)
	{
		return socket_.send_to(asio::buffer(data), endpoint);
	}

	void UdpSender::do_write()
	{
		if (!message_holder_.empty())
		{
			socket_.async_send_to(
				asio::buffer(message_holder_.front().data),
					message_holder_.front().endpoint,
				[this](const asio::error_code& error, std::size_t bytes_transferred)
			{
				if (!error)
				{
					message_holder_.pop_front();

					do_receive();

					do_write();
				}

			}
			);
		}
	}

	void UdpSender::do_write(const MessageData& data)
	{
		message_holder_.push_back(data);
		do_write();
	}

	int UdpSender::async_send_to(const char* data, std::size_t size,
		const std::string& address, uint16_t port)
	{
		asio::ip::udp::endpoint endpoint =
		{ asio::ip::address::from_string(address), port };

		return async_send_to({ data, size }, endpoint);
	}

	int UdpSender::async_send_to(
		const std::string& data, asio::ip::udp::endpoint& endpoint)
	{
		io_service_.post([=]
		{
			do_write({ data, endpoint });
		});

		return 0;
	}

	void UdpSender::close()
	{
		socket_.close();
	}
}
