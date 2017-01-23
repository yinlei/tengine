#ifndef TENGINE_SERVICE_HPP
#define TENGINE_SERVICE_HPP

#include "asio.hpp"

#include "allocator.hpp"
#include "handler.hpp"

#include "context.hpp"

#include <typeinfo>
#include <memory>

namespace tengine
{
	class Context;

	class Service;

	typedef Service* ServiceAddress;

	enum MessageType : unsigned long
	{
		kMessageNone,
		kMessageTimer,
		kMessageLogger,

		kMessageTcpServerAccept,
		kMessageTcpServerRead,
		kMessageTcpServerClosed,

		kMessageUdpServerRead,

		kMessageChannelConnected,
		kMessageChannelRead,
		kMessageChannelClosed,

		kMessageUdpChannelRead,
		kMessageUdpSenderRead,

		kMessageServiceRequest,
		kMessageServiceResponse,

		kMessageInternal,

		kMessageCount,
	};

	template<int T>
	struct MessageTypeTrait {
		//static_assert(T < MessageType::kMessageCount && T > 0, "T must be less than MessageType.");
	};

	class Service : public Allocator
	{
	public:
		typedef asio::strand<asio::executor> ServiceExecutor;

		Service(Context& context);

		virtual ~Service();

		virtual int init(const char* name);

		ServiceAddress address();

		const char *name() { return name_.c_str(); }

		Context& context() { return context_; }

		ServiceExecutor &executor() { return executor_; }

		int session();

		int id();

		template<int MessageType, class... Args>
		friend void send(ServiceAddress sfrom, int to, Args&&... args)
		{
			if (!sfrom)
				return;

			int from = sfrom->id();

			Context& context = sfrom->context();

			Service *sto = context.query(to);
			if (!sto)
				return;

			asio::post(sto->executor_,
				[=]
			{
				Service *src = nullptr;
				sto->call_handler<MessageType>(src, args...);
			});
		}

	protected:

		template<int MessageType, class ServiceType, class... Args>
		void register_handler(int(ServiceType::* mf)(ServiceAddress, Args...))
		{
			handlers_.push_back(
				std::make_shared<AMFMessageHandler<MessageType, ServiceType, Args...>>(mf,
					static_cast<ServiceType*>(this))
			);
		}

		template<int MessageType, class ServiceType, class... Args>
		void unregister_handler(int (ServiceType::* mf)(ServiceAddress, Args...))
		{
			const std::type_index& id = typeid(FunctionTrait<void, Args...>);
			for (auto iter = handlers_.begin(); iter != handlers_.end(); ++iter)
			{
				if ((*iter)->message_type() == MessageType && (*iter)->identity() == id)
				{
					auto mh = static_cast<AMFMessageHandler<MessageType, ServiceType, Args...>*>(iter->get());
					if (mh->is_function(mf))
					{
						handlers_.erase(iter);
						return;
					}
				}
			}
		}

		template<int MessageType, class... Args>
		void call_handler(ServiceAddress src, Args... args)
		{
			const std::type_index& message_id = typeid(FunctionTrait<void, Args...>);
			for (auto& h : handlers_)
			{
				if (h->message_type() == MessageType && h->identity() == message_id)
				{
					auto mh = static_cast<AMessageHandler<Args...>*>(h.get());
					mh->handle(src, args...);
				}
			}
		}

		Context& context_;

		ServiceExecutor executor_;

		std::vector<MessageHandlerBasePtr> handlers_;

		int session_id_;

		std::string name_;

		int service_id_;
	};

}

#endif
