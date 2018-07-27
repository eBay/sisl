/*
 * test_list.cpp
 *
 *  Created on: 10-Feb-2017
 *      Author: hkadayam
 */

#include "cds/init.h"
#include <iostream>
#include <sstream>
#include <assert.h>
#include <thread>
#include "../list/listmap.hpp"

using namespace std;

struct task_id : public fds::ListKey {
    int m_id;

    task_id() { m_id = 0; }
    task_id(int id) { m_id = id; }
    int compare(const fds::ListKey& other) const {
        const task_id& ot = static_cast<const task_id&>(other);
        return (this->m_id - ot.m_id);
    }

    task_id& operator=(task_id& other) {
        const task_id& ot = static_cast<const task_id&>(other);
        this->m_id = ot.m_id;
        return *this;
    }

    fds::ListKey& operator=(fds::ListKey& other) {
        return static_cast<fds::ListKey&>(operator=(static_cast<task_id&>(other)));
    }
};

struct task : public fds::ListValue {
  public:
    task_id id;
    std::string task_name;
    int task_type;

    fds::ListValue& operator=(fds::ListValue& other) {
        task& other_task = (task&)other;
        id = other_task.id;
        task_name = other_task.task_name;
        task_type = other_task.task_type;

        return *this;
    }

    void set_key(fds::ListKey& k) { id = k; }

    virtual const fds::ListKey* extract_key() const { return (&id); }
};

void insert_thread(fds::SkipListMap* map, int start, int count) {
    cds::threading::Manager::attachThread();

    task* tsk = new task[count];

    for (auto i = 0; i < count; i++) {
        std::stringstream ss;
        ss << "Task " << start + i;
        tsk[i].task_name = ss.str();
        tsk[i].task_type = 1;

        task dummy;
        task_id key(start + i);
        map->insert(key, tsk[i], &dummy);
    }

    cds::threading::Manager::detachThread();
}

int main(int argc, char** argv) {
    std::thread* thrs[8];

    // Initialize libcds
    cds::Initialize();

    // Initialize Hazard Pointer singleton
    cds::gc::DHP hpG;

    // If main thread uses lock-free containers
    // the main thread should be attached to libcds infrastructure
    cds::threading::Manager::attachThread();
    fds::SkipListMap map;

    int count = 100;
    int nthrs = 8;
    for (auto i = 0; i < nthrs; i++) {
        thrs[i] = new std::thread(insert_thread, &map, i * count, count);
    }

    for (auto i = 0; i < nthrs; i++) {
        thrs[i]->join();
    }

    for (auto i = 0; i < nthrs * count; i++) {
        int r = rand() % 200;
        task_id key(r);
        task t;
        map.get(key, &t);
        cout << "ID = " << t.id.m_id << " TaskName = " << t.task_name << endl;
    }

    cout << endl;
    for (auto i = 0; i < nthrs * count; i++) {
        task t;
        task_id key(i);
        map.remove(key, &t);

        cout << "Removed ID = " << t.id.m_id << " TaskName = " << t.task_name << endl;
    }

    // Terminate libcds
    cds::Terminate();
}
