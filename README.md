# SymbiosisLib (sisl)

This repo provides a symbiosis of libraries (thus named sisl - pronounced like sizzle) mostly for very high performance data 
structures and utilities. This is mostly on top of folly, boost, STL and other good well known libraries. Thus its not trying 
to replace these libraries, but provide a layer on top of it. In general there are 3 variations of these libraries

* Libraries which are higher performing than standard libraries for specific use cases
* Libraries which wrap existing library to provide simplistic view or use cases
* Libraries that fill any missing gaps.

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

