import numpy as np
import curses
import socket
import sys
from time import sleep

#  class World:
#      """
#       - world size 1000x1000
#      """
#      def __init__(self):
#          self.world = np.zeros((1000, 1000))
#          self.snakes_pos = []

#      def new_snake(self, user_id, )

#  class Snake:
#      """
#       - head is always center of screen
#       - one block per sec
#      """
#      def __init__(self):

class ComServ:
    """

    """
    def __init__(self, saddr, sport, user_name):
        self.saddr = saddr
        self.sport = sport
        self.socket = socket.socket()
        self.socket.connect((saddr, int(sport)))

    def send_key(self, mssg):
        self.socket.send(mssg.encode())

    def close(self):
        self.socket.close()

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("this <server_addr> <server_port> <user_name>")
        exit(0)

    comserv = ComServ(sys.argv[1], sys.argv[2], sys.argv[3])

    comserv.send_key("test")

    comserv.close()
