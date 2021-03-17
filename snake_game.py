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

    def send_bytes(self, mssg):
        self.socket.send(bytearray(mssg))

    def close(self):
        self.socket.close()

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("this <server_addr> <server_port> <user_name>")
        exit(0)

    comserv = ComServ(sys.argv[1], sys.argv[2], sys.argv[3])

    input('init?')
    comserv.send_bytes([0x01]) #init
    input('start?')
    comserv.send_bytes([0x02]) #start

    while True:
        a = input("key : ")
        if a == 'w':
            comserv.send_bytes([0xB1]) #start
        elif a == 's':
            comserv.send_bytes([0xB2]) #start
        elif a == 'd':
            comserv.send_bytes([0xB3]) #start
        elif a == 'a':
            comserv.send_bytes([0xB4]) #start



    sleep(0.01)
    comserv.send_bytes([0xB3]) #start
    sleep(0.01)
    comserv.send_bytes([0xB3]) #start
    sleep(0.01)
    comserv.send_bytes([0xB3]) #start
    sleep(5)
    comserv.send_bytes([0xB3]) #start
    sleep(2)
    comserv.send_bytes([0xB2]) #start
    sleep(5)



    comserv.close()
