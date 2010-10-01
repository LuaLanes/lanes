--
-- BIN2C.LUA [filename] [-o output.lch]
--
-- Convert files to byte arrays for automatic loading with 'lua_dobuffer'.
--
-- Based on 'etc/bin2c.c' of Lua 5.0.1 sources by:
--      Luiz Henrique de Figueiredo (lhf@tecgraf.puc-rio.br)
--
-- Changes:
--
-- 12-Dec-07/AKa: changed the output to have just the "{ ... }" part; others
--                (variable name) can be explicitly given before the '#include'
-- 16-Nov-07/AKa: taken into luaSub
-- 16-Mar-04/AKa: added 'glua_wrap()' support
-- xx-Jan-04/AKa: subdirectory names are not included in debug info
--

local function USAGE()
    io.stderr:write "lua bin2c.lua [filename] [-o output.lch]"
    os.exit(-1)
end

local out_f   -- file to output to (stdout if nil)

local function OUT( ... )
    (out_f or io.stdout): write( ... );     -- ; actually needed by Lua
    (out_f or io.stdout): write "\n"
end

local HEAD= "{ "
local START= '  '
local FILL= '%3d,'
local STOP= ""
local TAIL= "};\n"

--
local function dump( f )
    --
    OUT [[
/* bin2c.lua generated code -- DO NOT EDIT
 *
 * To use from C source: 
 *    char my_chunk[]=
 *    #include "my.lch"
 */]]

    local str= HEAD..'\n'..START
    local len= 0
    
    while true do
        for n=1,20 do
            local c= f:read(1)
            if c then
                str= str..string.format( FILL, string.byte(c) )
                len= len+1
            else
                OUT( str..STOP.. string.format( TAIL, len ) )
                return  -- the end
            end
        end
        OUT(str..STOP)
        str= START
    end
end

--
local function fdump( fn )
    --
    local f= io.open( fn, "rb" )    -- must open as binary
    
    if not f then
        error( "bin2c: cannot open "..fn )
    else
        dump( f )
        f:close()
    end
end

--
local function main( argv )
    --
    local fn= argv.o
    if fn then
        local f,err= io.open( fn, "w" )
        assert( f, "Unable to write '"..fn.."': "..(err or "") )

        out_f= f
    end
    
    if argv[2] then
        USAGE()
    elseif argv[1] then
        fdump( argv[1] )
    else    -- use stdin (no params)
        if os.getenv("WINDIR") then
            error "using stdin not allowed on Win32!"  -- it wouldn't be binary
        end
        dump(io.stdin)
    end
    
    if out_f then
        out_f:close()
    end
end

--
local argv= {}
local valid_flags= { o=1 }    -- lookup: 0=no value, 1=value

-- Need to use while since '-o' advances 'i' by 2
--
local args= select('#',...)
local i=1

while i<=args do
    local v= select(i,...)
    local flag= string.match( v, "^%-(.+)" )
    
    if flag then
        if not valid_flags[flag] then
            error( "Unknown flag: -"..flag )
        end
        argv[flag]= (i+1<=args) and select(i+1,...) or true
        i= i+1
    else
        table.insert( argv, v )   -- [1..N]
    end
    i= i+1
end

return main(argv)
