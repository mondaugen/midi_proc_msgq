import sysv_ipc as ipc
import struct
import time

key_out=789
q_out = ipc.MessageQueue(key_out,ipc.IPC_CREAT)
msgstruct=struct.Struct("IIQ")
msg=bytearray(16+msgstruct.size)
msgcont=bytes('abc','utf8')
msg[:len(msgcont)]=msgcont
count=0
while True:
    msg[-msgstruct.size:]=msgstruct.pack(len(msgcont),200+count,300+count)
    q_out.send(msg,block=0,type=1)
    time.sleep(0.5)
