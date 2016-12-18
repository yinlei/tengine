#ifndef TENGINE_DISPATCH_HPP
#define TENGINE_DISPATCH_HPP

#include "context.hpp"
#include "service.hpp"
#include "sandbox.hpp"
#include "handler.hpp"

namespace tengine
{
	template<class T, class... Args>
	static void dispatch(Context& context, int from, int to, Args&&... args)
	{
		T *sto = reinterpret_cast<T*>(context.query(to));
		if (!sto)
			return;

		asio::post(sto->executor(),
			[=]
		{
			//sto->handler(from, std::forward<Args>(args)...);
			sto->handler(from, args...);
		});
	}

	template<int MessageType, class T, class... Args>
	static void dispatch(Service *sfrom, const int to, Args&&... args)
	{
		if (!sfrom)
			return;

		Context& context = sfrom->context();

		Service *sto = context.query(to);
		if (!sto)
			return;

		//dispatch<MessageType, T>(sfrom, sto, args...);
		dispatch<MessageType, T>(sfrom, sto, std::forward<Args>(args)...);
	}

	template<int MessageType, class T, class... Args>
	static void dispatch(Service *sfrom, Service* sto, Args&&... args)
	{
		if (!sfrom || !sto)
			return;

		int from = sfrom->id();

		asio::post(sto->executor(),
			[=]
		{
			T* self = reinterpret_cast<T*>(sto);

			self->handler<MessageType>(from, std::forward<Args>(args)...);
		});
	}
}

#endif // !TENGINE_DISPATCH_HPP
