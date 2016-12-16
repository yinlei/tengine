--------------------------------------------------------------------------------
-- helper
--------------------------------------------------------------------------------
local _PACKAGE = string.gsub(...,"%.","/") or ""

return {
    bind = require (_PACKAGE.."/bind"),
    map = require (_PACKAGE.."/map"),
    balance = require (_PACKAGE.."/balance"),
    weaktable = require (_PACKAGE.."/weaktable"),
}