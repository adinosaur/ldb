# ldb
**ldb** defines an interactive source code debugger for LUA programs like Python's pdb module.

# Usage
First you need to insert a trace point in your programs.
```lua

local a = 1

local ldb = require "ldb"
ldb.set_trace() -- <== start trace here

print("kk")
```

After call function `set_trace()`, programs will stop running, and open an interactive console.
```
> 6    print("kk")
(ldb)
```

You can debug your programs here. For example, set breakpoints, step program excution, print variable and so on.
For more infomation, you can just type `help` to list all debug commands:
```
(ldb) h
h (help)                -- Print help msg
bt (backtrace)          -- Print function call info
l (list)                -- Print source code
n (next)                -- Keep going until next line (skip function call)
s (step)                -- Setp into function call
c (continue)            -- Continue excute code
p (print) expr          -- Print expr code (use lua `print` fucntion)
expr code               -- Excute lua expr
f (frame) stacklevel    -- Jump to `stacklevel` function call
fin (finish)            -- Finish current function call
b (break) lineno [if cond] -- Set a breakpoint in currentfile:lineno. If cond present, it will not stop excute unitl cond is true
d (delete) [breakpoint id] -- Delete a breakpoint. If not args, it will delete all breakpoints
q (quit)                -- Stop tracing
(ldb)
```
