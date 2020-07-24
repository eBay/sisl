# High Performing Metrics Captures
On any high performing applications, collecting and reporting metrics is one of the key requirements. It provides clear
insight to the application and some times help find bugs. However, for very high performing applications, collecting
metrics itself might be hindering performance and developers in those cases could use conservative approach to adding
metrics. This library aims to provide very high performing metrics collection and reporting among other features.

At high level this library provides following key features
* Various choices for very high performance metrics collection
* Different reporting of metrics
* Framework to organize multiple dimensions of metrics and do compile time look of them.

While this README takes prometheus as an example, it can be extended or used for any metrics server.

## Performance of Metrics collection. 
Prometheus C++ is a well known open source product for collection, reporting, maintaining of metrics. It provides a client
library for application to capture individual metrics and then provide a method to serialize into prometheus format. It
provides a multi-threaded safe way to capture the metrics, by using std::atomic variables. The key issue is having
lots of atomic fetch_and_add in a system increases the CPU impact. Having few of the metrics does not matter, however,
if at any given point the application updates lots of metrics, it comes out of its negligibility.

sisl::metrics provide few alternatives to this approach

### ThreadBufferWithSignal
In this method, it avoids atomic variables, by replacing it with a *sisl::ThreadBuffer*. 

**About ThreadBuffer**: ThreadBuffer is simply an header only implementation which keeps track of the threads that 
uses the facility and as soon as thread comes in, it calls all registered callers to initialize a specific version of 
its instance for this thread. Thus each thread can access these objects in lock-free manner. This is different than a 
thread-local variable that OS/language provides, in that this thread buffer can be a member in a class/struct and can be
non-static. Apart from that it provides method to access a specific threads buffer or all the thread buffers from a
non-participating thread as well. So far it is very similar to *folly::ThreadLocalPtr*. However, sisl::ThreadBuffer 
provides one more critical functionality which is exit safety. Once thread has exited, typically that threads version of
buffer is also gone. This might be ok for several scenarios, but on some occasions it is important to retain the thread 
buffer even after thread exits. One such requirement is for wait free metrics collection. 

**ThreadBufferWithSignal**: Using this threadbuffer, sisl::metrics library collects all the metrics in its own thread and access, counters, 
histograms in a wait free manner. Even if the thread exits, the ThreadBuffer class should hold onto the metrics
its collected, long enough for next scrapping, before it discards them. Each time a metric, say counter is update, it
is simply updating its thread version of it. As itself, this is not correct value. However, when the metrics are 
scrapped or viewed - which is probably typically once a minute or some cases on-demand - it gathers metrics from all
the thread buffers and then merges them and reports them.

It is important to note that when scrapping happens it will be accessing other threads metrics. However, when scrapping
and collection are running in multi-threaded fashion, there is no absolute point-in-time metrics unless entire system
is locked, which is highly undesirable. However, while access other threads metrics without locks or atomics, we 
suffer from L1/L2/L3 caches in-consistency across cores. To overcome that, this style of metrics gathering, send a
signal to all participating threads to do a atomic fencing and then upon confirmation, it access other thread's data.
Once all thread data is collected, they are merged and then reported using configured reporter.

Please note that this method still does not guarantee atomicity of all metrics. Histograms are typically represented
as <array of buckets> + <sum of all samples>. This cannot be possibily atomically updated. However, key point is
most of the metrics collection - including promethus library does not support that atomicity. Given that its a metric,
inconsistency on sample, shouldn't skew the results and in general its an acceptable approach.

### WISR RCU Framework

<More to be added soon>