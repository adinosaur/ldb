
package.cpath = package.cpath .. ";./build/?.so"

local ldb = require "ldb"

local function f()
    local t = {keke=1, hehe=2}
    
    print("hehe")
    local a = 1
    print(t.keke)

    for i = 1, 10, 1 do
        a = a + i
        if i == 8 then
            print(i, a)
        end
    end
    
    return a
end

ldb.set_trace()
local r = f()
print(r)
print("kk")