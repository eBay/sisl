#include <iostream>
//#include "objallocator.hpp"
#include "farmhash.h"
#include "libutils/fds/mem/memallocator.hpp"
#include "libutils/fds/smart_ptr.hpp"
//#include "/Users/hkadayam/src/downloads/e/e/lockfree_hash_map.h"

class value {
  public:
    value() {
        m_n1 = 0;
        m_n2 = 0;
    }

  public:
    long m_n1;
    int m_n2;
    fds::smart_ptr<value> m_next;
};

#if 0
class Key
{
public:
	Key()
	{
		m_str[0] = '\0';
	}

	char m_str[100];

	friend bool operator< (const Key &k1, const Key &k2);
};

bool operator <(const Key &k1, const Key &k2)
{
	return (strcmp(k1.m_str, k2.m_str) < 0);
}

bool operator ==(const Key &k1, const Key &k2)
{
	std::cout << "Comparing k1 " << k1.m_str << " with k2 " << k2.m_str << endl;
	bool ret = (strcmp(k1.m_str, k2.m_str) == 0);
	cout << "Returning " << ret << endl;
	return ret;
}

uint64_t get_hash_code(const Key &k)
{
	return util::Hash64(k.m_str, 100);
}
#endif

fds::mem_allocator* fds::mem_allocator::inst = nullptr;

// ds::obj_allocator<Value> *oallocator;

void func1(fds::smart_ptr<value> sptr2) {
    cout << "-------------------" << endl;
    cout << "In func1" << endl;
    sptr2->m_n1 = 20;
    sptr2->m_n2 = 200;

    cout << "First destruction expected" << endl;
    cout << "-------------------" << endl;
}

void func2(fds::smart_ptr<value>& sptr) {
    cout << "-------------------" << endl;
    cout << "In func2" << endl;
    fds::smart_ptr<value> sptr3 = sptr;
    sptr3->m_n1 = 30;
    sptr3->m_n2 = 300;

    cout << "Second destruction expected" << endl;
    cout << "-------------------" << endl;
}

fds::smart_ptr<value> add(fds::smart_ptr<value> pprev, int index) {
    // Value *v = (Value *)ds::malloc(sizeof(Value));
    // ds::smart_ptr<Value> pcur(v);

    fds::smart_ptr<value> pcur = fds::smart_ptr<value>::construct();

    pcur->m_n1 = index + 1;
    pcur->m_n2 = (index + 1) * 10;
    pprev->m_next = pcur;

    return pcur;
}

int main(int argc, char* argv[]) {
    fds::smart_ptr<value> prev;

    // oallocator = new ds::obj_allocator<Value>();

    //	Value *v = (Value *)ds::malloc(sizeof(Value));
    //	ds::smart_ptr<Value> head(v);
    // ds::smart_ptr<Value> head = oallocator->alloc();
    fds::smart_ptr<value> head = fds::smart_ptr<value>::construct();
    head->m_n1 = 0;
    head->m_n2 = 0;

    for (auto i = 0; i < 2; i++) {
        cout << "Loop " << i + 1 << endl;
        cout << "############" << endl;
        if (i == 0) {
            prev = add(head, i);
        } else {
            prev = add(prev, i);
        }
    }

    cout << "Done with test. Must be calling all destructors" << endl;
    cout << "----------------" << endl;
#if 0
	cout << "Step1: new value: m_n1 = " << sptr1->m_n1 << " m_n2 = " << sptr1->m_n2 << endl;

	value *v2 = (value *)fds::malloc(sizeof(value));
	fds::smart_ptr<value> sptr1(v1);

	func1(sptr1);
	func2(sptr1);

	cout << "new value: m_n1 = " << sptr1->m_n1 << " m_n2 = " << sptr1->m_n2 << endl;
	cout << "Third destruction expected" << endl;
#endif
}

#if 0
int main(int argc, char *argv[])
{
	e::lockfree_hash_map<Key, value, get_hash_code> hm;

	value t1;
	t1.m_n1 = 10;
	t1.m_n2 = 100;
	Key k1;
	strcpy(k1.m_str, "First");

	value t2;
	t2.m_n1 = 20;
	t2.m_n2 = 200;
	Key k2;
	strcpy(k2.m_str, "Second");

	cout << "Insert 1 returned " << hm.insert(k1, t1) << endl;
	cout << "Insert 2 returned " << hm.insert(k2, t2) << endl;
	//cout << "Insert 3 returned " << hm.insert(k2, t2) << endl;

	value t3;
	Key k3;
	strcpy(k3.m_str, "Second");
	cout << "lookup returned " << hm.lookup(k3, &t3) << endl;
	cout << "Result: n1 = " << t3.m_n1 << " n2 = " << t3.m_n2 << endl;

	return 0;
}
#endif
