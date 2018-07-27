/*
 * listset.hpp
 *
 *  Created on: 12-May-2017
 *      Author: hkadayam
 */

#ifndef FDS_LIST_LISTMAP_H_
#define FDS_LIST_LISTMAP_H_

#include <iostream>
#include <memory>
#include <cds/init.h>
#include <cds/intrusive/skip_list_dhp.h>

namespace fds {

class SkipListNode : public cds::intrusive::skip_list::node<cds::gc::DHP> {
  public:
    SkipListNode() {}
    virtual ~SkipListNode() {}

    virtual SkipListNode& operator=(const SkipListNode& other) = 0;
    virtual int compare(const SkipListNode& other) const = 0;
};

struct list_node_cmp {
    int operator()(const SkipListNode& v1, const SkipListNode& v2) { return v1.compare(v2); }
};

typedef cds::intrusive::SkipListSet<
    cds::gc::DHP, SkipListNode,
    typename cds::intrusive::skip_list::make_traits<
        cds::intrusive::opt::hook<cds::intrusive::skip_list::base_hook<cds::opt::gc<cds::gc::DHP>>>,
        cds::intrusive::opt::compare<list_node_cmp>>::type>
    list_set;

class SkipListSet {
  public:
    SkipListSet() {}
    virtual ~SkipListSet() {}

    inline bool insert(SkipListNode& n) { return m_set.insert(n); }

    inline bool get(SkipListNode* outn) {
        list_set::guarded_ptr gp(m_set.get(*outn));
        if (!gp) { return false; }

        *outn = *gp;
        return true;
    }

    bool remove(SkipListNode* outn) {
        list_set::guarded_ptr gp(m_set.extract(*outn));
        if (!gp) { return false; }

        *outn = *gp;
        return true;
    }

    ssize_t size() { return m_set.size(); }

  private:
    list_set m_set;
};
} // namespace fds
#endif
