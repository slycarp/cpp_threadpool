#include "threadpool.h"

const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 20;
const int THREAD_MAX_IDLE_TIME = 60; // s

//---------Thread����---------

Thread::Thread(ThreadFunc func)
	:func_(func)
	, threadId_(generateId_++)
{}

Thread::~Thread() = default;

// �����߳�
void Thread::start() {
	// �����̲߳�ִ���̺߳���
	std::thread t(func_, threadId_);	
	t.detach();			
}

// ��ȡthreadId_
int Thread::getId()const {
	return threadId_;
}
int Thread::generateId_ = 0;

//---------Thread����---------

//---------ThreadPool����---------

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

// �����̳߳ع���ģʽ
void ThreadPool::setMode(PoolMode mode) {
	if (checkRunningState()) return;
	poolMode_ = mode;
}

// ����task��������
void ThreadPool::setTaskQueMaxThreshHold(int threshhold) {
	if (checkRunningState()) return;
	taskQueMaxThreshHold_ = threshhold;
}

// cachedģʽ�߳���������
void ThreadPool::setThreadSizeThreashHold(int threshhold) {
	if (checkRunningState()) return;
	if (poolMode_ == PoolMode::MODE_CACHED) {
		threadSizeThreshHold_ = threshhold;
	}
}

// �����̳߳�
void ThreadPool::start(int initThreadSize) {
	isPoolRunning_ = true;

	// ��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// �����̶߳���
	for (int i = 0; i < initThreadSize_; i++) {
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadid = ptr->getId();
		threads_.emplace(threadid, std::move(ptr));
	}

	// ���������߳�
	for (int i = 0; i < initThreadSize_; i++) {
		threads_[i]->start();
		idleThreadSize_++;
	}
}

// �����̺߳���
void ThreadPool::threadFunc(int threadid){
	auto lastTime = std::chrono::high_resolution_clock().now();

	for (;;) {
		Task task;
		{
			std::unique_lock<std::mutex> lock(taskQueMtx_);	
			std::cout << "tid:" << std::this_thread::get_id()
				<< "���Ի�ȡ����..." << std::endl;

			// cachedģʽ�£������Ѿ������˺ܶ��̣߳����ǿ���ʱ�䳬����THREAD_MAX_IDLE_TIME������ն����߳�
			while (taskQue_.size() == 0) {
				if (!isPoolRunning_) {
					threads_.erase(threadid);
					std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
						<< std::endl;
					exitcond_.notify_all();
					return;
				}
				if (poolMode_ == PoolMode::MODE_CACHED) {
					// ��ʱ����
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_) {
							// ���յ�ǰ�߳�
							// �޸ļ�¼���߳���������ر�����ֵ
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
					// �ȴ�notEmpty����
					notEmpty_.wait(lock);	
				}
			}
			
			if(!isPoolRunning_){
				break;
			}

			idleThreadSize_--;

			std::cout << "tid:" << std::this_thread::get_id()
				<< "��ȡ����ɹ�..." << std::endl;

			// ���������ȡ����
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// ȡ�����񣬽���֪ͨ
			notFull_.notify_all();
			// ��Ȼ�����񣬼���֪ͨ
			if (taskQue_.size() > 0) notEmpty_.notify_all();
		}

		if (task != nullptr) {
			task();	
		}
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();  // �����߳�ִ���������ʱ��
	}
}

// ��ȡpool����״̬
bool ThreadPool::checkRunningState() const {
	return isPoolRunning_;
}

//---------ThreadPool����---------


