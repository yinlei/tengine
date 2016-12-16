#include "system_info.hpp"

#ifdef _WIN32
#include "system_info_win.cpp"
#else
#include "system_info_linux.cpp"
#endif // _WIN32