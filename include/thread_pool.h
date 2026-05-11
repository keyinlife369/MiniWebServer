#pragma once
//线程池实现
#include<vector>
#include<queue>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<future>
#include<memory>

//线程池类
class ThreadPool {
public:
	explicit ThreadPool(size_t threads = std::thread::hardware_concurrency());
	~ThreadPool();

	template<class F,class... Args>
	auto enqueue(F&& f, Args&&... args) -> std::future<typename std::_Invoke_result_t<F, Args...>>;

private:
	std::vector<std::thread> workers_;//工作线程集合
	std::queue<std::function<void()>> tasks_;//任务队列

	std::mutex queue_mutex_;//任务队列互斥锁
	std::condition_variable condition_;//条件变量
	bool stop_ = false;//线程池停止标志
};

inline ThreadPool::ThreadPool(size_t threads) {
	//循环指定数量的工作线程
	for (size_t i = 0; i < threads; i++) {
		//创建线程并加入数组
		//lambda捕获线程对象
		workers_.emplace_back([this] {
			//死循环线程启动后一直运行到线程池结束
			while (true) {
				std::function<void()> task; {

					//加锁独占访问任务队列，放置多线程同时修改队列
					std::unique_lock<std::mutex> lock(queue_mutex_);
					condition_.wait(lock, [this] {
						return stop_ || !tasks_.empty();
						});

					//线程阻塞休眠
					if (stop_ && tasks_.empty()) return;

					task = std::move(tasks_.front());
					tasks_.pop();
				}
				task();
			}
		});
	}
}

inline ThreadPool::~ThreadPool() {
	{
		//自动加锁，自动解锁
		std::unique_lock<std::mutex> lock(queue_mutex_);
		//线程关闭标志设为ture
		stop_ = true;
	}
	//唤醒正在等待工作的线程
	condition_.notify_all();

	for (std::thread& worker : workers_) {
		worker.join();
	}
}


template<class F, class... Args>//模板
//
auto ThreadPool::enqueue(F&& f, Args&&... args)
-> std::future<typename std::invoke_result_t<F, Args...>> {

	using return_type = typename std::invoke_result_t<F, Args...>;

	auto task = std::make_shared<std::packaged_task<return_type()>>(
		std::bind(std::forward<F>(f), std::forward<Args>(args)...)
	);
	//获取任务中的一个future对象
	std::future<return_type> res = task->get_future();
	{
		//加锁安全访问任务队列
		std::unique_lock<std::mutex> lock(queue_mutex_);
		if (stop_) throw std::runtime_error("enqueue on stopped ThreadPool");
		//线程停止，抛出异常禁止添加异常
		tasks_.emplace([task]() { (*task)(); });
	}

	//唤醒空闲线程
	condition_.notify_one();
	
	return res;
}