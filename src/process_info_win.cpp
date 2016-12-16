#include "process_info.hpp"

#include "system_info.hpp"

#include <psapi.h>

#pragma comment(lib,"psapi.lib")

namespace tengine 
{
	int64_t convertFileTimeToUtc(const FILETIME* ftime)
	{
		if (NULL != ftime)
		{
			LARGE_INTEGER li;
			li.LowPart = ftime->dwLowDateTime;
			li.HighPart = ftime->dwHighDateTime;
			return li.QuadPart;
		}
		else
			return 0;
	}

	ProcessInfo::ProcessInfo()
		: process_(kNullProcessHandle)
	{
		process_ = ::GetCurrentProcess();
	}

	ProcessInfo::ProcessInfo(ProcessHandle handle)
		: process_(handle)
	{

	}

	ProcessInfo ProcessInfo::current()
	{
		return ProcessInfo(::GetCurrentProcess());
	}

	ProcessIdentity ProcessInfo::id() const
	{
		if (kNullProcessHandle != process_)
			return ::GetProcessId(process_);
		else
			return kNullProcessId;
	}

	int ProcessInfo::amount_of_thread() const
	{
		if (kNullProcessHandle != process_)
		{
			//::GetProcessMemoryInfo(
			return 0;
		}
		else
			return 0;
	}

	int64_t ProcessInfo::amount_of_memory_used() const
	{
		if (kNullProcessHandle != process_)
		{
			PROCESS_MEMORY_COUNTERS pmc;
			::GetProcessMemoryInfo(process_, &pmc, sizeof(pmc));
			return pmc.WorkingSetSize;
		}
		else
			return 0;
	}

	int64_t ProcessInfo::amount_of_vmemory_used() const
	{
		if (kNullProcessHandle != process_)
		{
			PROCESS_MEMORY_COUNTERS pmc;
			::GetProcessMemoryInfo(process_, &pmc, sizeof(pmc));
			return pmc.PagefileUsage;
		}
		else
			return 0;
	}

	int ProcessInfo::cpu_usage() const
	{
		static int64_t last_time = 0;
		static int64_t last_cpu_time = 0;

		FILETIME lpCreationTime;
		FILETIME lpExitTime;
		FILETIME lpKernelTime;
		FILETIME lpUserTime;
		if (kNullProcessHandle != process_)
		{
			if (::GetProcessTimes(process_, &lpCreationTime, &lpExitTime, &lpKernelTime, &lpUserTime))
			{
				int64_t cpu_time = (convertFileTimeToUtc(&lpKernelTime) + convertFileTimeToUtc(&lpUserTime)) /
					SystemInfo::count_of_processors();
				FILETIME now;
				GetSystemTimeAsFileTime(&now);
				int64_t now_time = convertFileTimeToUtc(&now);
				if ((last_cpu_time == 0) || (last_time == 0))
				{
					last_cpu_time = cpu_time;
					last_time = now_time;
					return -1;
				}

				int64_t cpu_time_diff = cpu_time - last_cpu_time;
				int64_t time_diff = now_time - last_time;

				if (0 == time_diff)
					return -1;

				last_cpu_time = cpu_time;
				last_time = now_time;

				return (int)((cpu_time_diff * 100 + time_diff / 2) / time_diff);
			}
			else
				return -1;
		}
		else
			return -1;

	}


}	//namespace ccactor
