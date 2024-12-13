local io = assert(io)
local pairs = assert(pairs)
local select = assert(select)
local string_format = assert(string.format)
local tostring = assert(tostring)
local type = assert(type)

local print_id = 0
local P = function(whence_, ...)
    if not io then return end
    print_id = print_id + 1
    local _str = string_format("%s: %02d.\t", whence_, print_id)
    for _i  = 1, select('#', ...) do
        _str = _str .. tostring(select(_i, ...)) .. "\t"
    end
    if io then
        io.stderr:write(_str .. "\n")
    end
end

local MAKE_PRINT = function()
    local _whence = lane_threadname and lane_threadname() or "main"
    return function(...)
        P(_whence, ...)
    end
end

local tables_match

-- true if 'a' is a subtable of 'b'
--
local function subtable(a, b)
    --
    assert(type(a)=="table" and type(b)=="table")

    for k,v in pairs(b) do
        if type(v)~=type(a[k]) then
            return false    -- not subtable (different types, or missing key)
        elseif type(v)=="table" then
            if not tables_match(v,a[k]) then return false end
        else
            if a[k] ~= v then return false end
        end
    end
    return true     -- is a subtable
end

-- true when contents of 'a' and 'b' are identical
--
tables_match = function(a, b)
    return subtable(a, b) and subtable(b, a)
end

local function dump_error_stack(error_reporting_mode_, stack)
    local PRINT = MAKE_PRINT()
    if error_reporting_mode_ == "minimal" then
        assert(stack == nil, table.concat{"stack is a ", type(stack)})
    elseif error_reporting_mode_ == "basic" then -- each stack line is a string in basic mode
        PRINT("STACK:\n", table.concat(stack,"\n\t"));
    else -- each stack line is a table in extended mode
        PRINT "STACK:"
        for line, details in pairs(stack) do
            PRINT("\t", line);
            for detail, value in pairs(details) do
                PRINT("\t\t", detail, ": ", value)
            end
        end
    end
end

return {
    MAKE_PRINT = MAKE_PRINT,
    tables_match = tables_match,
    dump_error_stack = dump_error_stack
}
