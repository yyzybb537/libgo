# libgo


### libgo  - 协程库、并行编程库

libgo是一个使用C++11编写的调度式stackful协程库,

同时也是一个强大的并行编程库, 是专为Linux服务端程序开发设计的底层框架。

目前支持两个平台:

    Linux   (GCC4.8+)
    
    Win7-64bit (VS2013/2015)

使用libgo编写并行程序，即可以像golang、erlang这些并发语言一样开发迅速且逻辑简洁，又有C++原生的性能优势，鱼和熊掌从此可以兼得。

libgo有以下特点：

 *   1.提供golang一般功能强大协程，基于corontine编写代码，可以以同步的方式编写简单的代码，同时获得异步的性能，
 *   2.支持海量协程, 创建100万个协程只需使用1GB内存
 *   3.允许用户自由控制协程调度点，随意变更调度线程数；
 *   4.支持多线程调度协程，极易编写并行代码，高效的并行调度算法，可以有效利用多个CPU核心
 *   5.可以让链接进程序的同步的第三方库变为异步调用，大大提升其性能。再也不用担心某些DB官方不提供异步driver了，比如hiredis、mysqlclient这种客户端驱动可以直接使用，并且可以得到不输于异步driver的性能。
 *   6.动态链接和静态链接全都支持，便于使用C++11的用户静态链接生成可执行文件并部署至低版本的linux系统上。
 *   7.提供协程锁(co_mutex), 定时器, channel等特性, 帮助用户更加容易地编写程序. 
 *   8.网络性能强劲，超越ASIO异步模型；尤其在处理小包和多线程并行方面非常强大。
 
 *   如果你发现了任何bug、有好的建议、或使用上有不明之处，可以提交到issue，也可以直接联系作者:
      email: 289633152@qq.com  QQ交流群: 296561497

 *   samples目录下有很多示例代码，内含详细的使用说明，让用户可以循序渐进的学习libgo库的使用方法。

 
##### libgo的编译与使用:

 *    Linux: 
 
        0.如果你安装了ucorf，那么你已经安装过libgo了，可以跳过第1步.
 
        1.使用CMake进行编译安装：

            $ mkdir build
            $ cd build
            $ cmake .. -DCMAKE_BUILD_TYPE=RELEASE
            $ sudo make install

          如果希望编译可调试的版本, 只需要cmake那行命令变为:

            $ cmake ..
		
		  如果想要使用boost.coroutine作为底层上下文切换（不再是共享栈, 会浪费内存但是不存在第4条WARN的问题），编译命令为：
			
			调试： $ cmake .. -DENABLE_BOOST_COROUTINE=1
			优化： $ cmake .. -DCMAKE_BUILD_TYPE=RELEASE -DENABLE_BOOST_COROUTINE=1

		  如果不希望使用Hook功能，可以使用以下命令进行编译：

			调试： $ cmake .. -DDISABLE_HOOK=1
			优化： $ cmake .. -DCMAKE_BUILD_TYPE=RELEASE -DDISABLE_HOOK=1

		  执行单元测试代码：

			$ make test
			$ ./test.t

        2.以动态链接的方式使用时，一定要最先链接liblibgo.so，还需要链接libdl.so. 例如：
        
            g++ -std=c++11 test.cpp -llibgo -ldl [-lother_libs]
            
        3.以静态链接的方式使用时，只需链接liblibgo.a即可，不要求第一个被链接，但要求libc.a最后被链接. 例如:
        
            g++ -std=c++11 test.cpp -llibgo -static -static-libgcc -static-libstdc++

 *    Windows: 
 
        1.使用git submodule update --init下载子模块
        
        2.在Windows平台上，libgo是依赖boost-x64的静态库的，如果要编译请先编译64bit的boost库:

            推荐编译参数：bjam.exe address-model=64 --build-type=compelete
        
        3.使用CMake构建工程文件. 
			
			比如vs2015：
			$ cmake .. -G"Visual Studio 14 2015 Win64"

			其他编译参数参见Linux平台的编译方式
        
        4.使用时需要添加两个include目录：coroutine和coroutine/windows

##### 注意事项(WARNING)：


        1.在多线程模式下不要使用<线程局部变量>。使用多线程调度时，协程的每次切换，下一次继续执行都可能处于其他线程中

        2.不要让一个代码段耗时过长。协程的调度是协作式调度，需要协程主动让出执行权，推荐在耗时很长的循环中插入一些yield

	    3.除网络IO、sleep以外的阻塞系统调用，会真正阻塞调度线程的运行，请使用co_await, 并启动几个线程去Run内置的线程池.

        4.在Linux系统上且不使用ENABLE_BOOST_COROUTINE参数时，协程栈上对象不可被协程外部访问。
			由于采用共享栈的方式调度协程，协程处于非执行状态时，栈上对象会被保存到另外一块内存中，因此会失效，
			此时通过保存的地址访问栈上对象是一种未定义行为。有共享需求的对象请将其置于堆上或使用channel。
