#!/usr/bin/env tarantool

---
--- gh-4581: Increase limits for debug.traceback
---

local test = require('tap').test('traceback test')
test:plan(1)

-- Currently there is no Lua way to detect the values of
-- LUA_TRACEBACK_LEVEL1, LUA_TRACEBACK_LEVEL2, LUA_IDSIZE. Therefore
-- taking into account the default values configured for Tarantool
-- build and considering the existing testing pipeline we can hardcode
-- the corresponding values within this test suite to check the output
-- produced via debug.backtrace.
local LUA_TRACEBACK_LEVEL1 = 25
local LUA_TRACEBACK_LEVEL2 = 25

local TRACEBACK_MSG1 = 'recursion limit reached!'
local TRACEBACK_MSG2 = 'stack traceback:'
local TRACEBACK_PATTERN = 'tarantool%-4581%-traceback%.test%.lua:[0-9]+: in .*'

-- We use local output variable instead of returning function
-- value recursively since luajit optimizes end recursion
-- and in that case we would only get 2 stack frames for traceback
local output

-- Recursive function which generates stack frames for debug.traceback
local function frec(rec_limit)
    if rec_limit <= 0 then
        output = debug.traceback(TRACEBACK_MSG1)
        -- Strip path from the output and return it
        output = output:gsub("[^\n]*/", "")
    else
        frec(rec_limit - 1)
    end
end

-- Call debug.traceback with specified recursion depth
frec(100)

local test_ok = false

-- Split output into strings
local strings = {}
for each in output:gmatch("([^\n]+)") do
   table.insert(strings, each)
end

-- Total output string count
local count = 0
    + 2 -- header
    + (LUA_TRACEBACK_LEVEL1-1) -- first part
    + 1 -- three dots
    + (LUA_TRACEBACK_LEVEL2) -- second part

-- Check output strings
if count == table.getn(strings) then
    for i = 1,count,1 do
        local pattern

        if i == 1 then 
            pattern = TRACEBACK_MSG1
        elseif i == 2 then 
            pattern = TRACEBACK_MSG2
        elseif i ~= LUA_TRACEBACK_LEVEL1 + 2 then
            pattern = TRACEBACK_PATTERN
        else
            pattern = '%.%.%.'
        end

        test_ok = string.match(strings[i], pattern) ~= nil
        if not test_ok then
            break
        end
    end
end

test:is(test_ok, true)
