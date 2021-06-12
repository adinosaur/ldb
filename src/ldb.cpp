
#include <lua.hpp>

#include <readline/readline.h>
#include <readline/history.h>

#include <stdio.h>
#include <assert.h>

#include <map>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>


std::vector<std::string>& split(const std::string &s, char delim, std::vector<std::string> &elems)
{
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim))
    {
        if (item.length() > 0)
            elems.push_back(item);
    }
    return elems;
}

static int env_index(lua_State* L)
{
    int locals = lua_upvalueindex(1);
    int upvalues = lua_upvalueindex(2);
    const char* name = lua_tostring(L, 2);

    // search upvalues
    lua_getfield(L, upvalues, name);
    if (lua_isnil(L, -1) == 0)
        return 1;
    lua_pop(L, 1);

    // search _ENV
    lua_getfield(L, upvalues, "_ENV");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, name);
        if (lua_isnil(L, -1) == 0)
            return 1;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    // search locals
    lua_getfield(L, locals, name);
    if (lua_isnil(L, -1) == 0)
        return 1;
    lua_pop(L, 1);

    // search global
    lua_getglobal(L, name);
    if (lua_isnil(L, -1) == 0)
        return 1;
    lua_pop(L, 1);
    return 0;
}

static int env_newindex(lua_State* L)
{
    int table_index = 1;
    int key_index = 2;
    int value_index = 3;

    const char* key = lua_tostring(L, key_index);

    lua_Debug entry;
    if (lua_getstack(L, 2, &entry) <= 0)
        return 0;
    
    int status = lua_getinfo(L, "f", &entry);
    if (status <= 0)
        return 0;

    int function_index = lua_gettop(L);

    // find from locals
    for (int i = 1;; i++)
    {
        const char* name = lua_getlocal(L, &entry, i);
        if (!name)
            break;
        lua_pop(L, 1);
        if (strcmp(key, name) == 0)
        {
            lua_pushnil(L);
            lua_copy(L, value_index, -1);
            lua_setlocal(L, &entry, i);

            const char* value = lua_tostring(L, value_index);
            printf("set local %s=%s\n", key, value);
            return 0;
        }
    }

    // find from upvalues
    for (int i = 0;; i++)
    {
        const char* name = lua_getupvalue(L, function_index, i);
        if (!name)
            break;
        lua_pop(L, 1);
        if (strcmp(key, name) == 0)
        {
            lua_setupvalue(L, function_index, i);
            const char* value = lua_tostring(L, value_index);
            printf("set upvalue %s=%s\n", key, value);
            return 0;
        }
    }

    // set global
    {
        lua_pushnil(L);
        lua_copy(L, value_index, -1);
        lua_setglobal(L, key);
        lua_pop(L, 1);

        const char* value = lua_tostring(L, value_index);
        printf("set global %s=%s\n", key, value);
        return 0;
    }

    return 0;
}

static int create_env(lua_State* L, int stacklevel)
{
    lua_Debug entry;

    if (!lua_getstack(L, stacklevel, &entry))
        return 1;
    
    if (!lua_getinfo(L, "nSlu", &entry))
        return 1;
    
    lua_newtable(L);
    int env = lua_gettop(L);

    lua_newtable(L);
    int mt = lua_gettop(L);
    
    lua_newtable(L);
    int locals = lua_gettop(L);
    
    lua_newtable(L);
    int upvalues = lua_gettop(L);

    // locals
    for(int i = 1;; i++)
    {
        const char* name = lua_getlocal(L, &entry, i);
        if (name == nullptr)
            break;
        if (name[0] == '(')
        {
            lua_pop(L, 1);
            continue;
        }
        lua_setfield(L, locals, name);
    }

    // upvalues
    if (lua_getinfo(L, "f", &entry))
    {
        int function = lua_gettop(L);
        for (int i = 1;; i++)
        {
            const char* name = lua_getupvalue(L, function, i);
            if (name == nullptr)
                break;
            lua_setfield(L, upvalues, name);
        }
        lua_pop(L, 1);
    }

    int top = lua_gettop(L);
    assert(top == upvalues);

    // metamethod: __index
    // upvalues: [1]=locals, [2]=upvales
    lua_pushcclosure(L, env_index, 2);
    lua_setfield(L, mt, "__index");

    // metamethod: __newindex
    lua_pushcclosure(L, env_newindex, 0);
    lua_setfield(L, mt, "__newindex");

    lua_setmetatable(L, env);
    
    top = lua_gettop(L);
    assert(top == env);

    return 0;
}

