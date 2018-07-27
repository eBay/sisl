/*
 * mempool.hpp
 *
 *  Created on: 27-Jan-2017
 *      Author: hkadayam
 */

#ifndef CPP_PROXY_SRC_LIBUTILS_FDS_MEM_MEMPOOL_HPP_
#define CPP_PROXY_SRC_LIBUTILS_FDS_MEM_MEMPOOL_HPP_

namespace fds {
#define INVALID_MEM_ID ((uint32_t)-1)

class mem_id_t {
  public:
    uint32_t internal_id : 26;
    uint32_t pool_no : 5;
    uint32_t userdef : 1;

    static mem_id_t form(uint32_t n) {
        mem_id_t id_m;
        memcpy(&id_m, &n, sizeof(uint32_t));
        return id_m;
    }

    uint32_t to_uint32() {
        uint32_t n;
        memcpy(&n, this, sizeof(uint32_t));
        return n;
    }

    void set_userdef(uint32_t b) { userdef = b; }
} __attribute__((packed));

class mempool {
    /* Alloc and Free method variations */
    virtual uint8_t* alloc(size_t size, mem_id_t* pid_m = nullptr) = 0;
    virtual void free(uint8_t* mem) = 0;

    /* Method to validate if the memory is owned by the pool */
    virtual bool owns(uint8_t* mem) = 0;

    /* Conversion from one to other type methods */
    virtual uint8_t* mem_get(mem_id_t& id_m) = 0;
};

} // namespace fds

#endif /* CPP_PROXY_SRC_LIBUTILS_FDS_MEM_MEMPOOL_HPP_ */
