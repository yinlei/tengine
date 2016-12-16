#include "sandbox.hpp"

#include "context.hpp"

using namespace tengine;


static int timer(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	Timer *timer = (Timer*)context->query("Timer");
	if (timer == nullptr)
		return luaL_error(L, "no timer service");

	int timeout = (int)luaL_checknumber(L, 1);

	luaL_checktype(L, 2, LUA_TFUNCTION);

	lua_pushvalue(L, 2);

	int handler = luaL_ref(L, LUA_REGISTRYINDEX);

	void *timer_id;

	if (lua_isnoneornil(L, 3))
		timer_id = timer->add_timer(timeout, self->id(), handler);
	else
		timer_id = timer->add_callback(timeout, self->id(), handler);

	if (!lua_isnone(L, 3))
		lua_pop(L, 1);

	lua_settop(L, 0);

	lua_pushlightuserdata(L, timer_id);

	return 1;
}

void SandBox::timer(void* timer_id, int handler)
{
	if (timer_id <= 0)
		return;

	lua_State *L = this->l_;
	lua_rawgeti(L, LUA_REGISTRYINDEX, handler);

	call(0, true);

	luaL_unref(L, LUA_REGISTRYINDEX, handler);
}