static int bpid;

class BreakPoint
{
public:
    BreakPoint()
    : id(++bpid)
    {}

    int get_id() { return id; }

    void setup(lua_State* L, lua_Debug* entry, int lineno, const std::string& cond)
    {
        this->source = entry->source;
        this->lineno = lineno;
        this->condition = cond;
    }

    bool ishit(lua_State* L, lua_Debug* entry)
    {
        if (lineno != entry->currentline || source != entry->source)
            return false;

        if (condition.empty())
            return true;
        
        bool ishit = false;
        if (!condition.empty())
        {
            std::string expr = "return " + condition;
            int r = luaL_loadstring(L, expr.c_str());
            if (r != LUA_OK)
            {
                printf("setup breakpoint failed, invalid cond: %s\n", condition.c_str());
                return -1;
            }

            int cond_index = lua_gettop(L);
            create_env(L, 0);
            lua_setupvalue(L, cond_index, 1);

            r = lua_pcall(L, 0, 1, 0);
            if (r != LUA_OK)
            {
                printf("check breakpoint %d condition failed, error: %s\n", id, lua_tostring(L, -1));
                ishit = true;
            }
            else
            {
                if (lua_toboolean(L, -1))
                    ishit = true;
                lua_pop(L, 1);
            }
        }
        return ishit;
    }

    void print()
    {
        if (condition.empty())
            printf("break %d in %s:%d\n", id, source.c_str(), lineno);
        else
            printf("break %d in %s:%d if %s\n", id, source.c_str(), lineno, condition.c_str());
    }

private:
    int id;
    std::string source;
    int lineno;
    std::string condition;
};

#define PROMPT "(ldb) "

class ldb
{
public:
    ldb()
    : L(nullptr)
    , ar(nullptr)
    , breaks()
    , running(true)
    , cmdstop(false)
    , step(false)
    , next(false)
    , callfunclevel(0)
    , traceframe(0)
    , stopframe(-1)
    , lastline()
    {}

    void user_line()
    {
        do_list(0);
        cmdloop();
    }

    void cmdloop()
    {
        cmdstop = false;
        while (!cmdstop)
        {
            char* line = readline(PROMPT);
            if (!line)
            {
                printf("\n");
                break;
            }
            
            if (strlen(line) == 0)
            {
                onecmd(lastline);
            }
            else
            {
                onecmd(line);
                lastline = line;
                free(line);
            }
            
        }
    }

    void onecmd(const std::string& line)
    {
        std::vector<std::string> inputs;
        split(line, ' ', inputs);

        if (inputs.empty())
            return;
        
        std::string cmd = inputs[0];

        if (cmd == "h" || cmd == "help")
            do_help();
        else if (cmd == "bt" || cmd == "backtrace")
            do_backtrace();
        else if (cmd == "l" || cmd == "list")
            do_list();
        else if (cmd == "n" || cmd == "next")
            do_next();
        else if (cmd == "s" || cmd == "step")
            do_step();
        else if (cmd == "c" || cmd == "continue")
            do_continue();
        else if (cmd == "p")
            do_print(inputs);
        else if (cmd == "expr")
            do_expr(inputs);
        else if (cmd == "f" || cmd == "frame")
            do_frame(inputs);
        else if (cmd == "fin" || cmd == "finish")
            do_finish();
        else if (cmd == "b" || cmd == "break")
            do_break(inputs);
        else if (cmd == "d" || cmd == "delete")
            do_delete(inputs);
        else if (cmd == "q" || cmd == "quit")
            do_quit();
    }

    // cmd: help
    void do_help()
    {
        const char* HELP =  "h (help)\t\t-- Print help msg\n"
                            "bt (backtrace)\t\t-- Print function call info\n"
                            "l (list)\t\t-- Print source code\n"
                            "n (next)\t\t-- Keep going until next line (skip function call)\n"
                            "s (step)\t\t-- Setp into function call\n"
                            "c (continue)\t\t-- Continue excute code\n"
                            "p (print) expr\t\t-- Print expr code (use lua `print` fucntion)\n"
                            "expr code\t\t-- Excute lua expr\n"
                            "f (frame) stacklevel\t-- Jump to `stacklevel` function call\n"
                            "fin (finish)\t\t-- Finish current function call\n"
                            "b (break) lineno [if cond] -- Set a breakpoint in currentfile:lineno. If cond present, it will not stop excute unitl cond is true\n"
                            "d (delete) [breakpoint id] -- Delete a breakpoint. If not args, it will delete all breakpoints\n"
                            "q (quit)\t\t-- Stop tracing\n";
        std::cout << HELP;
    }

