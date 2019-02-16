# mod_solvesudoku
mod_solvesudoku.c 是一个解数独的 Apache2 模块，mod_solvesudoku.c 处理的 URL 是：

    http://主机IP地址/solvesudoku

只允许 HTTP 的 POST 方式，接收的内容格式是（“#”在传输的过程中会被编码为“%23”，“;”在传输的过程中会被编码为“%3B”）：

    # 注释;.........;.........;.........;.........;.........;.........;.........;.........;.........

上述 “.........” 代表一行数字，有数字的地方填数字，没数字的地方填 “.”。例如：

    #=;75.....21;....8....;.3.....9.;..16582..;3.......4;2.91.48.5;6..349..2;...2.5...;....7....

发送的结果是 9 行数字，以换行符间隔。例如：

    758963421
    926481357
    134527698
    471658239
    385792164
    269134875
    617349582
    893215746
    542876913

## 配置编译：
需要以下软件包，安装命令以 Ubuntu 为例
1. build-essential    安装命令：sudo apt-get install build-essential
2. GNU make           安装命令：sudo apt-get install make
3. apache2            安装命令：sudo apt-get install apache2
4. apache2-dev        安装命令：sudo apt-get install apache2-dev

### 1.编译：
打开终端，将目录切换到 Makefile 所在目录，输入

    make all

### 2.安装：
输入命令：

    make install

打开 Apache2 的配置文件 apache2.conf (一般在 /etc/apache2)，在最后添加以下几行：

(“/usr/lib/apache2/modules/mod_solvesudoku.so” 是编译的模块的路径，根据实际情况更改)

    LoadModule solvesudoku_module /usr/lib/apache2/modules/mod_solvesudoku.so
    <Location /solvesudoku>
    SetHandler solvesudoku
    </Location>

重新启动 Apache2 ：

    sudo apachectl restart

### 3.卸载：
输入命令：

    make uninstall

并删除在 apache2.conf 中添加的几行内容
