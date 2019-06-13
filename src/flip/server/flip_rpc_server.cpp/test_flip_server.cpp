//
// Created by Kadayam, Hari on 28/03/18.
//

#include "flip.hpp"

int main(int argc, char *argv[]) {
    flip::Flip f;
    f.start_rpc_server();

    sleep(1000);
    return 0;
}