#include <iostream>
#include <urcu-qsbr.h>          /* QSBR RCU flavor */
#include <urcu/rculist.h>       /* List example */
#include <urcu/compiler.h>      /* For CAA_ARRAY_SIZE */

namespace urcu {

template <typename T>
class urcu_node {
public:
    struct rcu_head m_rcu_head;
    T m_value;

    urcu_node(T arg) : m_value(arg) {}

    T *get() { return &m_value; }

    void set(const T &v) { m_value = v; }

    static void free (struct rcu_head *rh) { delete (urcu_node *) rh; }
};

template <typename T>
class urcu_data {
public:
    urcu_data(T arg) {
        auto node = new urcu_node<T>(arg);
        rcu_assign_pointer(m_rcu_node, node);
    }

    ~urcu_data() { delete(m_rcu_node); }

    urcu_node<T>* make_and_exchange() {
        auto new_node = new urcu_node<T>(*m_rcu_node->get());
        auto old_rcu_node = m_rcu_node;
        rcu_assign_pointer(m_rcu_node, new_node);
        call_rcu(&old_rcu_node->m_rcu_head, urcu_node<T>::free);
        return old_rcu_node;
    }

    urcu_node<T>* get() { return m_rcu_node; }

private:
    urcu_node<T>* m_rcu_node;
};

class urcu_ctl {
public:
    static void register_rcu() { rcu_register_thread(); }

    static void declare_quiscent_state() { rcu_quiescent_state(); }

    static void unregister_rcu() { rcu_unregister_thread(); }
};

}

