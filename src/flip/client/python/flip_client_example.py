from __future__ import print_function

import random
import logging
from flip_rpc_client import *

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
                             )

    fclient.flip_details("flip2")
    fclient.all_flip_details()