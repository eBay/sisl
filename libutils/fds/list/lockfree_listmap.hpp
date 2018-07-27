/*
 * listmap.hpp
 *
 *  Created on: 09-Feb-2017
 *      Author: hkadayam
 */

#ifndef FDS_LIST_LISTMAP_H_
#define FDS_LIST_LISTMAP_H_

#include <iostream>
#include <memory>
#include <cds/init.h>
#include <cds/intrusive/skip_list_dhp.h>

namespace fds {
class ListKey {
  public:
    virtual ~ListKey() {}
    virtual int compare(const ListKey& other) const = 0;
    virtual ListKey& operator=(ListKey& other) = 0;
};

class ListValue : public cds::intrusive::skip_list::node<cds::gc::DHP> {
  public:
    ListValue() {}
    virtual ~ListValue() {}

    virtual void set_key(ListKey& k) = 0;
    virtual ListValue& operator=(ListValue& other) = 0;
    virtual const ListKey* extract_key() const = 0;
};

struct list_value_cmp {
    int operator()(const ListValue& v1, const ListValue& v2) { return v1.extract_key()->compare(*(v2.extract_key())); }
};

typedef cds::intrusive::SkipListSet<
    cds::gc::DHP, ListValue,
    typename cds::intrusive::skip_list::make_traits<
        cds::intrusive::opt::hook<cds::intrusive::skip_list::base_hook<cds::opt::gc<cds::gc::DHP>>>,
        cds::intrusive::opt::compare<list_value_cmp>>::type>
    list_set;

class SkipListMap {
  public:
    SkipListMap() {}
    virtual ~SkipListMap() {}

    bool insert(ListKey& k, ListValue& v, ListValue* outv) {
        v.set_key(k);
        assert(k.compare(*(v.extract_key())) == 0);
        bool done;

        do {
            done = m_set.insert(v);
            if (done) { break; }

            // We should try to get the value and failing to
            // get might mean retry the insert.
            if (get(k, outv)) { break; }
        } while (true);

        return done;
    }

    bool get(ListKey& k, ListValue* outv) {
        outv->set_key(k);
        list_set::guarded_ptr gp(m_set.get(*outv));
        if (!gp) { return false; }

        *outv = *gp;
        return true;
    }

    bool remove(ListKey& k, ListValue* outv) {
        outv->set_key(k);
        list_set::guarded_ptr gp(m_set.extract(*outv));
        if (!gp) { return false; }

        *outv = *gp;
        return true;
    }

    ssize_t size() { return m_set.size(); }

  private:
    list_set m_set;
};
} // namespace fds

#endif /* FDS_LIST_LISTMAP_H_ */
