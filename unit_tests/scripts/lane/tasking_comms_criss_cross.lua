local require_lanes_result_1, require_lanes_result_2 = require "lanes".configure(config).configure()
print("require_lanes_result:", require_lanes_result_1, require_lanes_result_2)
local lanes = require_lanes_result_1

local require_assert_result_1, require_assert_result_2 = require "_assert"
print("require_assert_result:", require_assert_result_1, require_assert_result_2)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

local lanes_gen = assert(lanes.gen)
local lanes_linda = assert(lanes.linda)

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

local gc_cb = function(name_, status_)
    PRINT("				---> lane '" .. name_ .. "' collected with status '" .. status_ .. "'")
end

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

PRINT("\n\n", "---=== Comms criss cross ===---", "\n\n")

-- We make two identical lanes, which are using the same Linda channel.
--
local tc = lanes_gen("io", {gc_cb = gc_cb},
  function(linda, ch_in, ch_out)
        lane_threadname("criss cross " .. ch_in .. " -> " .. ch_out)
    local function STAGE(str)
        io.stderr:write(ch_in..": "..str.."\n")
        linda:send(nil, ch_out, str)
        local k,v= linda:receive(nil, ch_in)
        assert(v==str)
    end
    STAGE("Hello")
    STAGE("I was here first!")
    STAGE("So what?")
  end
)

local linda= lanes_linda("criss cross")

local a,b= tc(linda, "A","B"), tc(linda, "B","A")   -- launching two lanes, twisted comms

local _= a[0],b[0]  -- waits until they are both ready

PRINT("collectgarbage")
a, b = nil
collectgarbage()
