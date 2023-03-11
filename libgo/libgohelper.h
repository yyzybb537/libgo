#ifndef LIBGO_LIBGOHELPER__INCLUDED
#define LIBGO_LIBGOHELPER__INCLUDED
#pragma once
#include "coroutine.h"
namespace libgohelper {
    class RunB {
        public:
            RunB( std::function<void()> func):func(func){};
            RunB(){};
            std::function<void()> func;
    };

    class ScheHandler {
        public:
        ScheHandler(uint32_t maxThreadCount_ = 0):sched(co::Scheduler::Create()){
            std::thread t2([&]{ sched->Start(0,maxThreadCount_); });
            t2.detach();
        };

        ~ScheHandler(){
            sched->Stop();
        };
        co::Scheduler *sched;
        static ScheHandler*GetInst(){
            static std::unique_ptr<ScheHandler> instance;
            static std::once_flag once;
            call_once(once, [&]() {
                instance.reset(new ScheHandler());
            });
            return instance.get();
        };


        inline void Finish(std::vector<std::function<void()>>  &list) { 
            std::atomic_int c {0};
            c+=list.size();
            co_chan<RunB> ch_0(list.size());

            for(auto item : list) {
                ch_0 <<  RunB(item);
            }

            for(int i = 0;i<list.size();i++){
                 go co_scheduler(sched)[&]{
                    RunB s1;
                    ch_0>>s1;
                    s1.func();
                    c--;
                };
            }
            while (c) {
                usleep(1000);
            }
        };

        inline void UnsafeGo(std::function<void()>fn){
             go co_scheduler(sched)[&]{
                fn();
             };
        };
    
        template <typename source,typename result,typename func>
        inline void Mapreduce(const std::vector<source> &list,std::vector<result> &outList,func myfunc,void* handler)
        {
            using namespace std::placeholders;
            std::atomic_int c {0};
            c+=list.size();

            co_chan<source> ch_0(list.size());

            co_mutex cm;
            std::map<source,result> filter;   
            
            for(auto item : list) {
                ch_0 <<  item;
            }
            for(int i = 0;i<list.size();i++){
                go co_scheduler(sched) [&]{
                    source s1;
                    ch_0>>s1;
                    result r1;
                    myfunc(handler,s1,r1);
                    {
                        std::lock_guard<co_mutex> lock(cm);
                        filter[s1] = r1;
                    }
                    c--;
                };
            }
            while (c) {
                usleep(1000);
            }
            for(auto item : list) {
                auto it = filter.find(item);
                if(it!=filter.end()){
                    outList.push_back((it->second));
                }
            }
        }
    };
}
#endif
