#include "process_info.hpp"

#include "system_info.hpp"

namespace tengine {

	ProcessInfo::ProcessInfo()
		: process_(kNullProcessHandle)
	{
		process_ = kNullProcessHandle;
	}

	ProcessInfo::ProcessInfo(ProcessHandle handle)
		: process_(handle)
	{

	}

	ProcessInfo ProcessInfo::current()
	{
		return ProcessInfo(kNullProcessHandle);
	}

	ProcessIdentity ProcessInfo::id() const
	{
			return kNullProcessId;
	}

	int ProcessInfo::amount_of_thread() const
	{
			return 0;
	}

	int64_t ProcessInfo::amount_of_memory_used() const
	{
			return 0;
	}

	int64_t ProcessInfo::amount_of_vmemory_used() const
	{
			return 0;
	}

	int ProcessInfo::cpu_usage() const
	{
			return -1;
	}


}	//namespace tengine
