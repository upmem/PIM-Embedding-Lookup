import argparse
import os

from ctypes import *
from dputypes import *

# DEFAULTS #
so_file = None
my_functions = None
num_dpu=12
num_batches=32

# Defaults, can be overriden by cli arguments
if __name__ != "__main__":
    so_file = "./emblib.so"
    my_functions = CDLL(os.path.abspath(so_file))
    num_dpu=12
    num_batches= 32


def parse():
    parser = argparse.ArgumentParser(description="Test embedding")

    parser.add_argument(
        "--so_file", help="Input DLL", default="./emblib.so", type=str)
    parser.add_argument(
        "--num_dpu", help="Input number of DPUs", default=num_dpu, type=int)
    parser.add_argument(
        "--runtimes", help="Report runtimes", default=False, type=bool)
    parser.add_argument(
        "--runtime_file", help="Runtime CSV report", default='runtimes.csv', type=str)

    return parser.parse_args()


def populate(config, runtimes):
    my_functions.populate_mram.argtypes = c_uint32, c_uint64, POINTER(c_int32), POINTER(DpuRuntimeTotals)
    my_functions.populate_mram.restype = None
    data_ptr = (c_int32 * 24)(1, 2, 3, 4, 5, 6, 2, 4, 6, 8, 10, 12, 3, 6, 9, 12, 15, 18, 4, 8, 12, 16, 20, 24)
    for i in range (0, config.num_dpu):
        my_functions.populate_mram(i, 4, data_ptr, runtimes)


def lookup(config, runtime_group=None):
    my_functions.lookup.argtypes = POINTER(c_uint32), POINTER(c_uint32), POINTER(c_uint64), POINTER(c_uint64), POINTER(c_int32), POINTER(DpuRuntimeGroup)
    my_functions.lookup.restype = None
    indices=[]
    offsets=[]
    indices_len=[]
    offsets_len=[]
    for i in range(0,config.num_dpu):
        for j in range(0,num_batches):
            indices.extend([1,3,2,0])
            offsets.append(j*4)
        indices_len.append(4*num_batches)
        offsets_len.append(num_batches)

    indices_ptr=(c_uint32 * len(indices))(*indices)
    offsets_ptr=(c_uint32 * len(offsets))(*offsets)
    indices_len_ptr=(c_uint64 * len(indices_len))(*indices_len)
    offsets_len_ptr=(c_uint64 * len(offsets_len))(*offsets_len)
    ans=(c_int32 * (num_batches*6*config.num_dpu))()

    for _ in range(8):
        my_functions.lookup(indices_ptr, offsets_ptr, indices_len_ptr, offsets_len_ptr, ans, runtime_group)

    if runtime_group is not None:
        write_results(config, runtime_group)


if __name__ == "__main__":
    config = parse()
    so_file = config.so_file
    my_functions = CDLL(os.path.abspath(so_file))

    runtimes = pointer(DpuRuntimeTotals())
    populate(config, runtimes)

    rg = None
    if config.runtimes:
        runtimes_init = [DpuRuntimeGroup(length=8)] * config.num_dpu
        rg = (DpuRuntimeGroup * num_dpu)(*runtimes_init)
    lookup(config, runtime_group=rg)
