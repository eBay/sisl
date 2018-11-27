/*
 * utils.cpp
 *
 *  Created on: Sep 25, 2018
 */

#include "sds_grpc/utils.h"
#include <fstream>
#include <sstream>

namespace sds::grpc {

bool get_file_contents(const std::string & file_name, std::string & contents) {
    try {
        std::ifstream in(file_name.c_str(), std::ios::in);
        if (in) {
            std::ostringstream t;
            t << in.rdbuf();
            in.close();

            contents = t.str();
            return true;
        }
    } catch (...) {

    }
    return false;
}





}


