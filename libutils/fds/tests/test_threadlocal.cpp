//
// Created by Kadayam, Hari on 31/05/17.
//

#include <thread>
#include "libutils/fds/thread/thread_buffer.hpp"
#include <iostream>

using namespace std;

class Manager {
  public:
    int x;

    Manager(int a) {
        x = a + rand() % 1000;
        std::cout << "Manager constructor x = " << x << endl;
    }

    int get_x() { return x; }
};

class Server {
  public:
    Server() : mgr(5) {}

    void process() {
        Manager* m = mgr.get();
        std::cout << "m->get_x() = " << m->get_x() << endl;
        std::cout << "mgr->get_x() = " << mgr->get_x() << endl;
    }

  private:
    fds::ThreadBuffer<Manager> mgr;
};

void worker(int thread_num) {
    fds::ThreadLocal::attach(thread_num);
    Server s;
    s.process();
    fds::ThreadLocal::detach();
}

int main(int argc, char* argv[]) {
    std::thread* thrs[16];

    int nthrs = 10;
    for (auto i = 0; i < nthrs; i++) {
        thrs[i] = new std::thread(worker, i);
    }

    for (auto i = 0; i < nthrs; i++) {
        thrs[i]->join();
    }
}