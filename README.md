# libgo

[![Build Status](https://travis-ci.org/yyzybb537/libgo.svg?branch=master)](https://travis-ci.org/yyzybb537/libgo.svg?branch=master)

### libgo  - 协程库、并行编程库

libgo是一个使用C++11编写的协作式调度的stackful协程库,

同时也是一个强大的并行编程库, 是专为Linux服务端程序开发设计的底层框架。

目前支持两个平台:

    Linux   (GCC4.8+)
    
    Windows (Win7、Win8、Win10 x86 and x64 使用VS2013/2015编译)

使用libgo编写并行程序，即可以像golang、erlang这些并发语言一样开发迅速且逻辑简洁，又有C++原生的性能优势，鱼和熊掌从此可以兼得。

libgo有以下特点：

 *   1.提供golang一般功能强大协程，基于corontine编写代码，可以以同步的方式编写简单的代码，同时获得异步的性能，
 *   2.支持海量协程, 创建100万个协程只需使用2GB物理内存
 *   3.允许用户自由控制协程调度点，随时随地变更调度线程数；
 *   4.支持多线程调度协程，极易编写并行代码，高效的并行调度算法，可以有效利用多个CPU核心
 *   5.可以让链接进程序的同步的第三方库变为异步调用，大大提升其性能。再也不用担心某些DB官方不提供异步driver了，比如hiredis、mysqlclient这种客户端驱动可以直接使用，并且可以得到不输于异步driver的性能。
 *   6.动态链接和静态链接全都支持，便于使用C++11的用户静态链接生成可执行文件并部署至低版本的linux系统上。
 *   7.提供协程锁(co_mutex), 定时器, channel等特性, 帮助用户更加容易地编写程序. 
 *   8.网络性能强劲，超越boost.asio异步模型；尤其在处理小包和多线程并行方面非常强大。
 
 *   如果你发现了任何bug、有好的建议、或使用上有不明之处，可以提交到issue，也可以直接联系作者:
      email: 289633152@qq.com  QQ交流群: 296561497

 *   **tutorial目录下有很多教程代码，内含详细的使用说明，让用户可以循序渐进的学习libgo库的使用方法。**

 
##### libgo的编译与使用:

 *    Linux: 

		0.CMake编译参数
		
			ENABLE_BOOST_CONTEXT  `这是在linux上性能最佳的编译参数`
				libgo在Linux系统上默认使用ucontext做协程上下文切换，开启此选项将使用boost.context来替代ucontext.
				使用方式：
					$ cmake .. -DENABLE_BOOST_CONTEXT=ON

			ENABLE_BOOST_COROUTINE
				libgo在Linux系统上默认使用ucontext做协程上下文切换，开启此选项将使用boost.coroutine来替代ucontext.
				使用方式：
					$ cmake .. -DENABLE_BOOST_COROUTINE=ON

			DISABLE_HOOK
				禁止hook syscall，开启此选项后，网络io相关的syscall将恢复系统默认的行为，
				协程中使用阻塞式网络io将可能真正阻塞线程，如无特殊需求请勿开启此选项.
				使用方式：
					$ cmake .. -DDISABLE_HOOK=ON
					
			不开启ENABLE_BOOST_CONTEXT和ENABLE_BOOST_COROUTINE选项时, libgo不依赖boost库，可以直接使用，仅测试代码依赖boost库。
 
        1.如果你安装了ucorf或libgonet，那么你已经使用默认的方式安装过libgo了，如果不想设置如上的选项，可以跳过第2步.
 
        2.使用CMake进行编译安装：

            $ mkdir build
            $ cd build
            $ cmake ..
            $ sudo make install

          如果希望编译可调试的版本, "cmake .." 命令执行完毕后执行:

            $ make debug
			$ sudo make install

		  执行单元测试代码：

			$ make test
			$ make run_test

		  生成性能网络测试代码：

			$ make bm

        3.以动态链接的方式使用时，一定要最先链接liblibgo.so，还需要链接libdl.so. 
		  例如：
        
            g++ -std=c++11 test.cpp -llibgo -ldl [-lother_libs]
            
        4.以静态链接的方式使用时，只需链接liblibgo.a即可，不要求第一个被链接，但要求libc.a最后被链接. 
		  要求安装GCC的静态链接库, debian系Linux安装gcc时已经自带, redhat系Linux需要从源中另行安装(yum install gcc-static)
		  例如:
        
            g++ -std=c++11 test.cpp -llibgo -static -static-libgcc -static-libstdc++

 *    Windows: 

		0.CMake编译参数
		
			DISABLE_HOOK
				禁止hook syscall，开启此选项后，网络io相关的syscall将恢复系统默认的行为，
				协程中使用阻塞式网络io将可能真正阻塞线程，如无特殊需求请勿开启此选项.
				使用方式：
					$ cmake .. -DDISABLE_HOOK=ON
 
        1.使用git submodule update --init --recursive下载Hook子模块
        
        2.使用CMake构建工程文件. 
			
			比如vs2015(x64)：
			$ cmake .. -G"Visual Studio 14 2015 Win64"

			比如vs2015(x86)：
			$ cmake .. -G"Visual Studio 14 2015"
			
			比如vs2013(x64)：
			$ cmake .. -G"Visual Studio 12 2013 Win64"
        
        3.使用时需要添加两个include目录：src和src/windows, 或将这两个目录下的头文件拷贝出来使用
        
        4.如果想要执行测试代码, 需要依赖boost库. 且在cmake参数中设置BOOST_ROOT:
        
        		例如：
        		$ cmake .. -G"Visual Studio 14 2015 Win64" -DBOOST_ROOT="e:\\boost_1_61_0"

##### 注意事项(WARNING)：
* 

        1.在开启WorkSteal算法的多线程调度模式下不要使用<线程局部变量(TLS)>。因为协程的每次切换，下一次继续执行都可能处于其他线程中.

        2.不要让一个代码段耗时过长。协程的调度是协作式调度，需要协程主动让出执行权，推荐在耗时很长的循环中插入一些yield

	3.除网络IO、sleep以外的阻塞系统调用，会真正阻塞调度线程的运行，请使用co_await, 并启动几个线程去Run内置的线程池.

##### Linux系统上Hook的系统调用列表：
* 

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

*  
		close     
		fcntl     
		ioctl     
		getsockopt
		setsockopt
		dup       
		dup2      
		dup3      

    以上系统调用不会造成阻塞, 虽然也被Hook, 但并不会完全改变其行为, 仅用于跟踪socket的选项和状态. 

##### Windows系统上Hook的系统调用列表：
* 

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
