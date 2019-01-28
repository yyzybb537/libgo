# libgo

[![Build Status](https://travis-ci.org/yyzybb537/libgo.svg?branch=master)](https://travis-ci.org/yyzybb537/libgo)

### libgo  - 协程库、并行编程库

libgo是一个使用C++11编写的协作式调度的stackful协程库, 同时也是一个强大易用的并行编程库

目前支持三个平台:

    Linux

    MacOSX
    
    Windows (Win7及以上 x86 or x64 使用VS2015/2017编译)

使用libgo编写多线程程序，即可以像golang、erlang这些并发语言一样开发迅速且逻辑简洁，又有C++原生的性能优势，鱼和熊掌从此可以兼得。

libgo有以下特点：

 *   1.提供golang一般功能强大协程，基于corontine编写代码，可以以同步的方式编写简单的代码，同时获得异步的性能，
 *   2.支持海量协程, 创建100万个协程只需使用4.5GB物理内存.(真实值, 而不是刻意压缩stack得到的测试值)
 *   3.支持多线程调度协程, 提供高效的负载均衡策略和协程同步机制, 很容易编写高效的多线程程序.
 *   4.调度线程数支持动态伸缩, 不再有调度慢协程导致头部阻塞效应的问题.
 *   5.使用hook技术让链接进程序的同步的第三方库变为异步调用，大大提升其性能。再也不用担心某些DB官方不提供异步driver了，比如hiredis、mysqlclient这种客户端驱动可以直接使用，并且可以得到不输于异步driver的性能。
 *   6.动态链接和全静态链接均支持，便于使用C++11的用户静态链接生成可执行文件并部署至低版本的linux系统上。
 *   7.提供Channel, 协程锁(co_mutex), 协程读写锁(co_rwmutex), 定时器等特性, 帮助用户更加容易地编写程序. 
 *   8.支持协程局部变量(CLS), 并且完全覆盖TLS的所有使用场景(详见教程代码sample13_cls.cpp).

 *   从近两年的用户反馈情况看，有很多用户都是已经有了一个异步非阻塞模型的项目(可能是基于epoll、libuv或asio等网络库)，然后需要访问MySQL这类没有提供异步Driver的DB. 常规的连接池+线程池的方案在高并发场景下的开销十分昂贵(每个连接对应一个线程才能达到最佳性能, 几千个指令周期的线程上下文切换消耗+过多的活跃线程会导致OS的调度能力急剧下降), 让许多用户难以接受.

 *   鉴于此种情况, 想要使用libgo解决非阻塞模型中阻塞操作的问题，也是完全不必重构现有代码的, 全新的libgo3.0为此场景量身打造了三大利器, 可以无侵入地解决这个问题：运行环境隔离又可以便捷交互的多调度器(详见教程代码sample1_go.cpp)，替代传统线程池方案的libgo协程池(详见教程代码sample10_co_pool.cpp)，连接池(详见教程代码sample11_connection_pool.cpp)
 
 *   **tutorial目录下有很多教程代码，内含详细的使用说明，让用户可以循序渐进的学习libgo库的使用方法。**

 *   如果你发现了任何bug、有好的建议、或使用上有不明之处，可以提交到issue，也可以直接联系作者:
      email: 289633152@qq.com

 
### libgo的编译与使用:

 *    Vcpkg:

        如果你已经安装了vcpkg，可以直接使用vcpkg安装：

            $ vcpkg install libgo

 *    Linux: 

        1.使用CMake进行编译安装：

            $ mkdir build
            $ cd build
            $ cmake ..
            $ sudo make uninstall
            $ sudo make install

          如果希望编译可调试的版本, "cmake .." 命令执行完毕后执行:

            $ make debug
			$ sudo make install

        2.动态链接glibc: (libgo放到最前面链接)
        
            g++ -std=c++11 test.cpp -llibgo -ldl [-lother_libs]
            
        3.全静态链接: (libgo放到最前面链接)

            g++ -std=c++11 test.cpp -llibgo -Wl,--whole-archive -lstatic_hook -lc -lpthread -Wl,--no-whole-archive [-lother_libs] -static

 *    Windows: (3.0已兼容windows, 直接使用master分支即可!)
 
        0.windows上使用github下载代码一定要注意换行符的问题, 请正确安装git(使用默认选项), 使用git clone下载源码.(不可以下载压缩包)
 
        1.使用CMake构建工程文件. 
			
			比如vs2015(x64)：
			$ cmake .. -G"Visual Studio 14 2015 Win64"

			比如vs2015(x86)：
			$ cmake .. -G"Visual Studio 14 2015"
        
        2.如果想要执行测试代码, 需要依赖boost库. 且在cmake参数中设置BOOST_ROOT:
        
        		例如：
        		$ cmake .. -G"Visual Studio 14 2015 Win64" -DBOOST_ROOT="e:\\boost_1_69_0"

### 性能

libgo和golang一样实现了一个完整的调度器（用户只需创建协程，无需关心协程的执行、挂起、资源回收），因此有了和golang对比单线程协程调度性能的资格（功能不对等的情况下没资格做性能对比）。

libgo的调度器还实现了worksteal算法的多线程负载均衡调度，因此有了和golang对比多线程协程调度性能的资格。

	测试环境：2018款13寸mac笔记本（cpu最低配）
	操作系统：Mac OSX
	CPU: 2.3 GHz Intel Core i5（4核心 8线程）
	测试脚本：$ test/golang/test.sh thread_number

<img width="400" src="imgs/switch_cost.png"/>

<img width="600" src="imgs/switch_speed.png"/>

### 注意事项(WARNING)：

	协程中尽量不要使用TLS, 或依赖于TLS实现的不可重入的库函数。
	如果不可避免地使用, 要注意在协程切换后要停止访问切换前产生的TLS数据。

### 可能产生协程切换的行为有以下几种：

* 用户调用co_yield主动让出cpu.
* 竞争协程锁、channel读写
* sleep系列的系统调用
* poll, select, epoll_wait这类等待事件触发的系统调用
* DNS相关系统调用(gethostbyname系列)
* 在阻塞式socket上的connect、accept、数据读写操作
* 在pipe上的数据读写操作


### Linux系统上Hook的系统调用列表：

		connect   
		read      
		readv     
		recv      
		recvfrom  
		recvmsg   
		write     
		writev    
		send      
		sendto    
		sendmsg   
		poll      
		__poll
		select    
		accept    
		sleep     
		usleep    
		nanosleep
		gethostbyname                                                               
		gethostbyname2                                                              
		gethostbyname_r                                                             
		gethostbyname2_r                                                            
		gethostbyaddr                                                               
		gethostbyaddr_r

	以上系统调用都是可能阻塞的系统调用, 在协程中使用均不再阻塞整个线程, 阻塞等待期间CPU可以切换到其他协程执行.
    在原生线程中执行的被HOOK的系统调用, 与原系统调用的行为保持100%一致, 不会有任何改变.
  
		socket
		socketpair
		pipe
		pipe2
		close     
		__close
		fcntl     
		ioctl     
		getsockopt
		setsockopt
		dup       
		dup2      
		dup3      

    以上系统调用不会造成阻塞, 虽然也被Hook, 但并不会完全改变其行为, 仅用于跟踪socket的选项和状态. 

### Windows系统上Hook的系统调用列表：

		ioctlsocket                                                                        
		WSAIoctl                                                                           
		select                                                                             
		connect                                                                            
		WSAConnect                                                                         
		accept                                                                             
		WSAAccept                                                                          
		WSARecv                                                                            
		recv                                                                               
		recvfrom                                                                           
		WSARecvFrom                                                                        
		WSARecvMsg                                                                         
		WSASend                                                                            
		send                                                                               
		sendto                                                                             
		WSASendTo                                                                          
		WSASendMsg
