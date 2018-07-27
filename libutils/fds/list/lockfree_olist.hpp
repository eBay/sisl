/*
 * lockfree_olist.hpp
 *
 *  Created on: 26-Jan-2017
 *      Author: hkadayam
 *
 *
 *  Description:
 *   Implementation of a Lock-free ordered list based of Harris' algorithm
 *   "A Pragmatic Implementation of Non-Blocking Linked Lists"
 *    T. Harris, p. 300-314, DISC 2001.
 */

#ifndef LOCKFREE_OLIST_HPP_
#define LOCKFREE_OLIST_HPP_

#if 0
#include "smart_ptr.hpp"
using namespace std;

namespace fds
{
class list_value
{
	friend class lockfree_olist;

private:
	fds::smart_ptr<list_value> m_next;

public:
	list_value()
	{
	}

	virtual ~list_value();
	virtual int compare(const fds::smart_ptr<list_value> other) = 0;
};

class lockfree_olist
{
private:
	fds::smart_ptr<list_value> m_head;
	fds::smart_ptr<list_value> m_tail;
	atomic<uint32_t> m_size;

public:
	lockfree_olist()
	{
		m_head = fds::smart_ptr<list_value>::construct();
		m_tail = fds::smart_ptr<list_value>::construct();
		m_head->m_next = m_tail;
		m_size.store(0);
	}

	~lockfree_olist()
	{
		// TODO: The head and tail should get automatically deleted, because of smart_ptr.
		// But just confirm it is in fact happening.
	}

	bool insert(fds::smart_ptr<list_value> v, list_value *outval)
	{
		bool status = false;
		fds::smart_ptr<list_value> right;
		fds::smart_ptr<list_value> left;

		right = nullptr;
		left = nullptr;

		while (true) {
			search(v, &left, &right);

			// We already have the data
			if ((right != m_tail) && (right->compare(v) == 0)) {
				*outval = v;
				status = false;
				break;
			}

			v->m_next = right;
			if (left->m_next.cas(right, v) == true) {
				m_size.fetch_add(1);
				status = true;
				break;
			}
		}

		return status;
	}

	bool get(fds::smart_ptr<list_value> v, fds::smart_ptr<list_value> *outv)
	{
		fds::smart_ptr<list_value> it = m_head->m_next;

		while (it != m_tail) {
			if (!(it->m_next.is_valid())) {
				int x = it->compare(v);
				if (x == 0) {
					*outv = v;
					return true;
				} else if (x > 0) {
					return false;
				}
			}
			it = it->m_next;
		}

		return false;
	}

	bool remove(fds::smart_ptr<list_value> v)
	{
		fds::smart_ptr<list_value> left;
		fds::smart_ptr<list_value> right;
		//ds::smart_ptr<list_value> right_successor;

		search(v, &left, &right);

		if ((right == m_tail) || (right->compare(v) != 0)) {
			return false;
		}

		right.set_validity(false);
		return true;
	}

private:
#if 0
	void search(fds::smart_ptr<list_value> v, fds::smart_ptr<list_value> *pleft, fds::smart_ptr<list_value> *pright)
	{
		fds::smart_ptr<list_value> left_next;

		while (true) {
			fds::smart_ptr<list_value> cur = m_head;
			fds::smart_ptr<list_value> next = m_head->m_next;

			/* 1: Find left_node and right_node */
			while (cur.is_valid() || (cur->compare(v) < 0)) {
				if (!cur.is_valid()) {
					*pleft = cur;
					left_next = next;
				}
				cur = next;
				if (cur == m_tail) {
					break;
				}
				next = cur->m_next;
			}

			*pright = cur;

			/* 2: Check nodes are adjacent */
			if (left_next == *pright) {
				if (!((*pright).is_valid())) {
					return;
				}
			} else {
				if ((*pleft)->m_next.cas(left_next, *pright)) {
					if (!((*pright).is_valid())) {
						return;
					}
				}
			}
		}
	}
#endif

	void search(fds::smart_ptr<list_value> v, fds::smart_ptr<list_value> *pleft, fds::smart_ptr<list_value> *pright)
	{
		fds::smart_ptr<list_value> left_next;

		while (true) {
			fds::smart_ptr<list_value> cur = m_head;
			fds::smart_ptr<list_value> next = m_head->m_next;

			/* 1: Find left_node and right_node */
			do {
				if (!cur.is_valid()) {
					*pleft = cur;
					left_next = next;
				}
				cur = next;
				if (cur == m_tail) {
					break;
				}
				next = cur->m_next;
			} while (cur.is_valid() || cur->compare(v) < 0);

			*pright = cur;

			/* 2: Check nodes are adjacent */
			if (left_next == *pright) {
				if (*pright == m_tail || !((*pright).is_valid())) {
					return;
				}
		    } else if ((*pleft)->m_next.cas(left_next, *pright)) {
		    	/* 3: Remove one or more marked nodes */
				if (*pright == m_tail || !((*pright).is_valid())) {
					return;
				}
		    }
		}
	}
};
}
#endif
#endif /* LOCKFREE_OLIST_HPP_ */
