# Settings Framework

Any production grade systems level application will have multiple configurations to enable tuning for different set of 
use cases. This is more true with cloud based applications. One common way to achieve the configurations is to put a 
#define or constexpr instead of hard coding. But tuning these parameters means the entire application has to be 
recompiled and redeployed. It is very expensive and disruptive operation.

Alternate to that would be to put as some sort of json file and each application can read the json and put in some
sort of structure or read the value from json file. However, reading directly from json is resource intensive and doing
so on every operation is wasteful and performance impacting. Also changing these settings means application needs to
write quiet a bit of code to reload these settings in a thread safe, memory safe manner.

This framework provides easy way to group the configuration for an application, and provides a thread safe, memory safe,
highly efficient access of the configuration and also provides easy way to reload the settings. It supports both 
hot-swappable and non-hot-swappable configurations.

## How it works
It uses flatbuffers serialization to represent the hierarchical schema of configurations. The schema provides all
possible values with their default values. Once defined application needs to add the generated code into their library 
and can use the method this framework provides to access and 

Following are the tools it provides so far

## Async HTTP Server

Provides an HTTP REST Server for asynchronous programming model. It works on top of evhtp library, but wraps threading model
C++ methods for evhtp C library.

## Metrics

A very high performance metrics collection (counters, histograms and gauges) and report the results in form of json or 
sent to prometheus whichever caller choose from. It is meant to scale with multiple threads and huge amount of performance
metrics. The collection is extremely fast <<5ns per metric, but pay penalty during metrics result gathering which is rare. It
uses Wisr framework which will be detailed next

## Wisr

WISR stands for Waitfree Inserts Snoozy Rest. This is a framework and data structures on top of this framework which provides
ultra high performance waitfree inserts into the data structures, but pretty slow read and update operations. It is thread safe
and thus good use cases are to collect metrics, garbage collection etc, where one would want to quickly append the data into
some protected list, but scanning them is few and far between. It uses RCU (Read side during insert and write side during other
operations).

## FDS
This is a bunch of data structures meant for high performance or specific use cases
### Sparse Vector
A typical vector, but insert can mention the slot to insert and thus can be sparsely populated.

### Sorted Vector Set

