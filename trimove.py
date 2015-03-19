# same as command
# move trinamic motor
#cansend can1  001#04.01.00.00.0f.ff.e7.00

# copy can.conf to home directory
# run can-test once to setup the interface
# this is for the socket-can stack only, not RT-CAN

# then run this

from __future__ import print_function
import logging
import time
import struct
logging.basicConfig(level=logging.INFO)

scode =  { 100: "no error",
           101: "loaded into EEPROM",
           1  : "checksum error",
           2  : "invalid command",
           3  : "wrong type",
           4  : "invalid value",
           5  : "EEPROM locked",
           6  : "command not available",
           }
import can

def main():
    bus = can.interface.Bus()

    # MVP - move to pos, relative
    msg = can.Message(arbitration_id=0x1,
                      data=[4,1,0,0, 0x0f,0xff,0xe7,0],
                      extended_id=False)
    bus.send(msg)
    print("tx:", msg)
    m = bus.recv(timeout=1)
    if m is not None:
        (address, rc, cmd, value) = struct.unpack_from("BBBl", str(m.data))
        print("rx:", can.Message(data=m.data),scode[rc])
    else:
        print("no reply")

    time.sleep(3)

    # MST - stop motor
    msg = can.Message(arbitration_id=0x1,
                      data=[3,0,0,0,0,0,0,0],
                      extended_id=False)
    bus.send(msg)
    print("tx:", msg)
    m = bus.recv(timeout=1)
    if m is not None:
        (address, rc, cmd, value) = struct.unpack_from("BBBl", str(m.data))
        print("rx:", can.Message(data=m.data),scode[rc])

if __name__ == "__main__":
    main()
