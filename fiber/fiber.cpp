#include "fiber.h"

static bool debug = false;

// thread_local 用于表示不同线程的独立变量空间
namespace sylar{

    // 正在运行的协程
    static thread_local Fiber* t_fiber = nullptr;
    // 主协程
    static thread_local std::shared_ptr<Fiber> t_thread_fiber = nullptr;    

    // 调度协程
    static thread_local Fiber* t_scheduler_fiber = nullptr;

    // atomic 是原子操作，解决并发问题

    // 协程计数器
    static std::atomic<uint64_t> s_fiber_count{0};
    // 协程id
    static std::atomic<uint64_t> s_fiber_id{0};

    void Fiber::SetThis(Fiber *f){
	    t_fiber = f;
    }

    
    // 返回当前线程正在执行的协程
    // 如果还没有创建第一个协程，则创建第一个协程，且该协程为主协程，其余的协调都通过它进行调度
    // 其他协程结束时候，需要切回主协程，由主协程选择新的协程resume
    std::shared_ptr<Fiber> Fiber::GetThis()
    {
	    if(t_fiber)
	    {	
		    return t_fiber->shared_from_this();
	    }

	    std::shared_ptr<Fiber> main_fiber(new Fiber());  // 注意无参构造函数
	    t_thread_fiber = main_fiber;

        // get获取裸指针，可以避免+引用计数

	    t_scheduler_fiber = main_fiber.get(); // 除非主动设置 主协程默认为调度协程
	    assert(t_fiber == main_fiber.get());    
	    return t_fiber->shared_from_this();
    }

    // 可以改变/设置/调度协程
    void Fiber::SetSchedulerFiber(Fiber* f)
    {
    	t_scheduler_fiber = f;
    }

    //获取正在运行的协程id
    uint64_t Fiber::GetFiberId()
    {
	    if(t_fiber)
	    {
	    	return t_fiber->getId();
	    }
	    return (uint64_t)-1;
    }

    //无参构造函数，用于创建主协程
    Fiber::Fiber()
    {
    	SetThis(this);
    	m_state = RUNNING;
    
    	if(getcontext(&m_ctx))   //获取协程上下文
    	{
    		std::cerr << "Fiber() failed\n";
    		pthread_exit(NULL);
    	}
    
    	m_id = s_fiber_id++;
    	s_fiber_count ++;
    	if(debug) std::cout << "Fiber(): main id = " << m_id << std::endl;
    }

    Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler): m_cb(cb), m_runInScheduler(run_in_scheduler)
    {
    	m_state = READY;
	    // 分配协程栈空间
	    m_stacksize = stacksize ? stacksize : 128000;
	    m_stack = malloc(m_stacksize);

    	if(getcontext(&m_ctx))
    	{
    		std::cerr << "Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) failed\n";
    		pthread_exit(NULL);
    	}
	
    	m_ctx.uc_link = nullptr;
    	m_ctx.uc_stack.ss_sp = m_stack;
    	m_ctx.uc_stack.ss_size = m_stacksize;

    	makecontext(&m_ctx, &Fiber::MainFunc, 0);
	
    	m_id = s_fiber_id++;
    	s_fiber_count ++;
    	if(debug) std::cout << "Fiber(): child id = " << m_id << std::endl;
    }

    Fiber::~Fiber()
    {
    	s_fiber_count --;
	    if(m_stack)  //释放栈空间
	    {
	    	free(m_stack);
	    }
	    if(debug) std::cout << "~Fiber(): id = " << m_id << std::endl;	
    }

    //重置协程的入口函数
    void Fiber::reset(std::function<void()> cb)
    {
    	assert(m_stack != nullptr&&m_state == TERM);  //强制只有TERM状态的协程才可以reset

	    m_state = READY;
    	m_cb = cb;

	    if(getcontext(&m_ctx))
	    {
	    	std::cerr << "reset() failed\n";
	    	pthread_exit(NULL);
	    }

    	m_ctx.uc_link = nullptr;
	    m_ctx.uc_stack.ss_sp = m_stack;
    	m_ctx.uc_stack.ss_size = m_stacksize;
	    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    }

    // 将当前状态切换成执行状态
    void Fiber::resume()
    {
	    assert(m_state==READY);
	
    	m_state = RUNNING;

    	if(m_runInScheduler)  //调度器运行
	    {
	    	SetThis(this);
	    	if(swapcontext(&(t_scheduler_fiber->m_ctx), &m_ctx))
	    	{
	    		std::cerr << "resume() to t_scheduler_fiber failed\n";
	    		pthread_exit(NULL);
	    	}		
    	}
    	else
	    {
	    	SetThis(this);
		    if(swapcontext(&(t_thread_fiber->m_ctx), &m_ctx))
		    {
			    std::cerr << "resume() to t_thread_fiber failed\n";
			    pthread_exit(NULL);
		    }	
	    }
    }


    //当前协程让出cpu
    void Fiber::yield()
    {
    	assert(m_state==RUNNING || m_state==TERM);

    	if(m_state!=TERM)
    	{
	    	m_state = READY;
	    }

	    if(m_runInScheduler)
	    {
		    SetThis(t_scheduler_fiber);
		    if(swapcontext(&m_ctx, &(t_scheduler_fiber->m_ctx)))
		    {
		    	std::cerr << "yield() to to t_scheduler_fiber failed\n";
		    	pthread_exit(NULL);
		    }		
	    }
	    else
	    {
	    	SetThis(t_thread_fiber.get());
	    	if(swapcontext(&m_ctx, &(t_thread_fiber->m_ctx)))
	    	{
		    	std::cerr << "yield() to t_thread_fiber failed\n";
			    pthread_exit(NULL);
		    }	
	    }	
    }

    // 协程入口函数
    //这里没有考虑运行函数出现异常的情况
    void Fiber::MainFunc()
    {
    	std::shared_ptr<Fiber> curr = GetThis();
    	assert(curr!=nullptr);  

    	curr->m_cb(); 
    	curr->m_cb = nullptr;
    	curr->m_state = TERM;

	    // 运行完毕 -> 让出执行权
	    auto raw_ptr = curr.get();
	    curr.reset(); 
	    raw_ptr->yield(); 
    }


}