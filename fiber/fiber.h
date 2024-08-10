#ifndef _COROUTINE_H_
#define _COROUTINE_H_



#include <iostream>     
#include <memory>       
#include <atomic>       
#include <functional>   
#include <cassert>      
#include <ucontext.h>   
#include <unistd.h>
#include <mutex>
namespace sylar {
//协程类

//继承至enable_shared_from_this,实现协程共享

class Fiber : public std::enable_shared_from_this<Fiber>{

public:

    typedef std::shared_ptr<Fiber> ptr; //指向自己的shared_ptr

    //协程的运行状态
    enum State {
        //就绪 运行 结束
        READY,
        RUNNING,
        TERM
    };

private :
    //用于创建线程的主协程
    Fiber();   //协程的无参构造

public :
   //构造函数，用于创建用户协程
   //cb 协程入口函数 tasksize 栈大小  run_in_scheduler 是否在调度器中运行
   Fiber(std::function<void()> cb,size_t stacksize = 0, bool run_in_scheduler = true);

   ~Fiber(); //析构函数，用于释放协程栈空间

   //重置协程和入口函数，复用栈空间
   void reset(std::function<void()> cb);

   //将当前协程切换到执行状态
   //当前协程running，前一个协程ready
   void resume();

   //当前协程让出cpu
   void yield();

   //获取协程ID
   uint64_t getId() const{
    return m_id;
   }

   //获取协程状态
   State getStatus() const {
    return m_state;
   }

public :

   //设置当前正在运行的协程
   static void SetThis(Fiber *f);

   //设置main协程

   static void SetSchedulerFiber(Fiber *f);

   //返回当前线程正在执行的协程
   static Fiber::ptr GetThis();

   //获取协程总数
   static uint64_t TotalFibers();

   //协程入口函数
   static void MainFunc();

   //获取当前协程Id
   static uint64_t GetFiberId();

private :
   uint64_t m_id; //协程ID

   uint64_t m_stacksize; //协程栈大小

   State m_state; //协程状态

   ucontext_t m_ctx; //协程上下文

   void *m_stack = nullptr; // 协程栈地址

   std::function<void()> m_cb; //协程入口函数

   bool m_runInScheduler; //是否在调度器中运行

public:
	std::mutex m_mutex; //还有一个锁
};
}

#endif