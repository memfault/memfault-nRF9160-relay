import socket

LOCAL_IP = "localhost"
LOCAL_PORT = 20001
BUFFER_SIZE = 38

UDPServerSocket = socket.socket(family=socket.AF_INET, type=socket.SOCK_DGRAM)
UDPServerSocket.bind((LOCAL_IP, LOCAL_PORT))

print(f"Listening on {LOCAL_IP}:{LOCAL_PORT}")

while(True):
    message, address = UDPServerSocket.recvfrom(BUFFER_SIZE)
    print("{}: {}".format(address, message))
