#include <memory>
#include <iostream>
#include "ordered_list.hpp"

using namespace fds;
using namespace std;

class Txn : public std::enable_shared_from_this<Txn>, public fds::OrderedNode<Txn> {
  public:
    Txn(int id) { m_id = id; }

    virtual std::shared_ptr<Txn> get_ref() { return shared_from_this(); }

    inline virtual OrderedNode<Txn>* get_node_hook() { return (OrderedNode<Txn>*)&m_hook; }

    virtual int compare(std::shared_ptr<Txn>& o) {
        if (m_id < o->m_id) {
            return 1;
        } else if (m_id > o->m_id) {
            return -1;
        } else {
            return 0;
        }
    }

    void print_key() {
    std:
        cout << "key = " << m_id << std::endl;
    }

  private:
    int m_id;
    OrderedNode<Txn> m_hook;
};

int main(int argc, char* argv[]) {
    OrderedList<Txn> l;
    for (auto i = 0; i < 10; i++) {
        auto r = rand() % 100;
        std::cout << "Inserting " << r << std::endl;
        auto t = std::make_shared<Txn>(r);
        l.insert_from_front(t);
    }

    {
        OrderedListForwardIterator<Txn> fiter(&l, false);
        std::shared_ptr<Txn> t;
        while ((t = fiter.next()) != nullptr) {
            t->print_key();
        }
    }

    std::cout << "Reverse Iteration: " << std::endl;
    {
        OrderedListReverseIterator<Txn> riter(&l, true);
        std::shared_ptr<Txn> t;
        while ((t = riter.next()) != nullptr) {
            t->print_key();
        }
    }
}
