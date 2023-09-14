# SymbiosisLib (sisl)
[![Conan Build](https://github.com/eBay/sisl/actions/workflows/merge_build.yml/badge.svg?branch=master)](https://github.com/eBay/sisl/actions/workflows/merge_build.yml)
[![CodeCov](https://codecov.io/gh/eBay/sisl/branch/master/graph/badge.svg)](https://codecov.io/gh/eBay/Sisl)

This repo provides a symbiosis of libraries (thus named sisl - pronounced like sizzle) mostly for very high performance data 
structures and utilities. This is mostly on top of folly, boost, STL and other good well known libraries. Thus its not trying 
to replace these libraries, but provide a layer on top of it. In general there are 3 variations of these libraries

* Libraries which are higher performing than standard libraries for specific use cases
* Libraries which wrap existing library to provide simplistic view or use cases
* Libraries that fill any missing gaps.

Following are the tools it provides so far

## Whats in this library
### Metrics

A very high performance metrics collection (counters, histograms and gauges) and report the results in form of json or 
sent to prometheus whichever caller choose from. It is meant to scale with multiple threads and huge amount of performance
metrics. The collection is extremely fast <<5ns per metric, but pay penalty during metrics result gathering which is rare. It
uses Wisr framework which will be detailed next

*Lacks MacOS support*

### Wisr

WISR stands for Waitfree Inserts Snoozy Rest. This is a framework and data structures on top of this framework which provides
ultra high performance waitfree inserts into the data structures, but pretty slow read and update operations. It is thread safe
and thus good use cases are to collect metrics, garbage collection etc, where one would want to quickly append the data into
some protected list, but scanning them is few and far between. It uses RCU (Read side during insert and write side during other
operations).

More details in the Wisr README under [src/wisr/README.md]

### FDS
This is a bunch of data structures meant for high performance or specific use cases. Each of these structures are detailed in their 
corresponding source files. Some of the major data structures are listed below:

*Lacks MacOS support*

#### Bitset
A high performance bitset to have various functionalities to scan the contiguous 1s, 0s, set/reset multiple bits without iterating over
every bit, ability to serialize/deserialize bitsets, atomically update concurrent bits, ability to dynamically resize and shrink. It
has many functionalities which are not provided by std::bitset or boost::dynamic_bitset or folly::AtomicBitset

#### StreamTracker
Support a very popular pattern of tracking sequential entities, where key is an integer and value is any structure. It tracks consecutive
completions of the key and sweep everything that are completed. It is an essential pattern seen in multiple stream processing and this
container provides concurrent access without exclusive locks for all critical operations.

#### MallocHelper
To be able to use either tcmalloc or jemalloc and have consistent tunables across both the mallocs and metrics collections into prometheus.

#### ThreadBuffer
This is an enhanced version of per thread buffer, where the buffers are optionally tracked even after the thread exits till it is consumed.
This pattern is essential to build reliable structures using Wisr framework.

#### VectorPool
Capture the vector in a pool in thread local fashion, so that vectors are not built from scratch everytime.

### Settings Framework
Please refer to the README under [src/settings/README.md]

### Flip
Flip is fault injection framework, Please refer to the README under [src/flip/README.md]

## Installation
This is mostly header only library and can be just compiled into your code. There are some of the pieces which needs a library (libsisl)
to be built. 

### With conan
Assuming the conan setup is already done

```
$ ./prepare.sh # this will export some recipes to the conan cache
$ mkdir build
$ cd build

# Install all dependencies
$ conan install ..

# Build the libsisl.a
$ conan build ..
```

### Without conan
To be Added

## Contributing to This Project
We welcome contributions. If you find any bugs, potential flaws and edge cases, improvements, new feature suggestions or discussions, please submit issues or pull requests.

Contact
Harihara Kadayam hkadayam@ebay.com

## License Information
Copyright 2021 eBay Inc.

Primary Author: Harihara Kadayam

Primary Developers: Harihara Kadayam, Rishabh Mittal, Bryan Zimmerman, Brian Szmyd

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
