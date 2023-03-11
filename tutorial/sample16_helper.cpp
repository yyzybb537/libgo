/************************************************
 * libgo sample16 libgohelper
*************************************************/
#include "libgohelper.h"
#include "win_exit.h"
#include <stdio.h>
#include <thread>


int main()
{
    //----------------------------------
    // libgohelper 提供两个能解决很多并发问题的封装，其以单例方法来提供给应用程序使用
    //  inline void Finish(std::vector<std::function<void()>>  &list)  
    //  同步等待所有协程结束，接入方法同 go语法
  
    int a0 = 0;
    int a1 = 0;
    int a2 = 0;
    int a3 = 0;
    int a4 = 0;
    vector<std::function<void()>> list2 = {
        [&]{a0++;},
        [&]{a1++;},
        [&]{a2++;},
        [&]{a3++;},
        [&]{a4++;}
    };
    libgohelper::ScheHandler::GetInst()->Finish(list2);
    printf("a0=%d a1=%d a2=%d a3=%d a4=%d \n",a0,a1,a2,a3,a4);

    //  inline void UnsafeGo(std::function<void()>fn)
    //  单例直接调用go关键词，接入方法同 go语法
  
    int sum = 0;
    libgohelper::ScheHandler::GetInst()->UnsafeGo([&]{ 
        for(int i = 0;i<1000;i++){sum++;}
    }
    );
    printf("begin sum=%d\n",sum); 
    WaitUntilNoTaskS(*(libgohelper::ScheHandler::GetInst()->sched));
    printf("end sum=%d\n",sum); 
   
   
   
    //template <typename source,typename result,typename func>
    //inline void Mapreduce(const vector<source> &list,vector<result> &outList,func myfunc,void* handler)
    // 1.定义要查询的列表，此时参数类型为uint32_t 
    // vector<int> list;
    // 2.定义要返回的列表，参数类型为uint32
    //  vector<int> outList;
    // 3.定义回调函数
    //   [&](void *p,int &i,int &j)
    //   a.回调函数的第一个参数是 void*p 
    //   该参数是为了转换步骤4的类指针
    //   b.回调函数的第二,第三个参数,分别为步骤1中的参数类型
    //   c.书写回调函数
    //   [&](void *p,int &i,int &j){
    //       j = i+1;
    //   }
    //   此时将 void*p 显式转为步骤4的传入的指针，并调用其成员函数
    //   如果不是成员方法直接传空即可
    //   如果是成员方法 第四个参数为 对象指针
    //   [&](void *p,int &i,int &j){
    //     obj *tmp = (obj *)p;
    //     tmp->add(i,j);
    //   }


    vector<int> list = {2,2,3,4,5,6,7};
    vector<int> result2;
    libgohelper::ScheHandler::GetInst()->Mapreduce(list,result2,[&](void *p,int &i,int &j){
        j = i+1;
    },(void *)NULL);
    for(auto i:result2){
       printf("%d ", i);
    }
    printf("\n");
}

