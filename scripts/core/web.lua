-------------------------------------------------------------------------------
-- wrap for c.web
-------------------------------------------------------------------------------
local _PACKAGE = (...):match("^(.+)[%./][^%./]+") or ""

local coroutine, string = coroutine, string

local c = tengine.c

local co_pool = require(_PACKAGE .. "/pool")

local methods = {
}

local new = function(port, handler)
    local self = setmetatable({}, {__index = methods})

    self.port = port

    self.server = c.web(port,
            function(type, path, content)
                local co = co_pool.new(handler)
                local succ, err = coroutine_resume(co, type, path, content)
                if not succ then
                    error(err)
                end
            end
    )

    return self
end

return {
    new = new
}
