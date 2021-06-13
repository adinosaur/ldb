
package.cpath = package.cpath .. ";./build/?.so"

local ldb = require "ldb"

local gt = {jjj=1}

local function f()
    local t = {keke=1, hehe=2}
    
    print("hehe")
    local a = 1
    print(t.keke)

    for i = 1, 10, 1 do
        a = a + i
        if i == 8 then
            print(i, a)
            gt.jjj=2
        end
        local b = a - 1
    end
    
    return a
end

-- ldb.set_trace()
require "croissant.debugger"()
local r = f()
print(r)
print("kk")