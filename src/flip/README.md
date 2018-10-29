# Flip

Flip stands for **F**au**l**t **I**njection **P**oint. Its a generic framework for injecting fault into the code. 
It provides a framework, where actual fault could be injected outside the application. 

# Fault Injection

To induce the system to take an alternate path of the code, we explicitly inject faults. One typical way to achieve
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
to ease out fault injection. It provides the following:

**Multiple fault injection points:** Flip supports multiple fault injection points in a single program.
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

**Trigger fault externally:** One of the major design goal is the ability to trigger these faults externally. Flip provides a
protobuf based serialization, which can be used by any RPC mechanism.

**Parameterized faults:** In the above example, if there should be a provision for different types of packets to be injected at
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

**Return specific values:** It is useful to inject an alternate path, but what will be more useful is whats the error it should
generate as configurable. This will avoid multiple flip points for different types of error generation. Extending above example,
if one wants to simulate return of different errors from IO::submit, one can write similar _flip.test_flip("")_ for all possible
error codes, but it will become too verbose. Flip supports parameterized return as well. Hence it one could do the following
```c++
Flip flip;
io_status IO::submit(packet_t *pkt) {
    auto ret = flip.test_flip("fail_specific_writes", pkt->op_code, pkt->size);
    if (ret) {
        // Do your failure generation here
        return ret.get();
    }
    return real_submit(pkt);
}
```

Now fault injection externally can make flip return specific errors, not just io_status::error, but say io_status::corrupted_data
etc..

**Async delay injection:** One more typical use of fault injection is to delay execution or simulate delays to test timeout code
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