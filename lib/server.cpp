/*
 * server.cpp
 *
 *  Created on: Oct 24, 2018
 */

#include <sds_grpc/server.h>


namespace sds::grpc
{

void BaseServerCallData::proceed() {
    if (status_ == CREATE){
        status_ = PROCESS;
        do_create();
    } else if (status_ == PROCESS) {
        // status must be changed firstly, otherwise this may
        // cause concurrency issue with multi-threads
        status_ = FINISH;
        do_process();
    } else {
        do_finish();
    }
}


void BaseServerCallData::do_finish(){
    GPR_ASSERT(status_ == FINISH);
    // Once in the FINISH state, this can be destroyed
    delete this;
}

}
