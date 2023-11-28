#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <future>
#include <thread>

// 线程池支持的模式
enum class PoolMode { MODE_FIXED, MODE_CACHED, };

// 线程类型
class Thread {
public:
	// 线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func);
	~Thread();

	// 启动线程
	void start();

	// 获取threadId_
	int getId()const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;
};

// 线程池类型
class ThreadPool {
public:
	ThreadPool();
	~ThreadPool();

	// 设置线程池工作模式
	void setMode(PoolMode mode);

	// 设置task队列上限
	void setTaskQueMaxThreshHold(int threshhold);

	// cached下线程上限设置
	void setThreadSizeThreashHold(int threshhold);

	// 给线程池提交任务
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&& ... args) -> std::future<decltype(func(args...))>
	{
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));	
		std::future<RType> result = task->get_future();
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		// 用户提交任务，最长不能阻塞超过1s,否则判断任务提交失败，并返回
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool { return taskQue_.size() < taskQueMaxThreshHold_; }))
		{
			std::cerr << "task queue is full, task submit fail." << '\n';

			auto task = std::make_shared<std::packaged_task<RType()>>([]()->RType {return RType(); });	// 返回所希望的类型的零值
			(*task)();
			return task->get_future();		
		}

		// 有空余，把任务放入队列中
		taskQue_.emplace([task]() {
			(*task)();
			});
		taskSize_++;

		notEmpty_.notify_all();

		// 根据任务数量和空闲线程数量，判断是否需要创建新的线程
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThreshHold_) {

			std::cout << ">>> create new thread..." << std::endl;

			// 创建新线程
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadid = ptr->getId();
			threads_.emplace(threadid, std::move(ptr));
			// 启动线程，并修改相关变量
			threads_[threadid]->start();
			curThreadSize_++;
			idleThreadSize_++;
		}
		return result;
	}

	// 开启线程池
	void start(int initThreadSize = std::thread::hardware_concurrency());

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 定义线程函数
	void threadFunc(int threadid);

	// 获取pool运行状态
	bool checkRunningState() const;


private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;  // 线程列表
	int initThreadSize_;	// 初始线程数量
	std::atomic_int curThreadSize_;// 当前线程池内线程总数
	int threadSizeThreshHold_;	// cached下线程数量上限
	std::atomic_uint idleThreadSize_;	// 空闲线程数

	using Task = std::function<void()>;	

	std::queue<Task> taskQue_; // 任务队列
	std::atomic_uint taskSize_;	// 任务数量
	int taskQueMaxThreshHold_;	// 任务队列数量上限

	std::mutex taskQueMtx_;	// 保证任务队列的线程安全
	std::condition_variable notFull_;	// 表示任务队列不满
	std::condition_variable notEmpty_;	// 表示任务队列不空
	std::condition_variable exitcond_;	// 表示回收信号

	PoolMode poolMode_;	// 当前工作模式
	std::atomic_bool isPoolRunning_;	// 是否启动了线程池
};

#endif