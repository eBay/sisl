/*
 * ordered_list.hpp
 *
 *  Created on: 24-Mar-2017
 *      Author: hkadayam
 */

#ifndef SRC_LIBUTILS_FDS_ORDERED_LIST_HPP_
#define SRC_LIBUTILS_FDS_ORDERED_LIST_HPP_

#include <pthread.h>
#include <assert.h>
#include <memory>

namespace fds {

template <typename T> class OrderedNode {
  public:
    OrderedNode() {
        m_next = nullptr;
        m_prev = nullptr;
    }

    virtual ~OrderedNode() {}

    std::shared_ptr<T> m_next;
    std::shared_ptr<T> m_prev;
};

template <typename T> class OrderedListIterator;

template <typename T> class OrderedListForwardIterator;

template <typename T> class OrderedListReverseIterator;

template <typename T> class OrderedList {
    friend class OrderedListIterator<T>;
    friend class OrderedListForwardIterator<T>;
    friend class OrderedListReverseIterator<T>;

  public:
    OrderedList() {}

    virtual ~OrderedList() {}

    void insert_from_front(std::shared_ptr<T> data) {
        OrderedNode<T>* data_node = data->get_node_hook();
        data_node->m_next = nullptr;
        data_node->m_prev = nullptr;

        {
            std::lock_guard<std::mutex> lock(m_lock);

            std::shared_ptr<T> cur = m_head;
            while (cur) {
                if (cur->compare(data) < 0) { break; }
                cur = cur->get_node_hook()->m_next;
            }

            /* Add before cur node */
            if (cur == nullptr) {
                if (m_tail == nullptr) {
                    assert(m_head == nullptr);
                    m_head = m_tail = data;
                } else {
                    m_tail->get_node_hook()->m_next = data;
                    data_node->m_prev = m_tail;
                    m_tail = data;
                }
            } else if (cur == m_head) {
                data_node->m_next = m_head;
                cur->get_node_hook()->m_prev = data;
                m_head = data;
            } else {
                OrderedNode<T>* cur_node = cur->get_node_hook();
                cur_node->m_prev->get_node_hook()->m_next = data;
                data_node->m_prev = cur_node->m_prev;

                cur_node->m_prev = data;
                data_node->m_next = cur;
            }
        }
    }

    void insert_from_back(std::shared_ptr<T> data) {
        OrderedNode<T>* data_node = data->get_node_hook();
        data_node->m_next = nullptr;
        data_node->m_prev = nullptr;

        {
            std::lock_guard<std::mutex> lock(m_lock);

            std::shared_ptr<T> cur = m_tail;
            while (cur) {
                if ((cur->compare(data) >= 0)) { break; }
                cur = cur->get_node_hook()->m_prev;
            }

            /* Add after cur node */
            if (cur == nullptr) {
                if (m_tail == nullptr) {
                    assert(m_head == nullptr);
                    m_head = m_tail = data;
                } else {
                    m_tail->get_node_hook()->m_next = data;
                    data_node->m_prev = m_tail;
                    m_tail = data;
                }
            } else if (cur == m_tail) {
                data_node->m_prev = m_tail;
                cur->get_node_hook()->m_next = data;
                m_tail = data;
            } else {
                OrderedNode<T>* cur_node = cur->get_node_hook();
                cur_node->m_next->get_node_hook()->m_prev = data;
                data_node->m_next = cur_node->m_next;

                cur_node->m_next = data;
                data_node->m_prev = cur;
            }
        }
    }

    void remove(std::shared_ptr<T> data) {
        {
            std::lock_guard<std::mutex> lock(m_lock);
            do_remove(data);
        }
    }

  private:
    void do_remove(std::shared_ptr<T> data) {
        OrderedNode<T>* data_node = data->get_node_hook();

        if (data_node->m_prev != nullptr) {
            data_node->m_prev->get_node_hook()->m_next = data_node->m_next;
        } else {
            assert(m_head == data);
            m_head = data_node->m_next;
        }

        if (data_node->m_next != nullptr) {
            data_node->m_next->get_node_hook()->m_prev = data_node->m_prev;
        } else {
            m_tail = data_node->m_prev;
        }
    }

  private:
    std::mutex m_lock;
    std::shared_ptr<T> m_head;
    std::shared_ptr<T> m_tail;
};

template <typename T> class OrderedListIterator {
  public:
    OrderedListIterator() {
        m_list = nullptr;
        m_protected_mode = false;
        m_cur = nullptr;
    }

    OrderedListIterator(OrderedList<T>* l, bool protected_mode) { populate(l, protected_mode); }

    virtual ~OrderedListIterator() {
        if (m_protected_mode) { m_list->m_lock.unlock(); }
    }

    virtual void populate(OrderedList<T>* l, bool protected_mode) {
        m_list = l;
        m_protected_mode = protected_mode;
        m_cur = nullptr;

        if (protected_mode) { l->m_lock.lock(); }
    }

    std::shared_ptr<T> next() {
        m_cur = get_next_item();
        return m_cur;
    }

    bool remove() {
        // Remove can only be done in protective mode iterator.
        assert(m_protected_mode);
        if (m_cur == nullptr) { return false; }

        std::shared_ptr<T> n = m_cur;
        m_cur = get_next_item();
        m_list->do_remove(n);
        return true;
    }

  protected:
    virtual std::shared_ptr<T> get_next_item() = 0;
    virtual std::shared_ptr<T> get_prev_item() = 0;

  protected:
    std::shared_ptr<T> m_cur;
    bool m_protected_mode;
    OrderedList<T>* m_list;
};

template <typename T> class OrderedListForwardIterator : public OrderedListIterator<T> {
  public:
    OrderedListForwardIterator() : OrderedListIterator<T>() {}
    OrderedListForwardIterator(OrderedList<T>* l, bool protected_mode) : OrderedListIterator<T>(l, protected_mode) {}

    void populate(OrderedList<T>* l, bool protected_mode) override {
        OrderedListIterator<T>::populate(l, protected_mode);
    }

  protected:
    std::shared_ptr<T> get_next_item() override {
        if (this->m_cur == nullptr) {
            return this->m_list->m_head;
        } else {
            return this->m_cur->get_node_hook()->m_next;
        }
    }

    std::shared_ptr<T> get_prev_item() override {
        if (this->m_cur == nullptr) {
            return this->m_list->m_tail;
        } else {
            return this->m_cur->get_node_hook()->m_prev;
        }
    }
};

template <typename T> class OrderedListReverseIterator : public OrderedListIterator<T> {
  public:
    OrderedListReverseIterator() : OrderedListIterator<T>() {}
    OrderedListReverseIterator(OrderedList<T>* l, bool protected_mode) : OrderedListIterator<T>(l, protected_mode) {}

    virtual void populate(OrderedList<T>* l, bool protected_mode) {
        OrderedListIterator<T>::populate(l, protected_mode);
    }

  protected:
    std::shared_ptr<T> get_next_item() {
        if (this->m_cur == nullptr) {
            return this->m_list->m_tail;
        } else {
            return this->m_cur->get_node_hook()->m_prev;
        }
    }

    std::shared_ptr<T> get_prev_item() {
        if (this->m_cur == nullptr) {
            return this->m_list->m_head;
        } else {
            return this->m_cur->get_node_hook()->m_next;
        }
    }
};

} // namespace fds

#endif /* SRC_LIBUTILS_FDS_ORDERED_LIST_HPP_ */
