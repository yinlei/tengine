# tengine

tengine

## 编译引擎

* window

  ```
  build.bat
  ```

* linux 首先安装mysqlclient库
  ```
  sudo apt install libmysqlclient-dev
  ```
    
  编译
  ```
  ./premake gmake

  cd build

  make config=debug64
  ```

## 创建引擎运行环境

```
create.bat
```

### 输入目录名 `demo`， 目录结构如下:

* demo
  * bin         引擎执行文件以及用到的动态库
  * tengine     引擎核心脚本

  * boot        你需要创建的启动服务目录
    * main.lua  你需要创建的服务启动文件

  * service1    你创建的服务目录
  * service2    你创建的服务目录
  
  * start.bat   启动

### 创建服务启动文件

创建`lua`文件 `main.lua`

```lua
local T = require "tengine" -- 引入引擎脚本

T.p("hello world.")
T.TRACE_MSG("hello world.")
T.DEBUG_MSG("hello world.")
T.INFO_MSG("hello world.")
T.NOTICE_MSG("hello world.")
T.WARNING_MSG("hello world.")
T.ERROR_MSG("hello world.")
T.CRITICAL_MSG("hello world.")

```

# Licence

none

