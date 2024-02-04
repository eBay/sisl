from __future__ import print_function

import random
import logging
import sys

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
