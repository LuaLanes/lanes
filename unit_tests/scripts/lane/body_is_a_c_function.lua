local lanes = require "lanes".configure()

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

-- we can create a generator where the lane body is a C function
do
    local b, g = pcall(lanes.gen, "*", print)
    assert(b == true and type(g) == "function")
    -- we can start the lane
    local b, h = pcall(g, "hello")
    -- the lane runs normally
    h:join()
    assert(h.status == "done")
end

-- we can create a generator where the lane body is a C function that raises an error
do
    local b, g = pcall(lanes.gen, "*", error)
    assert(b == true and type(g) == "function")
    -- we can start the lane
    local b, h = pcall(g, "this is an error")
    -- this provides the error that occurred in the lane
    local s, e, t = h:join()
    assert(h.status == "error")
    assert(s == nil and e == "this is an error" and t == nil)
end
