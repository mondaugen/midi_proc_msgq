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
    msg=q_out.receive(block=True,type=1)[0]
    buf=msg[:16]
    size,rel,mon =msgstruct.unpack(msg[-msgstruct.size:])
    print("msg: %s, size: %u, rel: %u, mon: %u\n" % (buf[:size],size,rel,mon))
#    time.sleep(2)

