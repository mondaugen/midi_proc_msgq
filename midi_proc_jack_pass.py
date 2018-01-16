import sysv_ipc as ipc
import sys
import signal
import time

MSGTYPE=1
key_in=124
key_out=123
q_in = ipc.MessageQueue(key_in,ipc.IPC_CREAT)
q_out = ipc.MessageQueue(key_out,ipc.IPC_CREAT)

def doquit(x,y):
    sys.exit(0)

signal.signal(signal.SIGINT,doquit)

while True:
    while q_in.current_messages > 0:
        msg=q_in.receive(type=MSGTYPE)[0]
#        print("passing through: %s\n" %(str(msg),))
        q_out.send(msg,type=MSGTYPE)
    time.sleep(0.001)
