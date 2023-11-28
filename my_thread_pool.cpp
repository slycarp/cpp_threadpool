// my_thread_pool.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "threadpool.h"

using namespace std;


auto func(int a, int b, int c) {
    return (a + b) * c;
}

int main()
{
    ThreadPool pool;
    pool.start();

    future<int> result1 = pool.submitTask(func, 10, 20, 2);
    future<int> result2 = pool.submitTask([](int start, int end)->int {
        int sum = 0;
        for (int i = start; i < end; i++) {
            sum += i;
        }   
        return sum;
        }, 0, 10000);
    cout << result1.get() << endl;
    cout << result2.get() << endl;

    getchar();

    return 0;

}