    // cmd: backtrace
    void do_backtrace()
    {
        lua_Debug entry;
        for (int level = 0;; level++)
        {
            if (lua_getstack(L, level, &entry) == 0)
                break;
            int status = lua_getinfo(L, "Sln", &entry);
            if (status <= 0)
                break;
            printf("#%d in %s at %s:%d\n", level, entry.name ? entry.name : "?", entry.short_src, entry.currentline);
        }
    }

    // cmd: l(ist)
    void do_list(int around=10)
    {
        lua_Debug entry;
        lua_getstack(L, traceframe, &entry);
        lua_getinfo(L, "Sln", &entry);

        const char* src = entry.source;
        if (src[0] == '@')
        {
            std::string filepath(src+1);
            std::vector<std::string> lines = get_file_lines(filepath, entry.currentline, around);
            for (const std::string& line : lines)
                printf("%s\n", line.c_str());
        }
        else if (src[0] == '=')
        {
            printf("Unknown source from %s\n", src);
        }
    }

    std::vector<std::string> get_file_lines(const std::string& filepath, int targetline, int around)
    {
        std::vector<std::string> outs;

        // 如果文件路径和缓存的不一致，则重新读新的文件
        if (filepath != cache_filename)
        {
            cache_lines.clear();
            std::ifstream input(filepath);
            std::string line;
            while (std::getline(input, line))
                cache_lines.push_back(line);
            cache_filename = filepath;
        }

        int beginline = std::max(1, targetline - around);
        int endline = std::min((int)cache_lines.size(), targetline + around);

        for (int i = beginline; i <= endline; i++)
        {
            std::ostringstream os;
            if (i == targetline)
                os << " > " << i << "\t" << cache_lines[i-1];
            else
                os << "   " << i << "\t" << cache_lines[i-1];
            outs.push_back(os.str());
        }

        return outs;
    }

    // cmd: n(ext)
    void do_next()
    {
        next = true;
        callfunclevel = 0;
        do_continue();
    }

    // cmd: s(tep)
    void do_step()
    {
        step = true;
        do_continue();
    }

    // cmd: c(ontinue)
    void do_continue()
    {
        cmdstop = true;
    }

    // cmd: p(rint)
    void do_print(std::vector<std::string>& inputs)
    {
        if (inputs.size() < 2)
            return;
        std::string expr = "print(" + inputs[1] + ")";
        inputs[1] = expr;
        do_expr(inputs);
    }

    // cmd: expr
    void do_expr(std::vector<std::string>& inputs)
    {
        if (inputs.size() < 2)
            return;
        
        int r = luaL_loadstring(L, inputs[1].c_str());
        if (r != LUA_OK)
            return;
        
        int function_index = lua_gettop(L);
        create_env(L, traceframe);
        lua_setupvalue(L, function_index, 1);
        lua_pcall(L, 0, 1, 0);
    }

    // cmd: f(rame)
    void do_frame(std::vector<std::string>& inputs)
    {
        if (inputs.size() < 2)
            return;
        
        int frame = 0;
        try
        {
            frame = std::stoi(inputs[1]);
        }
        catch (std::invalid_argument e)
        {
            printf("invalid frame: %s\n", inputs[1].c_str());
            return;
        }

        lua_Debug entry;
        for (int level = 0;; level++)
        {
            if (lua_getstack(L, level, &entry) == 0)
                break;
            int status = lua_getinfo(L, "Sln", &entry);
            if (status <= 0)
                break;
            if (frame == level)
            {
                traceframe = frame;
                do_list(0);
                return;
            }
        }
        
        printf("invalid frame: %d\n", frame);
    }

    // cmd: fin(ish)
    void do_finish()
    {
        stopframe = get_stacksize() - 1;
        do_continue();
    }

    // cmd: b(reak)
    void do_break(std::vector<std::string>& inputs)
    {
        if (inputs.size() < 2)
        {
            // list breaks
            for (auto bp : breaks)
                bp->print();
            return;
        }
        
        int lineno = -1;
        try
        {
            lineno = std::stoi(inputs[1]);
        }
        catch (std::invalid_argument e)
        {
            printf("invalid lineno: %s\n", inputs[1].c_str());
            return;
        }

        std::string cond;
        if (inputs.size() > 3 && inputs[2] == "if")
        {
            for (int i = 3; i < inputs.size(); i++)
                cond += inputs[i];
        }

        lua_Debug entry;
        lua_getstack(L, traceframe, &entry);
        lua_getinfo(L, "Sln", &entry);
        
        BreakPoint* bp = new BreakPoint();
        bp->setup(L, &entry, lineno, cond);
        breaks.push_back(bp);
        bp->print();
    }

