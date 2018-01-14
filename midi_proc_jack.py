import sysv_ipc as ipc
import time
import struct
import array
import signal
import sys

head_struct=struct.Struct("l")
tail_struct=struct.Struct("IIQ")
midi2_struct=struct.Struct("cc")
midi3_struct=struct.Struct("ccc")

out_fmt="""
buf: %s
size: %u
tme_rel: %u
tme_mon: %ull
"""

done = False

def setdone(x,y):
    print("Got signal")
    done = True
    sys.exit(0)

signal.signal(signal.SIGINT,setdone)

midi_event_table = {
        0x80: 'NoteOff',
        0x90: 'NoteOn',
        0xa0: 'PolyKeyPress',
        0xb0: 'ControlChange',
        0xc0: 'ProgramChange',
        0xd0: 'ChanPress',
        0xe0: 'PitchBend'
}

# Callback table for specific events on specific channels
midi_cbs_chan = {
        0x80: None,
        0x90: None,
        0xa0: None,
        0xb0: None,
        0xc0: None,
        0xd0: None,
        0xe0: None
}
midi_cbs=[]
for i in xrange(16):
    midi_cbs.append(midi_cbs_chan.copy())


def print_midi_from_bytes(b):
    sz=len(b)
    if sz >= 2:
        chan=int(b[0])&0xf
        kind=midi_event_table[int(b[0])&0xf0]
#        try:
#            kind=midi_event_table[int(b[0])&0xf0]
#        except IndexError:
     kind='Unknown'
        dat1=int(b[1])
        print("Chan: %d, Kind: %s, Dat1: %u" % (chan,kind,dat1))
    if sz >= 3:
        dat2=int(b[2])
        print("Dat2: %u" % (dat2,))
    print("\n")

def msg_from_midi_dat(tme_rel,tme_mon,kind,chan,dat1,dat2=None):
    midi_stat=kind|chan
    if (dat2==None):
        msg=midi2_struct.pack(midi_stat,dat1)
    else:
        msg=midi3_struct.pack(midi_stat,dat1,dat2)
    size=len(msg)
    msg+=bytes(128-size)
    tail=tail_struct.pack(size,tme_rel,tme_mon)
    msg+=tail
    # TODO: May need padding in order to work with C...
    return msg


def proc_midi(msg):
    #... use structure depending on length, parsing different message components
    # each event can have callback, processing event
    buf=msg[:128]
    msg=msg[128:]
    size,tme_rel,tme_mon=tail_struct.unpack(msg[:16])
    print(out_fmt % (buf[:size],size,tme_rel,tme_mon))
    if (size <= 1):
        return # we don't process such events yet
    chan=int(b[0])&0xf
    kind=int(b[0])&0xf0
    if (size == 2):
        _,dat1=midi2_struct.unpack(buf[:size])
        dat1=midi_cbs[chan][kind](dat1)
    if (size == 3):
        _,dat1,dat2=midi3_struct.unpack(buf[:size])
        dat1,dat2=midi_cbs[chan][kind](dat1,dat2)

key_in=123
q_in = ipc.MessageQueue(key,ipc.IPC_CREAT)
key_out=124
q_out = ipc.MessageQueue(key,ipc.IPC_CREAT)
while not done:
    while q.current_messages > 0:
        msg=q.receive()[0]
        #mtype=head_struct.unpack(msg)
        #msg=msg[4:]
        buf=msg[:128]
        msg=msg[128:]
        print("msg len: %d",len(msg))
        size,tme_rel,tme_mon=tail_struct.unpack(msg[:16])
        print(out_fmt % (buf[:size],size,tme_rel,tme_mon))
        print_midi_from_bytes(buf[:size])

    time.sleep(0.1)

sys.exit(0)
