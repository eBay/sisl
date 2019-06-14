# Flip

Flip stands for **F**au**l**t **I**njection **P**oint. Its a generic framework for injecting fault into the code. 
It provides a framework, where actual fault could be injected outside the application. 

# Fault Injection

To induce the system to take an alternate path of the code, we simulate faults. One typical way to achieve
this is by explicitly writing a injected fault and make it compile time option to trigger, something like.

```c++
io_status IO::submit(packet_t *pkt) {
    ... 
#ifdef INJECT_SUBMIT_FAULT 
    return io_status::error; 
#endif
    .... 
    return real_submit(pkt); 
}
```

Unfortunately this is extremely limited because every time a fault needs to triggered it needs to be recompiled. Another
limitation to this approach is that it hits on every call to a method irrespective of any condition. This makes it lot of
hand holding and affect automation of the test cases. Flip tries to eliminate these issues and provide a generic framework
to ease out fault injection.

Following are some of the important features of Flip:

* **Multiple fault injection points:** Flip supports multiple fault injection points in a single program.
Each fault injection point is associated with an unique name. Example:

```c++
Flip flip;
io_status IO::submit(packet_t *pkt) {
    if (flip.test_flip("fail_all_writes")) {
        // Do your failure generation here
        return io_status::error;
    }
    return real_submit(pkt);
}
```

* **Trigger fault externally:** One of the major design goal is the ability to trigger these faults externally. Flip provides a
protobuf based serialization, which can be used by any RPC mechanism.

* **Parameterized faults:** In the above example, if there should be a provision for different types of packets to be injected at
different instances, there would be a fault injection for every type. This will quickly become unscalabale approach as more and
more packet types could be added and generic framework idea will be lost. Flip provides parameterized definition of faults. The
above example could be extended to

```c++
Flip flip;
io_status IO::submit(packet_t *pkt) {
    if (flip.test_flip("fail_specific_writes", pkt->op_code)) {
        // Do your failure generation here
        return io_status::error;
    }
    return real_submit(pkt);
}
```

Here the _pkt->op_code_ is the parameter which could be controlled externally in-order to inject the fault. Flip supports filtering
various conditions (not just ==) to the value of the parameter. Hence, while triggering in the above example one can trigger
all OP_TYPE_CREATE or anything but OP_TYPE_CREATE etc.

There are no limits to number of parameters, but the 2 conditions will be anded. The above example could be expanded to
```c++
Flip flip;
io_status IO::submit(packet_t *pkt) {
    if (flip.test_flip("fail_specific_writes", pkt->op_code, pkt->size)) {
        // Do your failure generation here
        return io_status::error;
    }
    return real_submit(pkt);
}
```

* **Return specific values:** It is useful to inject an alternate path, but what will be more useful is whats the error it should
generate as configurable. This will avoid multiple flip points for different types of error generation. Extending above example,
if one wants to simulate return of different errors from IO::submit, one can write similar _flip.test_flip("")_ for all possible
error codes, but it will become too verbose. Flip supports parameterized return as well. Hence it one could do the following
```c++
Flip flip;
io_status IO::submit(packet_t *pkt) {
    auto ret = flip.get_test_flip("fail_specific_writes", pkt->op_code, pkt->size);
    if (ret) {
        // Do your failure generation here
        return ret.get();
    }
    return real_submit(pkt);
}
```

Now fault injection externally can make flip return specific errors, not just io_status::error, but say io_status::corrupted_data
etc..

* **Async delay injection:** One more typical use of fault injection is to delay execution or simulate delays to test timeout code
paths etc.. In a sync code, it is easy to put a sleep to reproduce the delay. However, more and more applications do async operation
or code being completely async. In these cases, there needs a timer routine to keep track of the delay. Flip covers this and
creates a simple async delay injection framework.

```c++
Flip flip;
void IO::process_response(packet_t *pkt) {
    if (flip.delay_flip("delay_response", [this, pkt]() {
            IO::real_process_response(pkt);
        }, pkt->op_code)) {
        return;
    }
    IO::real_process_response(pkt);
```

Above example, provide the fault injection to delay specific opcode. After the configured delay (externally controllable) it calls
the closure. As always number of parameters are unlimited and it is exactly similar to the other types of fault injections explained above.
Also like other faults, it can be controlled externally on how many times and how frequent the faults have to be triggered.

Flip supports combining delay_flip and simulated_value generation flip, so it can generate a specific value after imparting
delay. This will be useful, since after delay an application typically return a timeout error or other types of errors which
will have different behavior in apps, which needed to be tested.

```c++
Flip flip;
void IO::process_response(packet_t *pkt) {
    if (flip.get_delay_flip("delay_response", [this, pkt](io_status generated_error) {
            pkt->status = generated_error;
            IO::real_process_response(pkt);
        }, pkt->op_code)) {
        return;
    }
    IO::real_process_response(pkt);
```

