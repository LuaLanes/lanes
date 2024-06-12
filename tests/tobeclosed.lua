--
-- TOBECLOSED.LUA           Copyright (C) 2024 benoit Germain <bnt.germain@gmail.com>
--

local require_lanes_result_1, require_lanes_result_2 = require "lanes"
print("require_lanes_result:", require_lanes_result_1, require_lanes_result_2)
local lanes = require_lanes_result_1

local WR = function(...)
    local str=""
    for i=1,select('#',...) do
        local v = select(i,...)
        if type(v) == "function" then
            local infos = debug.getinfo(v)
            --[[for k,v in pairs(infos) do
                print(k,v)
            end]]
            v = infos.source..":"..infos.linedefined
        end
        str= str..tostring(v).."\t"
    end
    if io then
        io.stderr:write(str.."\n")
    end
end

-- #################################################################################################
-- test that table and function handlers work fine, even with upvalues
WR "================================================================================================"
WR "Basic to-be-closed"
do
    local closed_by_f = false
    local closed_by_t = false
    do
        local close_handler_f = function(linda_, err_)
            WR("f closing ", linda_)
            closed_by_f = true
        end
        local lf <close> = lanes.linda("closed by f", close_handler_f)

        local close_handler_t = setmetatable({},
            {
                __call = function(self_, linda_)
                    WR("t closing ", linda_)
                    closed_by_t = true
                end
            }
        )
        local lt <close> = lanes.linda("closed by t", close_handler_t)
    end
    assert(closed_by_f == true)
    assert(closed_by_t == true)
end


-- #################################################################################################
-- test that linda transfer still works even when they contain a close handler
WR "================================================================================================"
WR "Through Linda"
do
    local l = lanes.linda("channel")

    local close_handler_f = function(linda_, err_)
        WR("f closing ", linda_)
        linda_:set("closed", true)
    end
    local l_in = lanes.linda("voyager", close_handler_f)
    l:set("trip", l_in)

    do

        local l_out <close> = l:get("trip")
    end
    assert(l_in:get("closed") == true)
end

-- #################################################################################################
-- test that lane closing works
WR "================================================================================================"
WR "Lane closing"
do
    local lane_body = function()
        WR "In lane body"
        lanes.sleep(1)
    end

    local h = lanes.gen("*", lane_body)()
    do
        local tobeclosed <close> = h
    end
    assert(h.status == "done")
end

-- #################################################################################################
-- test that linda with a close handler can still be transferred in a Lane
WR "================================================================================================"
WR "Linda closing through Lane"
do
    local l = lanes.linda("channel")
    local lane_body = function(l_arg_)
        WR "In lane body"
        -- linda obtained through a linda
        local l_out <close> = l:get("trip")
        -- linda from arguments
        local l_arg <close> = l_arg_
        return true
    end

    local close_handler_f = function(linda_, err_)
        WR("f closing ", linda_)
        linda_:set("closed", (linda_:get("closed") or 0) + 1)
    end
    local l_in = lanes.linda("voyager", close_handler_f)
    l:set("trip", l_in)

    do
    lanes.gen("*", lane_body)(l_in):join()
    end
    assert(l_in:get("closed") == 2)
end

WR "================================================================================================"
WR "TEST OK"