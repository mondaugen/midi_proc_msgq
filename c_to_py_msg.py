import sysv_ipc as ipc
import struct
import time
import signal
import sys
buflen=17
key_out=789
q_out = ipc.MessageQueue(key_out,ipc.IPC_CREAT)
msgstruct=struct.Struct("IIQ")
msg=bytearray(buflen+msgstruct.size)
msgcont=bytes('abc','utf8')
msg[:len(msgcont)]=msgcont
count=0
running=True
print("size: %d\n"%(msgstruct.size+buflen,))
def doquit(n,f):
    running=False
    sys.exit(0)
signal.signal(signal.SIGINT,doquit)
while True:
    msg[-msgstruct.size:]=msgstruct.pack(len(msgcont),200+count,300+count)
    if (running):
        count += 1
        print(msg)
        q_out.send(bytes(msg),block=True,type=1)
        time.sleep(2)
    else:
        break
