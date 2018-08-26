生成Visual Studio 2017/2015工程方法：

1.安装git
2.安装cmake, 并将cmake可执行文件加入系统目录
3.启动git bash, 并切换到你正在阅读的这个README文件所在目录
4.执行脚本  ./make_vs_projs.sh
5.在当前目录下会生成不同版本的vs工程文件, 直接使用即可

另外, 如果你想运行测试代码和教程代码, 需要安装boost(编译x64的lib),
并且在执行脚本时给出boost的安装目录, 例如:
  $ ./make_vs_projs.sh -DBOOST_ROOT="E:\\boost_1_68_0"
