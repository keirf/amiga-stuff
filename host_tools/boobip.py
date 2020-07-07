#!/usr/bin/env python3
#
# boobip.py
#
# Control script for Chris Morley's BooBip ROM switcher.
# Notes:
#  Reset is via SP: Connect the SP pad to system /RESET line.
#  New adapters should be initialised once only with the 'init' command.
#
# Written & released by Keir Fraser <keir.xen@gmail.com>
#
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import serial, sys, time

def ser_open():
    try:
        ser = serial.Serial('/dev/boobip')
    except serial.SerialException as e:
        print(e)
        sys.exit(1)
    ser.reset_input_buffer()
    return ser

def print_help(argv):
    print('Usage: %s CMD [OPTS]...' % argv[0])
    print('Commands:')
    print(' h         : Print this message')
    print(' r         : Reset the host')
    print(' s 0-3     : Reset and Switch to selected ROM')
    print(' p 0-3 BIN : Reset, Program and Switch to selected ROM')
    print(' init      : Initialise a new BooBip switcher (only needed once)')
    sys.exit(1)

def rst(ser, pre=b'', post=b''):
    ser.write(b'9l' + pre) # SP low
    time.sleep(0.5)
    ser.write(post + b'9z') # SP hi-z
    
def reset(argv):
    if len(argv) != 2:
        print_help(argv)
    rst(ser_open())

def switch(argv):
    if len(argv) != 3:
        print_help(argv)
    bank = int(argv[2])
    ser = ser_open()
    # 'wN': Switch to ROM #N
    rst(ser, post = b'w%d' % bank)
    
def program(argv):
    if len(argv) != 4:
        print_help(argv)
    bank, image = int(argv[2]), argv[3]
    ser = ser_open()
    # 'pN5y': program #N with 512kB data requiring byteswap
    # Do this under reset. No need for extra delay as in rst().
    ser.write(b'9lp%d5y' % bank)
    with open(image, 'rb') as f:
        ser.write(f.read())
    ser.write(b'w%d9z' % bank)
    
def init(argv):
    if len(argv) != 2:
        print_help(argv)
    ser = ser_open()
    # 'o' & '^': Enter & Exit the option menu
    # 'p1m': 27C400, master
    # 'wn':  disable wireless ROM switch
    # 'ln':  disable LED
    # 'c':   commit to Flash
    ser.write(b'op1mwnlnc^')
    rst(ser)
    
def main(argv):
    if len(argv) < 2:
        print_help(argv)
    cmd = argv[1]
    if cmd == 'r':
        reset(argv)
    elif cmd == 's':
        switch(argv)
    elif cmd == 'p':
        program(argv)
    elif cmd == 'init':
        init(argv)
    else:
        print_help(argv)

if __name__ == "__main__":
    main(sys.argv)

# Local variables:
# python-indent: 4
# End:
