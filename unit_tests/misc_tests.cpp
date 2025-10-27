#include "_pch.hpp"

#include "shared.h"

// #################################################################################################
// #################################################################################################

TEST_CASE("misc.__lanesconvert.for_tables")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };
    S.requireSuccess("lanes = require 'lanes'.configure()");

    S.requireSuccess(
        " l = lanes.linda()"
        " t = setmetatable({}, {__lanesconvert = lanes.null})" // table with a nil-converter
        " l:send('k', t)" // send the table
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and type(out) == 'nil', 'got ' .. key .. ' ' .. tostring(out))" // should have become nil
    );

    S.requireSuccess(
        " l = lanes.linda()"
        " t = setmetatable({}, {__lanesconvert = 'decay'})" // table with a decay-converter
        " l:send('k', t)" // send the table
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and type(out) == 'userdata', 'got ' .. key .. ' ' .. tostring(out))" // should have become a light userdata
    );

    S.requireSuccess(
        " l = lanes.linda()"
        " t = setmetatable({}, {__lanesconvert = function(t, hint) return 'keeper' end})" // table with a string-converter
        " l:send('k', t)" // send the table
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and out == 'keeper')" // should become 'keeper', the hint that the function received
    );

    // make sure that a function that returns the original object causes an error (we don't want infinite loops during conversion)
    S.requireFailure(
        " l = lanes.linda()"
        " t = setmetatable({}, {__lanesconvert = function(t, hint) return t end})" // table with a string-converter
        " l:send('k', t)" // send the table, it should raise an error because the converter triggers an infinite loop
    );
}

// #################################################################################################
// #################################################################################################

TEST_CASE("misc.__lanesconvert.for_userdata")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };
    S.requireSuccess("lanes = require 'lanes'.configure()");
    S.requireSuccess("fixture = require 'fixture'");

    S.requireSuccess("u_tonil = fixture.newuserdata{__lanesconvert = lanes.null}; assert(type(u_tonil) == 'userdata')");
    S.requireSuccess(
        " l = lanes.linda()"
        " l:send('k', u_tonil)" // send a full userdata with a nil-converter
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and type(out) == 'nil')" // should become nil
    );

    S.requireSuccess("u_tolud = fixture.newuserdata{__lanesconvert = 'decay'}; assert(type(u_tolud) == 'userdata')");
    S.requireSuccess(
        " l = lanes.linda()"
        " l:send('k', u_tolud)" // send a full userdata with a decay-converter
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and type(out) == 'userdata' and getmetatable(out) == nil)" // should become a light userdata
    );

    S.requireSuccess("u_tostr = fixture.newuserdata{__lanesconvert = function() return 'yo' end}; assert(type(u_tostr) == 'userdata')");
    S.requireSuccess(
        " l = lanes.linda()"
        " l:send('k', u_tostr)" // send a full userdata with a string-converter
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and out == 'yo')" // should become 'yo'
    );

    // make sure that a function that returns the original object causes an error (we don't want infinite loops during conversion)
    S.requireSuccess("u_toself = fixture.newuserdata{__lanesconvert = function(u) return u end}; assert(type(u_toself) == 'userdata')");
    S.requireFailure(
        " l = lanes.linda()"
        " l:send('k', u_toself)" // send the userdata, it should raise an error because the converter triggers an infinite loop
    );

    // TODO: make sure that a deep userdata with a __lanesconvert isn't converted (because deep copy takes precedence)
}

// #################################################################################################
// #################################################################################################

TEST_CASE("misc.convert_fallback.unset")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };
    S.requireSuccess("lanes = require 'lanes'.configure()");

    S.requireSuccess(
        " l = lanes.linda()"
        " l:send('k', {})" // send a table without a metatable
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and type(out) == 'table')" // should not change
    );

    S.requireSuccess("fixture = require 'fixture'; u = fixture.newuserdata(); assert(type(u) == 'userdata')");
    S.requireFailure(
        " l = lanes.linda()"
        " l:send('k', u)" // send a full userdata without a metatable, should fail
    );
}

// #################################################################################################
// #################################################################################################

TEST_CASE("misc.convert_fallback.decay")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };
    S.requireSuccess("lanes = require 'lanes'.configure{convert_fallback = 'decay'}");
    S.requireSuccess("fixture = require 'fixture'");

    S.requireSuccess(
        " l = lanes.linda()"
        " l:send('k', {})" // send a table without a metatable
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and type(out) == 'userdata' and getmetatable(out) == nil)" // should have become a light userdata
    );

    S.requireSuccess("u = fixture.newuserdata(); assert(type(u) == 'userdata')");
    S.requireSuccess(
        " l = lanes.linda()"
        " l:send('k', u)" // send a non-copyable non-deep full userdata
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and type(out) == 'userdata' and getmetatable(out) == nil)" // should have become a light userdata
    );
}

// #################################################################################################
// #################################################################################################

TEST_CASE("misc.convert_fallback.convert_no_nil")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };
    S.requireSuccess("lanes = require 'lanes'; lanes.configure{convert_fallback = lanes.null}");

    S.requireSuccess(
        " l = lanes.linda()"
        " l:send('k', {})" // send a table without a metatable
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and type(out) == 'nil')" // should have become nil
    );

    S.requireSuccess(
        " l = lanes.linda()"
        " t = setmetatable({}, {__lanesconvert = 'decay'})" // override global converter with our own
        " l:send('k', t)" // send the table
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and type(out) == 'userdata', 'got ' .. key .. ' ' .. tostring(out))" // should have become a light userdata
    );
}

// #################################################################################################
// #################################################################################################

TEST_CASE("misc.convert_max_attempts.is_respected")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };
    S.requireSuccess("lanes = require 'lanes'; lanes.configure{convert_max_attempts = 3}");
    S.requireSuccess("l = lanes.linda()");

    S.requireSuccess(
        " t = setmetatable({n=1}, {__lanesconvert = function(t, hint) t.n = t.n - 1 return t.n > 0 and t or 'done' end})" // table with a string-converter
        " l:send('k', t)" // send the table
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and out == 'done', 'got ' .. key .. ' ' .. tostring(out))" // should have stayed a table
    );

    S.requireSuccess(
        " t = setmetatable({n=2}, {__lanesconvert = function(t, hint) t.n = t.n - 1 return t.n > 0 and t or 'done' end})" // table with a string-converter
        " l:send('k', t)" // send the table
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and out == 'done', 'got ' .. key .. ' ' .. tostring(out))" // should have stayed a table
    );

    S.requireSuccess(
        " t = setmetatable({n=3}, {__lanesconvert = function(t, hint) t.n = t.n - 1 return t.n > 0 and t or 'done' end})" // table with a string-converter
        " l:send('k', t)" // send the table
        " key, out = l:receive('k')" // read it back
        " assert(key == 'k' and out == 'done', 'got ' .. key .. ' ' .. tostring(out))" // should have stayed a table
    );

    S.requireFailure(
        " t = setmetatable({n=4}, {__lanesconvert = function(t, hint) t.n = t.n - 1 return t.n > 0 and t or 'done' end})" // table with a string-converter
        " l:send('k', t)" // send the table, it should raise an error because the converter retries too many times
    );
}

#define MAKE_TEST_CASE(DIR, FILE, CONDITION) \
    TEST_CASE("scripted_tests." #DIR "." #FILE) \
    { \
        FileRunner _runner(R"(.\unit_tests\scripts)"); \
        _runner.performTest(FileRunnerParam{ #DIR "/" #FILE, TestType::CONDITION }); \
    }

MAKE_TEST_CASE(misc, verbose_errors, AssertNoLuaError)
