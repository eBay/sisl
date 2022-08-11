# Wisr

WISR stands for Waitfree Inserts Snoozy Rest. This is a framework and data structures on top of this framework which provides
ultra high performance waitfree inserts into the data structures, but pretty slow read and update operations. It is thread safe
and thus good use cases are to collect metrics, garbage collection etc, where one would want to quickly append the data into
some protected list, but scanning them is few and far between. It uses RCU (Read side during insert and write side during other
operations).

In any practical high performance application, there would be a need to collect data during critical path and then analyze or
process the data later on slightly non-critical path, in parallel to critical path. There are several very good concurrent 
data structures available, but majority is bound to specific data structures and a generic ones are tuned towards waitfree
reads at the cost of either lockfree writes or locked writes. **waitfree latency < lockfree latency < synchronized latency.**
Wisr tries to solve the problem to provide very high performing writes (waitfree writes) and relatively slow read operations.
Benchmark shows write side perform 10-12x better performance than mutex and 1.5x better on read side compared to shared mutex

Before looking into wisr, its worth looking at possible solutions to solve a common problem. Lets say an user has a data structure of 
type T, which needs to collect data during critical path and slightly non-critical path read the structure T.

## Mutex
One common way to solve the problem is both read and write are surrounded by mutex (shared_mutex for better concurrency). 
The advantage of this approach is 
* Familiarity: Its a common approach and no special changes or understanding is needed
* The data structure can be arbitrary sized and is not bound by 8 bytes or so limits other solutions listed below

The major disadvantage is 
* Performance: Despite shared_mutex, taking a mutex itself is highly cpu bound for a high performance applications. The performance
difference for each of them could range 100s of nano seconds per operation and thus few updates per IO could cost multiple micro 
seconds which might not be ideal too.

## Atomic
If the data structure itself are <= 8 bytes, then another common approach is doing atomic variables. In this approach we declare 
std::atomic< T > and access the structure using atomic instructions. Here both read/write side has similar penalty and much faster 
than mutex on some cases. However, the penalty varies on operations we perform. If its a simple load/store on the data structure,
then its performance nearly zero overhead cost (other than a memory barrier). Lets say if T = uint64 and we use them as gauge metric,
then using them store the value atomically and then reader side reading them is fast enough that difference with non-atomic is
almost negligible. However, the cost increases almost 3x times (compared to load/store), if it is atomic fetch and increment/decrement.
If it is a compare and swap operation (say to find out max/min etc..), the cost is more than 5x and sometimes it is boundless depending
on multi-threading usage pattern. 

## sisl::ThreadBuffer
This sisl library introduces another structure called ThreadBuffer. *sisl::ThreadBuffer< T >* is simple concept in that it creates object
of the said type T for every thread that access this structure. Thus it has N unique versions of member T for N thread that access this
structure and each thread access their portion of structure to perform operation (insert). It provides methods to access all of these 
members, which can be then be used in read operation to gather all per thread version and can be combined. 
The advantage of this approach is
* Performance: It is the fastest of all the things listed here, especially in insert operation side, practically there is no extra 
  synchronization, so it has absolute zero overhead. Even on read side, it only takes a mutex and walks all threads and so very little
  overhead.
  
The major disadvantage is 
* Like atomics this cannot work atomically beyond 8 bytes limit (or 16 bytes on DWORD processors). Even for that, there are compiler
  barriers and hence one need to send signal to all threads (or use sys_membarrier()) system call to flush all threads. Example of
  such approach is provided in metrics/metrics_tlocal.cpp

## Wisr
Wisr framwework is an extension of ThreadBuffer way, where instead of accessing member T* as is, it is protected by RCU and thus provide
atomicity and also very high performance. *sisl::wisr_framework< T >* creates a version of T for each thread it accesses. However, the
T* member of each thread is protected by RCU (Read Copy Update). If an insert operation has to be performed, it uses read side of RCU
to access T* member of the thread and modify them. When a read operation is performed, then it uses ThreadBuffers access all threads
and each T* member access write side of RCU and then swap the pointer with new and merge the results from each thread.

wisr_framework expects one static member of T defined called `T::merge(T* to, const T* from)` which merges the given object to other. wisr
already predefines and exports `wisr_vector`, `wisr_list`, `wisr_dequeue`

It has a huge performance advantage over Mutex and Atomic and also provide atomic access for any size.

A benchmark sample on wisr vector compared to shared_mutex vector is provided below

```
# ./bin//wisr_vector_benchmark
2020-08-08 02:00:35
Running ./bin//wisr_vector_benchmark
Run on (32 X 3000 MHz CPU s)
CPU Caches:
  L1 Data 32K (x16)
  L1 Instruction 32K (x16)
  L2 Unified 1024K (x16)
  L3 Unified 11264K (x2)
Load Average: 2.72, 1.19, 0.64
***WARNING*** CPU scaling is enabled, the benchmark real time measurements may be noisy and will incur extra overhead.
---------------------------------------------------------------------------------------------
Benchmark                                                   Time             CPU   Iterations
---------------------------------------------------------------------------------------------
test_locked_vector_insert/iterations:100/threads:8      79755 ns       557003 ns          800
test_wisr_vector_insert/iterations:100/threads:8         1247 ns         7714 ns          800
test_locked_vector_insert/iterations:100/threads:1      15143 ns        15143 ns          100
test_wisr_vector_insert/iterations:100/threads:1         2101 ns         2103 ns          100
test_locked_vector_read/iterations:100/threads:1        83536 ns        83540 ns          100
test_wisr_vector_read/iterations:100/threads:1          25274 ns        25275 ns          100
```
