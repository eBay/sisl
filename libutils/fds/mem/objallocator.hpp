/*
 * objallocator.hpp
 *
 *  Created on: 20-Jan-2017
 *      Author: hkadayam
 */

#ifndef OBJALLOCATOR_HPP_
#define OBJALLOCATOR_HPP_

#include <initializer_list>
#include "memallocator.hpp"
#include "smart_ptr.hpp"

namespace fds {
template <typename T> class obj_allocator {
  public:
    static obj_allocator<T>* inst;

    static obj_allocator<T>* instance() {
        if (inst == nullptr) { inst = new obj_allocator(); }
        return inst;
    }

    obj_allocator() {}

    ~obj_allocator() {}

    template <typename... Args> fds::smart_ptr<T> alloc(Args&&... args) {
        uint8_t* mem = fds::malloc(sizeof(T));

        T* o = new (mem) T(std::forward<Args>(args)...);
        fds::smart_ptr<T> ptr(o);

        return ptr;
    }

    void free() {}

  private:
};
} // namespace fds
#endif /* OBJALLOCATOR_HPP_ */