In the above example after delay the value injected externally is passed to the closure, which could be used to simulate
various error scenarios.

# How to use Flip

Flip usage has 2 phases

* **Definition phase:** This is the phase where the declaration of which place and what action needs to be taken in application code
with the fault. It needs to be written before compiling the application code.
* **Injection phase:** This is the phase where the faults are injected or triggered either through local flip client or external client.

There are **4** important parameters that needs to be determined for a fault point. The proto file _proto/flip_spec.proto_ defines
these parameters:

**Flip name**: Unique name identifies this flip point. There can be multiple instances (with different parameters) for same flip
point, say "fail_writes" flip for opcode=1, opcode=2 can coexist (with the same name) in above examples. The name needs to be
declared during definition phase and addressed with that during injection phase.

**Flip Parameters**: The list of parameters that needs to filter out a flip. During definition phase, application code needs to
decide what are the possible filtering attributes to control with. Thus it is advisable to write a flip in possible common portion
of the code and let injection phase decide what it needs to filter on. In above example **_flip.get_test_flip("fail_specific_writes", pkt->op_code, pkt->size)_**
allows the injection phase to filter on opcode and pkt_size. Note that if there are multiple parameters each filter conditions are and'd.

During injection phase, user can supply values and operator for each parameter. Flip as of now only supports primitive types (int, long,
double, bool, uint64) and string, const char* as parameter types. It supports all operators (==, >, <, >=, <=, !=) and one more called "*"
to ignore the check for this parameter.

**Flip Action**: If the fault is triggered what action the application should take. Flip supports 4 types of action

* **No explicit action**: Flip does not take any other action other than returning the fault is hit. Application code will then
write the error simulation code.
* **Return a value action**: A value decided during injection phase (of type determined during definition phase) will be returned
as part of flip hit.
* **Delay action**: Introduce a time delay determined during the injection phase.
* **Delay and return a value action**: Combining above 2.

**Flip frequency**: This is actioned only during injection phase, which determines how frequently and how much the flip has to hit
or trigger the fault.

Count: How many times it needs to hit.
Frequency:
   either percentage of times it needs to hit (to randomly hit for this much percentage) or
   every nth time

## Flip APIs
Flip can be initialized with default constructor. In future it will accept parameters like having application own timer routine
and also specific grpc instance. As of now flip when called with default constructor creates/uses its own timer and thread for
delay and does not provide any RPC service to call.

### test_flip
```c++
template< class... Args >
bool test_flip(std::string flip_name, Args &&... args);
```
Test if flip is triggered or not. Parameters are
* flip_name: Name of the flip
* args: variable list of arguments to filter. Arguments can be of primitive types or std::string or const char *

Returns: If flip is hit or not. Flip is hit only if it matches the filter criteria and frequency of injection criteria. This API
can be called on any of the 4 types of flip.

### get_test_flip
```c++
template< typename T, class... Args >
boost::optional< T > get_test_flip(std::string flip_name, Args &&... args);
```
Test if flip is triggered and if triggered, returns injected value.
* flip_name: Name of the flip
* args: variable list of arguments to filter. Arguments can be of primitive types or std::string or const char *

Returns: If flip is not hit, returns boost::none, otherwise returns the injected value. The injected value cannot be of one of the
primitive types or std::string. This API is only valid for "return a value action" flip type.

### delay_flip
```c++
template< class... Args >
bool delay_flip(std::string flip_name, const std::function<void()> &closure, Args &&... args);
```
Test if flip is triggered and if triggered, calls the supplied closure callback after injected delay time in microseconds.
* flip_name: Name of the flip
* closure: The callback closure to call after the delay, if flip is hit
* args: variable list of arguments to filter. Arguments can be of primitive types or std::string or const char *

Returns: If the flip is hit or not. Whether flip is hit or not is immediately known.

### get_delay_flip
```c++
template<typename T, class... Args >
bool get_delay_flip(std::string flip_name, const std::function<void(T)> &closure, Args &&... args);
```
Test if flip is triggered and if triggered, calls the supplied closure callback after injected delay time in microseconds with
the injected value as a parameter.
* flip_name: Name of the flip
* closure: The callback closure to call after the delay, if flip is hit. The closure should accept the parameter of type which
the fault could be injected with.
* args: variable list of arguments to filter. Arguments can be of primitive types or std::string or const char *

Returns: If the flip is hit or not. Whether flip is hit or not is immediately known.

## Integration with Application

Flip is a header only framework and hence will be included and compiled along with application binary. It uses a protobuf to
serialize the message about how faults can be triggered. The protobuf could be used against any RPCs the application provide.

