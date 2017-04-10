#!/usr/bin/env python

import ao
import mad
import readline
import socket
import struct
import sys
import time
import threading
import Queue
kill = threading.Event()

"""
Allows use to MAD audio library for streaming
"""
class mywrapper(object):
    def __init__(self):
        self.data = ""

    # When it asks to read a specific size, give it that many bytes, and
    # update our remaining data.
    def read(self, size):
        result = self.data[:size]
        self.data = self.data[size:]
        return result

"""
Displays list of available songs
"""
def show_list(sock, header):
    size = int(header[5:len(header)-4])
    data = recv_data(sock,size)
    lists = data[5:].split("/")
    lists = filter(None, lists)
    print "\n"
    for line in lists:
        combined = line.split(",")
        print "({0}) {1}".format(combined[0], combined[1])

"""
Displays current info
"""
def show_info(sock, header):
    size = int(header[5:len(header)-4])+5
    data = ""
    while True:
        data += sock.recv(size)
        if len(data) >= size:
            break
    print "\n-- Info --\n\n{0}\n".format(data[5:size])

"""
Asks for music and then plays it
"""
def play_func(wrap, cond_connected):
    audio_device = ao.AudioDevice('pulse')
    stream = True
    mf = mad.MadFile(wrap)
    time.sleep(.5)
    cond_connected.acquire()
    cond_connected.wait()
    cond_connected.release()
    while not kill.is_set():
        buf = mf.read()
        if buf is not None:
            # Tell the audio device to play our decoded audio.
            audio_device.play(buf, len(buf))
        else:
            sys.exit(0)
    sys.exit(0)

"""
Helper function for receiving data from server
"""
def recv_data(sock, size):
    length = 0
    new_data = ""
    while True:
        received = sock.recv(size-length)
        length+= len(received)
        new_data += received
        if length >= size:
            return new_data

"""
Fetch data from server
"""
def recv_func(wrap, sock, cond_connected):
    while True:
        data = recv_data(sock, 13)
        if data[:4] == "list":
            print "here"
            show_list(sock, data)
        elif data[:4] == "info":
            show_info(sock, data)
        elif data[:5] == "play ":
            length = 0
            new_data = ""
            received = recv_data(sock,5000)
            cond_connected.acquire()

            wrap.data+=received
            cond_connected.notify()

            cond_connected.release()
        elif data[:5] == "play-":
            data = data[6:]
            data = data[:data.find(" ")]
            received = recv_data(sock,int(data))
            cond_connected.acquire()
            wrap.data+=received
            print len(received)
            cond_connected.notify()
            cond_connected.release()

def stop(wrap):
    kill.set()
    kill.clear()
    wrap.data =""
    time.sleep(1)

def main():
    global playing
    if len(sys.argv) < 3:
        print 'Usage: %s <server name/ip> <server port>' % sys.argv[0]
        sys.exit(1)

    wrap = mywrapper()
    cond_connected = threading.Condition()
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    recv_thread = threading.Thread(target=recv_func, args=(wrap,sock, cond_connected))

    recv_thread.daemon = True

    sock.connect((sys.argv[1], int(sys.argv[2])))

    data = recv_data(sock,9)
    if "connect success" in data:
        pass
    else:
        print "error conntecting"
        sys.exit(0)
    recv_thread.start()

    print "To begin, please enter a command\nPlay # - Info # - List - Stop\n"
    while True:
        line = raw_input('>> ')
        line = line.lower()
        if line in ['quit', 'q', 'exit']:
            sys.exit(0)
        elif line == "list":
            sock.sendall("list")
            time.sleep(.01)
        elif "info" in line:
            sock.sendall(line)
            time.sleep(.01)
        elif "play" in line:
            if playing:
                stop(wrap)
            playing = True
            play_thread = threading.Thread(target=play_func, args=(wrap, cond_connected))
            play_thread.daemon = True
            play_thread.start()

            sock.sendall(line)
        elif "stop" in line:
            stop(wrap)

if __name__ == '__main__':
    playing = False
    main()
