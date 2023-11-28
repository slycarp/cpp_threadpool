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

// �̳߳�֧�ֵ�ģʽ
enum class PoolMode { MODE_FIXED, MODE_CACHED, };

// �߳�����
class Thread {
public:
	// �̺߳�����������
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func);
	~Thread();

	// �����߳�
	void start();

	// ��ȡthreadId_
	int getId()const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;
};

// �̳߳�����
class ThreadPool {
public:
	ThreadPool();
	~ThreadPool();

	// �����̳߳ع���ģʽ
	void setMode(PoolMode mode);

	// ����task��������
	void setTaskQueMaxThreshHold(int threshhold);

	// cached���߳���������
	void setThreadSizeThreashHold(int threshhold);

	// ���̳߳��ύ����
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&& ... args) -> std::future<decltype(func(args...))>
	{
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));	
		std::future<RType> result = task->get_future();
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		// �û��ύ�����������������1s,�����ж������ύʧ�ܣ�������
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool { return taskQue_.size() < taskQueMaxThreshHold_; }))
		{
			std::cerr << "task queue is full, task submit fail." << '\n';

			auto task = std::make_shared<std::packaged_task<RType()>>([]()->RType {return RType(); });	// ������ϣ�������͵���ֵ
			(*task)();
			return task->get_future();		
		}

		// �п��࣬��������������
		taskQue_.emplace([task]() {
			(*task)();
			});
		taskSize_++;

		notEmpty_.notify_all();

		// �������������Ϳ����߳��������ж��Ƿ���Ҫ�����µ��߳�
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThreshHold_) {

			std::cout << ">>> create new thread..." << std::endl;

			// �������߳�
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadid = ptr->getId();
			threads_.emplace(threadid, std::move(ptr));
			// �����̣߳����޸���ر���
			threads_[threadid]->start();
			curThreadSize_++;
			idleThreadSize_++;
		}
		return result;
	}

	// �����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency());

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// �����̺߳���
	void threadFunc(int threadid);

	// ��ȡpool����״̬
	bool checkRunningState() const;


private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;  // �߳��б�
	int initThreadSize_;	// ��ʼ�߳�����
	std::atomic_int curThreadSize_;// ��ǰ�̳߳����߳�����
	int threadSizeThreshHold_;	// cached���߳���������
	std::atomic_uint idleThreadSize_;	// �����߳���

	using Task = std::function<void()>;	

	std::queue<Task> taskQue_; // �������
	std::atomic_uint taskSize_;	// ��������
	int taskQueMaxThreshHold_;	// ���������������

	std::mutex taskQueMtx_;	// ��֤������е��̰߳�ȫ
	std::condition_variable notFull_;	// ��ʾ������в���
	std::condition_variable notEmpty_;	// ��ʾ������в���
	std::condition_variable exitcond_;	// ��ʾ�����ź�

	PoolMode poolMode_;	// ��ǰ����ģʽ
	std::atomic_bool isPoolRunning_;	// �Ƿ��������̳߳�
};

#endif