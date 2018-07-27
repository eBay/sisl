#for standadlone local unit and integration testing

(1) to build the dependent modules required by MonstorDB. to run ./deps.sh under MonstorDB root directory.

(2) to build the library:
y
mkdir build
cd build
cmake -DCMAKE_DEPENDENT_MODULES_DIR=../../deps_prefix -DUNITTEST=1 ..

then issue:
make

to show the compilation details, issue:

make VERBOSE=1