    // cmd: d(elete)
    void do_delete(std::vector<std::string>& inputs)
    {
        if (inputs.size() < 2)
        {
            // list breaks
            for (auto bp : breaks)
            {
                delete bp;
            }
            breaks.clear();
            return;
        }
        
        int id = -1;
        try
        {
            id = std::stoi(inputs[1]);
        }
        catch (std::invalid_argument e)
        {
            printf("invalid breakpoint id: %s\n", inputs[1].c_str());
            return;
        }

        for (auto it = breaks.begin(); it != breaks.end(); it++)
        {
            BreakPoint* bp = *it;
            if (bp->get_id() == id)
            {
                delete bp;
                breaks.erase(it);
                printf("delete breakpoint %d\n", id);
                break;
            }
        }
    }

    // cmd: d(elete)
    void do_quit()
    {
        running = false;
        do_continue();
    }

    int get_stacksize()
    {
        lua_Debug entry;
        for (int level = 0;; level++)
            if (lua_getstack(L, level, &entry) == 0)
                return level;
        return -1;
    }

    void set_step(bool step)
    {
        this->step = step;
    }

    void set_next(bool next)
    {
        this->next = next;
    }

    bool stop_here()
    {
        if (step)
            return true;
        if (next && callfunclevel == 0)
            return true;
        if (stopframe != 1)
        {
            int stacksize = get_stacksize();
            if (stopframe == stacksize)
                return true;
        }
        return false;
    }

    bool break_here()
    {
        lua_Debug entry;
        lua_getstack(L, 0, &entry);
        lua_getinfo(L, "Sln", &entry);
        for (auto bp : breaks)
            if (bp->ishit(L, &entry))
                return true;
        return false;
    }

    void trace_dispatch(lua_State* L, lua_Debug* ar)
    {
        if (!running)
            return;

        this->L = L;
        this->ar = ar;

        switch (ar->event)
        {
            case LUA_HOOKLINE: dispatch_line(); break;
            case LUA_HOOKCALL: dispatch_call(); break;
            case LUA_HOOKTAILCALL: dispatch_tailcall(); break;
            case LUA_HOOKRET: dispatch_return(); break;
            case LUA_HOOKCOUNT: dispatch_count(); break;
        }
    }

    void reset()
    {
        step = false;
        next = false;
        traceframe = 0;
        stopframe = -1;
    }

    void dispatch_line()
    {
        if (stop_here() || break_here())
        {
            reset();
            user_line();
        }
    }

    void dispatch_call()
    {
        callfunclevel++;
    }

    void dispatch_tailcall()
    {
        callfunclevel++;
    }

    void dispatch_return()
    {
        callfunclevel--;
    }

    void dispatch_count()
    {
        
    }

private:
    lua_State* L;
    lua_Debug* ar;

    std::vector<BreakPoint*> breaks;

    std::string cache_filename;
    std::vector<std::string> cache_lines;

    bool running;
    bool cmdstop;
    bool step;
    bool next;
    int callfunclevel;
    int traceframe;
    int stopframe;
    std::string lastline;
};


// =========================
// Debug Hook
// =========================
static lua_Hook g_old_hook;
static int g_old_hook_mask;
static int g_old_hook_count;

static ldb g_ldb;

static void debug_hook(lua_State *L, lua_Debug *ar)
{
    g_ldb.trace_dispatch(L, ar);
}

static int install_hook(lua_State *L)
{
    // 备份旧的hook函数
    lua_Hook hook = lua_gethook(L);
    if (!g_old_hook)
    {
        g_old_hook = hook;
        g_old_hook_mask = lua_gethookcount(L);
        g_old_hook_count = lua_gethookmask(L);
    }

    g_ldb.set_step(true);

    // 注册hook函数
    if (hook != debug_hook)
    {
        lua_sethook(L, debug_hook, LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE, 0);
    }

    return 0;
}


static int ldb_set_trace(lua_State *L)
{
    install_hook(L);

    return 0;
}

static const luaL_Reg ldblib[] = {
    {"set_trace", ldb_set_trace},
    {NULL, NULL}
};

extern "C" int luaopen_ldb(lua_State* L)
{
    luaL_newlib(L, ldblib);
    return 1;
}