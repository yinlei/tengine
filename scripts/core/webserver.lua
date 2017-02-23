-------------------------------------------------------------------------------
-- wrap for c.webserver
-------------------------------------------------------------------------------
local _PACKAGE = (...):match("^(.+)[%./][^%./]+") or ""

local coroutine_running = coroutine.running
local coroutine_resume = coroutine.resume
local coroutine_yield = coroutine.yield

local string = string

local c = tengine.c

local co_pool = require(_PACKAGE .. "/pool")

local send = function(self, ...)
    if self.server then
        self.server:send(...)
    end
end

local close = function(self, session)
    if self.server then
        self.server:close(session)
    end
end

local start = function(self, path, open, message, close, error)
    if not self.server then
        error('create webserver first !!!')
    end

    self.server:start(path, {
        on_open =
            function(session)
                local co = co_pool.new(open)
                local succ, err = coroutine_resume(co, session)
                if not succ then
                    error(err)
                end
            end,

        on_message =
            function(session, data, size)
                local co = co_pool.new(message)
                local succ, err = coroutine_resume(co, session, data, size)
                if not succ then
                    error(err)
                end
            end,

        on_close =
            function(session, status, reason)
                local co = co_pool.new(close)
                local succ, err = coroutine_resume(co, session, status, reason)
                if not succ then
                    error(err)
                end
            end,

        on_error =
            function(session, err)
                local co = co_pool.new(errors)
                local succ, err = coroutine_resume(co, session, err)
                if not succ then
                    error(err)
                end
            end,           
    })
end

local methods = {
    send = send,
    close = close,
    start = start,
}

local new = function(port)
    local self = setmetatable({}, {__index = methods})

    self.port = port

    self.server = c.webserver(port)

    return self
end

return {
    new = new
}
