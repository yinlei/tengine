#include "service.hpp"

#include "context.hpp"

namespace tengine
{
	Service::Service(Context& context)
		: context_(context)
		, executor_(std::move(context.executor()))
		, handlers_()
		, session_id_(0)
		, name_()
		, service_id_(0)
	{
	}

	Service::~Service()
	{

	}

	int Service::init(const char* name)
	{
		name_ = name;
		service_id_ = context_.register_service(this);
		return 0;
	}

	ServiceAddress Service::address()
	{
		return this;
	}

	int Service::session()
	{
		session_id_++;
		return session_id_;
	}

	int Service::id()
	{
		return service_id_;
	}

}

