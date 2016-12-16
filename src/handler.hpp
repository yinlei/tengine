#ifndef TENGINE_HANDLER_HPP
#define TENGINE_HANDLER_HPP

#include <typeinfo>
#include <typeindex>

namespace tengine
{
	class Service;

	typedef Service* ServiceAddress;

	class MessageHandlerBase;

	typedef std::shared_ptr<MessageHandlerBase> MessageHandlerBasePtr;

	class MessageHandlerBase : public Allocator
	{
	public:
		~MessageHandlerBase() {}

		virtual const std::type_index& identity() const = 0;
		virtual const int message_type() const = 0;
	};

	template<class T, class...Args> 
	struct FunctionTrait 
	{
		typedef T result_type;

		static constexpr std::size_t arg_count = sizeof...(Args);

		template<std::size_t N> struct arg {
			static_assert(N < arg_count, "N must be less than the number of arguments in the function.");
			typedef typename std::tuple_element<N, std::tuple<Args...>>::type type;
		};
	};

	template<class... Args>
	class AMessageHandler : public MessageHandlerBase
	{
	public:
		virtual void handle(ServiceAddress src, Args... args) = 0;
	};

	template<int MessageType, class ServiceType, class... Args>
	class AMFMessageHandler : public AMessageHandler<Args...>
	{
	public:
		AMFMessageHandler(int (ServiceType::* mf)(ServiceAddress, Args...), ServiceType* s)
			: function_(mf)
			, service_(s)
			, message_type_(MessageType)
			, trait_type_(typeid(FunctionTrait<void, Args...>))
		{
		}

		virtual const std::type_index& identity() const
		{
			return trait_type_;
		}

		virtual const int message_type() const
		{
			return message_type_;
		}

		virtual void handle(ServiceAddress src, Args... args)
		{
			(service_->*function_)(src, std::forward<Args>(args) ...);
		}

		bool is_function(int(ServiceType::* mf)(ServiceAddress, Args...))
		{
			return mf == function_;
		}

	private:
		int(ServiceType::* function_)(ServiceAddress, Args...);
		ServiceType *service_;
		int message_type_;
		std::type_index trait_type_;
	};

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
	};

}

#endif
