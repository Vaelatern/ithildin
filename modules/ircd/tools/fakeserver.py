#!/usr/bin/env python

"""
create a fake IRC server using some pretty simple techniques.  basically
meant as a way to allow easy pretend input.
"""

import getopt, os, select, socket, sys, threading, time

# go ahead and create a miniature 'server' class to make it easy to lock
# down for the threads...
class fake_server(object):
    def __init__(self, name, password, uplink):
        self.name = name
        self.password = password
        self.uplink = uplink

        self.lock = threading.Lock()
        self.iothread = threading.Thread(target = self.io)
        self.iothread.setDaemon(True)
        self.parsethread = threading.Thread(target = self.parser)
        self.parsethread.setDaemon(True)

        self.writeq = list()
        self.readq = list()
        self.readq_cv = threading.Condition()

        self.rbuf = ""
        self.sock = None
    # end of __init__

    def parse(self, line):
        """parse an individual line of input"""

        # output a slightly formatted version of the line
        print "<<< %s" % line

        # split the line.  first find (if any exists) the remaining
        # arguments from the command, they'll be preceded by a : that isn't
        # the first character in the string.
        lastarg = ""
        idx = line.rfind(":")
        if idx > 0:
            lastarg = line[idx + 1:]
            line = line[:idx].rstrip()
        args = line.split(" ")
        if lastarg:
            args.append(lastarg)

        sender = ""
        if args[0][0] == ":":
            sender = args[0][1:]
            args = args[1:]
        
        cmd = ""
        if args:
            cmd = args[0]
            args = args[1:]

        # okay, we've now got a sender, cmd, and arguments so we can decide
        # what to reply to.. right now we don't reply to a whole hell of a
        # lot
        if not cmd: return
        elif cmd.upper() == "PING":
            self.send("PONG %s :%s" % (self.name, args[0]))

    # end of parse

    def send(self, line):
        self.lock.acquire()
        self.writeq.append("%s\r\n" % line)
        self.lock.release()
        print ">>> %s" % line
    # end of send

    def start(self):
        self.iothread.start()
        self.parsethread.start()

        # make with the eating of input
        while True:
            try:
                line = raw_input()
                self.send(line)
            except KeyboardInterrupt:
                sys.exit(0)

    def io(self):
        """main I/O looping facility.  selects on the desciptor to see if
        I/O is ready and handles some very simple connectivity stuff"""

        while True:
            # infloop this stuff..
            if not self.sock:
                self.sock = socket.socket()
                self.sock.setblocking(True)
                self.sock.connect((self.uplink, 6667))
                self.sock.setblocking(False)
                self.send("PROTOCOL dreamforge")
                self.send("PASS %s" % self.password)
                self.send("SERVER %s 1 :test server" % self.name)

            # if the socket isn't connected an error will get thrown.  need
            # to wrap stuff to deal with this
            (rlist, wlist, xlist) = select.select([self.sock], [self.sock],
                                                  [], 0.05)
            # lock now in anticipation of changing stuff...
            if rlist:
                self.readq_cv.acquire()
                self.rbuf += self.sock.recv(1024)
                self.rbuf = self.rbuf.strip("\r")
                idx = 0
                while idx != -1:
                    idx = self.rbuf.find("\n")
                    if idx != -1:
                        self.readq.append(self.rbuf[:idx])
                        self.rbuf = self.rbuf[idx + 1:]
                if self.readq: self.readq_cv.notify()
                self.readq_cv.release()
            if wlist:
                # I have no interest in dealing with write failures.
                self.lock.acquire()
                self.sock.setblocking(True)
                for line in self.writeq:
                    self.sock.send(line)
                self.writeq = list()
                self.sock.setblocking(False)
                self.lock.release()

    # end of io

    def parser(self):
        while True:
            # loop on the work queue..
            self.readq_cv.acquire()
            while not self.readq:
                self.readq_cv.wait()
            # got it.
            for line in self.readq:
                self.parse(line)
            self.readq = list()
            self.readq_cv.release()

    # end of parser

# suck up options to override the above variables
my_name = "test.server"
my_password = "test.pass"
my_uplink = "127.0.0.1"
opts = getopt.getopt(sys.argv[1:], "n:p:u:")[0]
for opt, val in opts:
    if opt == "-n": my_name = val
    elif opt == "-p": my_password = val
    elif opt == "-u": my_uplink = val

server = fake_server(my_name, my_password, my_uplink)
server.start()
