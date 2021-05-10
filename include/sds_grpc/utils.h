/*
 * utils.h
 *
 *  Created on: Sep 25, 2018
 */

#pragma once

#include <string>

namespace sds::grpc {

bool get_file_contents(const std::string& file_name, std::string& contents);

}

