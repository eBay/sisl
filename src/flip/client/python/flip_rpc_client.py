from __future__ import print_function

import random
import logging
import sys
sys.path.append("gen_src")

import grpc
import flip_spec_pb2 as fspec
import flip_spec_pb2_grpc
import flip_server_pb2
import flip_server_pb2_grpc

class FlipRPCClient:
    def __init__(self, server_address):
        self.channel = grpc.insecure_channel(server_address)
        self.stub = flip_server_pb2_grpc.FlipServerStub(self.channel)
 
    def inject_fault(self, name, freq, conds, action):
        self.stub.InjectFault(fspec.FlipSpec(flip_name=name, conditions=conds, flip_action=action, flip_frequency=freq))

    def inject_test_flip(self, name, freq, conds):
        print("------ Inject test flip", name, "-------------")
        self.inject_fault(name, freq, conds, fspec.FlipAction(no_action=1))

    def inject_ret_flip(self, name, freq, conds, retval):
        print("------ Inject ret flip", name, "-------------")
        self.inject_fault(name, freq, conds, fspec.FlipAction(returns=fspec.FlipAction.ActionReturns(retval=retval)))

    def inject_delay_flip(self, name, freq, conds, delay_usec):
        print("------ Inject delay flip", name, "-------------")
        self.inject_fault(name, freq, conds,
                          fspec.FlipAction(delays=fspec.FlipAction.ActionDelays(delay_in_usec=delay_usec)))

    def inject_delay_ret_flip(self, name, freq, conds, delay_usec, retval):
        print("------ Inject delay and then ret flip", name, "-------------")
        self.inject_fault(name, freq, conds,
                          fspec.FlipAction(delay_returns=fspec.FlipAction.ActionDelayedReturns(
                                                                                      delay_in_usec=delay_usec,
                                                                                      retval=retval)))

    def flip_details(self, name):
        list_response = self.stub.GetFaults(flip_server_pb2.FlipNameRequest(name=name))
        for finfo in list_response.infos:
            print(finfo.info)

    def all_flip_details(self):
        list_response = self.stub.GetFaults(flip_server_pb2.FlipNameRequest(name=None))
        for finfo in list_response.infos:
            print(finfo.info)

""" def run():
    with grpc.insecure_channel('localhost:50051') as channel:
        stub = flip_server_pb2_grpc.FlipServerStub(channel)
        print("------ Inject Fault -------------")
        #stub.InjectFault(flip_spec_pb2.FlipSpec(flip_name="xyz", flip_action=flip_spec_pb2.FlipAction.action(no_action = true)))
        #cond = flip_spec_pb2.FlipCondition(name = "my_id", oper = flip_spec_pb2.Operator(EQUAL), value = flip_spec_pb2.ParamValue(kind = flip_spec_pb2.ParamValue.kind(null_value=true) ))
        action = flip_spec_pb2.FlipAction(no_action=1)
        freq = flip_spec_pb2.FlipFrequency(count=2)
        stub.InjectFault(
            flip_spec_pb2.FlipSpec(flip_name="xyz",
                                   flip_action=action,
                                   flip_frequency=freq))


def connect():
    with grpc.insecure_channel('localhost:50051') as channel:
        global stub
        stub = flip_server_pb2_grpc.FlipServerStub(channel)
        print("Stub = ", stub)

if __name__ == '__main__':
    logging.basicConfig()
    fclient = FlipRPCClient('localhost:50051')

    fclient.inject_test_flip("flip1",
                             fspec.FlipFrequency(count=4, percent=100),
                             [
                                fspec.FlipCondition(oper=fspec.Operator.NOT_EQUAL, value=fspec.ParamValue(int_value=5)),
                                fspec.FlipCondition(oper=fspec.Operator.DONT_CARE)
                             ])
    fclient.inject_test_flip("flip2",
                             fspec.FlipFrequency(count=2, every_nth=5),
                             [])

    fclient.inject_ret_flip("flip3",
                             fspec.FlipFrequency(count=2, percent=100),
                             [
                                fspec.FlipCondition(oper=fspec.Operator.NOT_EQUAL, value=fspec.ParamValue(int_value=5)),
                             ],
                             fspec.ParamValue(string_value="Simulated corruption")
                            )

    fclient.inject_delay_flip("flip4",
                              fspec.FlipFrequency(count=10000, percent=100),
                              [
                                 fspec.FlipCondition(oper=fspec.Operator.GREATER_THAN_OR_EQUAL,
                                                     value=fspec.ParamValue(long_value=50000)),
                              ],
                              1000
                             )

    fclient.inject_delay_ret_flip("flip5",
                              fspec.FlipFrequency(count=1, percent=50),
                              [
                                 fspec.FlipCondition(oper=fspec.Operator.LESS_THAN_OR_EQUAL,
                                                     value=fspec.ParamValue(double_value=800.15)),
                              ],
                              1000,
                              fspec.ParamValue(bool_value=False)
                             ) """