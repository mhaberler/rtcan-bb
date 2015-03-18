# same as command

# move trinamic motor
#cansend can1  001#04.01.00.00.0f.ff.e7.00

# copy can.conf to home directory
# run can-test once to setup the interface
# then run this

from __future__ import print_function

import can

def main():
    bus = can.interface.Bus()
    msg = can.Message(arbitration_id=0x1,
                      data=[4,1,0,0, 0x0f, 0xff, 0xe7, 0],
                      extended_id=False)
    bus.send(msg)
    print("Message sent")

if __name__ == "__main__":
    main()
