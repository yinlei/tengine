#include "system_info.hpp"

namespace tengine {

	int	SystemInfo::count_of_processors()
	{
		return 0;
	}

	int64_t SystemInfo::amount_of_physical_memory()
	{
		return 0;
	}

	std::string SystemInfo::operating_system_name()
	{
		return "Linux";
	}

}	//	namespace tengine