Flip supports an optional GRPC server which can be started using 

```c++
Flip::start_rpc_server()
```

If application uses GRPC and if another GRPC server creation is to be avoided, then application can instead add the
following grpc definition to its RPC call to the grpc service proto.
```c++
// Inject a fault rpc
rpc InjectFault (flip.FlipSpec) returns (flip.FlipResponse);
```

# Flip Client

Flip needs a client to trigger the faults externally. At present there are 2 forms of flip client, one GRPC client, which
allows flip faults can be injected remotely from external application. Second form is local flip client, which means
flip will be triggered by the same application binary which is to be fault tested.

## GRPC Client

### Python GRPC Client
There is python grpc client library through which it can be triggered remotely. To setup the python client, execute
```
bash ./setup_python_client.sh
```

Python library is available under ***src/client/python/flip_rpc_client.py***

Libraries are defined in class **FlipRPCClient**. Examples of how to use library is provided by the python script
***src/client/python/flip_client_example.py***

### Nodejs GRPC Client

There is a current implementation using GRPC for a project called "NuData/MonstorDB.git" which has nodejs client to inject the fault. Example of
grpc service is provided in path "MonstorDB/nodejs-test/test/support/monstor_client/inject_fault.js" and examples of how to use is
in "MonstorDB/nodejst-test/test/support/run_grpc_client.js"

Example:
```javascript
await test.do_inject_fault(
        "op_error_in_bson",
        [{name : "op_type", oper : FlipOperator.EQUAL.ordinal, value : {string_value : "INSERT"} }], // Conditions}
        { returns : { return : { int_value : 6 } } }, // Returns BSON_DOCUMENT_CORRUPTED
        1,  // count
        100 // percentage
)
```


## Local Client
If the code that needs to be fault injected and tested is a library in itself and that there is separate unit tests which runs
the code, we don't need an RPC, but local calls to trigger the faults. For example, if the tested code runs with GTest or other
unit test framework, the test code runs in the same context as actual code. Flip provides a FlipClient class to trigger faults.

Following are the APIs for FlipClient
### FlipClient()
```c++
FlipClient(Flip *f)
```
Constructs a flipclient providing the flip instances of actual code. Typically FlipClient and Flip could be singleton instances.

### create_condition
```c++
template< typename T>
void create_condition(const std::string& param_name, flip::Operator oper, const T& value, FlipCondition *out_condition);
```
Parameters are:
* param_name: This is not used for any cross verification but just for logging purposes
* oper: One of the flip operators (==, >, <, >=, <= !=, *)
* value: Value parameter of the filter criteria
* condition: Returns the FlipCondition that will be passed in subsequent APIs.

### Inject APIs
```c++
bool inject_noreturn_flip(std::string flip_name, const std::vector< FlipCondition >& conditions, const FlipFrequency &freq);

template <typename T>
bool inject_retval_flip(std::string flip_name, const std::vector< FlipCondition >& conditions,
                        const FlipFrequency& freq, const T& retval);
                         
bool inject_delay_flip(std::string flip_name, const std::vector< FlipCondition >& conditions,
                      const FlipFrequency& freq, uint64_t delay_usec);
template <typename T>
bool inject_delay_and_retval_flip(std::string flip_name, const std::vector< FlipCondition >& conditions,
                                  const FlipFrequency &freq, uint64_t delay_usec, const T& retval);
```

Parameters are:
* flip_name: Name of the flip to inject the fault to
* conditions: Vector of conditions which will be and'd. Each condition can be created using create_condition API
* freq: Flip frequency determines how much and how frequent. Can be constructed using _FlipFrequency::set_count(),
FlipFrequency::set_percent(), FlipFrequency::set_every_nth()_
* retval: (for _inject_retval_flip_ and _inject_delay_and_retval_flip_): What is the injected value to be returned or called back respecitvely
* delay_usec: (for _inject_delay_flip_ and _inject_delay_and_retval_flip_): How much delay to inject

Returns:
* If successfully injected the fault or not.

Example code
```c++
    Flip flip;
    FlipClient fclient(&flip);
    ...
    FlipCondition cond1, cond2;
    fclient.create_condition("cmd_type", flip::Operator::EQUAL, (int)1, &cond1);
    fclient.create_condition("size_bytes", flip::Operator::LESS_THAN_OR_EQUAL, (long)2048, &cond2);
    
    FlipFrequency freq;
    freq.set_count(2); freq.set_percent(100);
    fclient.inject_delay_flip("delay_flip", {cond1, cond2}, freq, 100000 /* delay in usec */);
```
Above examples, trigger a flip called delay flip to inject a delay of 100ms if cmd_type == 1 and size_bytes <= 2048

