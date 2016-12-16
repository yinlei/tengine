#include "system_info.hpp"

#include <windows.h>

namespace tengine 
{
	int	SystemInfo::count_of_processors()
	{
		SYSTEM_INFO info;
		::GetSystemInfo(&info);

		return info.dwNumberOfProcessors;
	}

	int64_t SystemInfo::amount_of_physical_memory()
	{
		return 0;
	}

	std::string SystemInfo::operating_system_name()
	{
		return "Windows";
	}

}	//	namespace ccactor