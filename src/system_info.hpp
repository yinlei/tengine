#ifndef TENGINE_SYSTEM_INFO_HPP
#define TENGINE_SYSTEM_INFO_HPP

#include <stdint.h>
#include <string>

namespace tengine
{
	class SystemInfo
	{
	public:
		static int count_of_processors();

		static int64_t amount_of_physical_memory();

		static int amount_of_physical_memory_mb()
		{
			return int(amount_of_physical_memory() / 1024 / 1024);
		}

		static std::string operating_system_name();
	};
}

#endif // !TENGINE_SYSTEM_INFO_HPP
