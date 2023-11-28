#include "threadpool.h"

const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 20;
const int THREAD_MAX_IDLE_TIME = 60; // s

//---------Thread类型---------

Thread::Thread(ThreadFunc func)
	:func_(func)
	, threadId_(generateId_++)
{}

Thread::~Thread() = default;

// 启动线程
void Thread::start() {
	// 创建线程并执行线程函数
	std::thread t(func_, threadId_);	
	t.detach();			
}

// 获取threadId_
int Thread::getId()const {
	return threadId_;
}
int Thread::generateId_ = 0;

//---------Thread类型---------

//---------ThreadPool类型---------

ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, taskSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}

ThreadPool::~ThreadPool(){
	isPoolRunning_ = false;
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();	
	exitcond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

// 设置线程池工作模式
void ThreadPool::setMode(PoolMode mode) {
	if (checkRunningState()) return;
	poolMode_ = mode;
}

// 设置task队列上限
void ThreadPool::setTaskQueMaxThreshHold(int threshhold) {
	if (checkRunningState()) return;
	taskQueMaxThreshHold_ = threshhold;
}

// cached模式线程上限设置
void ThreadPool::setThreadSizeThreashHold(int threshhold) {
	if (checkRunningState()) return;
	if (poolMode_ == PoolMode::MODE_CACHED) {
		threadSizeThreshHold_ = threshhold;
	}
}

// 开启线程池
void ThreadPool::start(int initThreadSize) {
	isPoolRunning_ = true;

	// 记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// 创建线程对象
	for (int i = 0; i < initThreadSize_; i++) {
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadid = ptr->getId();
		threads_.emplace(threadid, std::move(ptr));
	}

	// 启动所有线程
	for (int i = 0; i < initThreadSize_; i++) {
		threads_[i]->start();
		idleThreadSize_++;
	}
}

// 定义线程函数
void ThreadPool::threadFunc(int threadid){
	auto lastTime = std::chrono::high_resolution_clock().now();

	for (;;) {
		Task task;
		{
			std::unique_lock<std::mutex> lock(taskQueMtx_);	
			std::cout << "tid:" << std::this_thread::get_id()
				<< "尝试获取任务..." << std::endl;

			// cached模式下，可能已经创建了很多线程，但是空闲时间超过了THREAD_MAX_IDLE_TIME，则回收多余线程
			while (taskQue_.size() == 0) {
				if (!isPoolRunning_) {
					threads_.erase(threadid);
					std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
						<< std::endl;
					exitcond_.notify_all();
					return;
				}
				if (poolMode_ == PoolMode::MODE_CACHED) {
					// 超时返回
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_) {
							// 回收当前线程
							// 修改记录了线程数量的相关变量的值
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
								<< std::endl;
							return;
						}
					}
				}
				else {
					// 等待notEmpty条件
					notEmpty_.wait(lock);	
				}
			}
			
			if(!isPoolRunning_){
				break;
			}

			idleThreadSize_--;

			std::cout << "tid:" << std::this_thread::get_id()
				<< "获取任务成功..." << std::endl;

			// 从任务队列取任务
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 取出任务，进行通知
			notFull_.notify_all();
			// 依然有任务，继续通知
			if (taskQue_.size() > 0) notEmpty_.notify_all();
		}

		if (task != nullptr) {
			task();	
		}
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();  // 更新线程执行完任务的时间
	}
}

// 获取pool运行状态
bool ThreadPool::checkRunningState() const {
	return isPoolRunning_;
}

//---------ThreadPool类型---------


