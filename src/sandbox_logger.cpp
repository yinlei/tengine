#include "sandbox.hpp"

#include "context.hpp"

using namespace tengine;

static int log(lua_State* L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	Logger *logger = (Logger*)context->query("Logger");
	if (logger == nullptr)
		return luaL_error(L, "no logger service");

	int level = (int)luaL_checknumber(L, 1);
	std::size_t len;
	const char *msg = lua_tolstring(L, 2, &len);
	logger->log(level, std::string().assign(msg, len), self);

	lua_settop(L, 0);

	return 0;
}
