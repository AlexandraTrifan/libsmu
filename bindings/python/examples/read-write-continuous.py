#!/usr/bin/env python
#
# Simple script showing how to read and write data to a device in continuous
# mode, use Ctrl-C to exit.

from __future__ import print_function

from signal import signal, SIG_DFL, SIGINT
import sys
import random

from pysmu import Session, Mode


def refill_data(num_samples, v=None):
    if v is None:
        # fill channels with a static, random integer between 0 and 5
        v = random.randint(0, 5)
    return [v] * num_samples
    # fill channels with a static, random float between -0.2 and 0.2
    #return [random.uniform(-0.2,0.2)] * num_samples


if __name__ == '__main__':
    # don't throw KeyboardInterrupt on Ctrl-C
    signal(SIGINT, SIG_DFL)

    session = Session()

    if session.devices:
        # Grab the first device from the session.
        dev = session.devices[0]

        # Ignore read buffer overflows when printing to stdout.
        dev.ignore_dataflow = sys.stdout.isatty()

        # Set both channels to source voltage, measure current mode.
        chan_a = dev.channels['A']
        chan_b = dev.channels['B']
        chan_a.mode = Mode.SVMI
        chan_b.mode = Mode.SVMI
        #chan_a.mode = Mode.SIMV
        #chan_b.mode = Mode.SIMV

        # Start a continuous session.
        session.start(0)
        v = 0
        num_samples = 1024

        while True:
            # Write iterating voltage values to both channels.
            chan_a.write(refill_data(num_samples, v % 6))
            chan_b.write(refill_data(num_samples, v % 6))
            v += 1

            # Read incoming samples in a non-blocking fashion.
            samples = dev.read(num_samples)
            for x in samples:
                print("{: 6f} {: 6f} {: 6f} {: 6f}".format(x[0][0], x[0][1], x[1][0], x[1][1]))
    else:
        print('no devices attached')
