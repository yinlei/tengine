#ifndef TENGINE_PROCESS_INFO_HPP
#define TENGINE_PROCESS_INFO_HPP

#include <string>

#ifdef _WIN32

#include <windows.h>
typedef HANDLE ProcessHandle;
typedef DWORD ProcessIdentity;

static ProcessHandle kNullProcessHandle = NULL;
static ProcessIdentity kNullProcessId = 0;

#else

typedef pid_t ProcessHandle;
typedef int ProcessIdentity;

static ProcessHandle kNullProcessHandle = 0;
static ProcessIdentity kNullProcessId = 0;

#endif


namespace tengine {

	class ProcessInfo
	{
	public:
		ProcessInfo();

		ProcessInfo(ProcessHandle handle);

		static ProcessInfo current();

		ProcessIdentity id() const;

		int amount_of_thread() const;

		int64_t amount_of_memory_used() const;

		int amount_of_memory_used_mb() const
		{
			return int(amount_of_memory_used() / 1024 / 1024);
		}

		int amount_of_memory_used_kb() const
		{
			return int(amount_of_memory_used() / 1024);
		}

		int64_t amount_of_vmemory_used() const;

		int amount_of_vmemory_used_mb() const
		{
			return int(amount_of_vmemory_used() / 1024 / 1024);
		}

		int amount_of_vmemory_used_kb() const
		{
			return int(amount_of_vmemory_used() / 1024);
		}

		int cpu_usage() const;

	private:

		ProcessHandle process_;
	};

}	//namespace tengine

#endif // !TENGINE_PROCESS_INFO_HPP
