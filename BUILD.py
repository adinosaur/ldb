# -*- coding=utf-8 -*-

from __main__ import SharedLibraryTarget
import glob

class LDB(SharedLibraryTarget):
    def __init__(self):
        super(LDB, self).__init__()
        
        self.name = 'ldb.so'
        
        self.cxxflags = ['-g', '-std=c++11', '-fPIC']   # 编译参数
        self.incs = []                                  # 头文件搜索路径
        self.srcs = glob.glob('src/*.cpp')              # 源文件列表

        self.deps = []                                  # 链接的依赖文件
        self.ldflags = ['-rdynamic']                    # 链接的参数
        self.libs = []                                  # 链接的库文件

LDB()