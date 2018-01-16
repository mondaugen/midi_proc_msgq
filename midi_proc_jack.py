import sysv_ipc as ipc
import time
import struct
import array
import signal
import sys
import threading

BUFLEN=16
MSGTYPE=1

head_struct=struct.Struct("l")
tail_struct=struct.Struct("IQ")

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

def get_midi_event_name(code):
    try:
        return midi_event_table[code]
    except KeyError:
        raise KeyError('MIDI code %x not supported' % (code,))

midi_code_table = {
        'NoteOff': 0x80,
        'NoteOn': 0x90,
        'PolyKeyPress': 0xa0,
        'ControlChange': 0xb0,
        'ProgramChange': 0xc0,
        'ChanPress': 0xd0,
        'PitchBend': 0xe0
}

def get_midi_event_code(name):
    try:
        return midi_code_table[name]
    except KeyError:
        raise KeyError('MIDI name %s not supported' % (name,))

# Size includes status byte
midi_event_size_table = {
        'NoteOff': 3,
        'NoteOn': 3,
        'PolyKeyPress': 3,
        'ControlChange': 3,
        'ProgramChange': 2,
        'ChanPress': 2,
        'PitchBend': 3
        # Others not yet supported
}

def get_midi_event_size(name):
    try:
        return midi_event_size_table[name]
    except KeyError:
        raise KeyError('MIDI event %s not supported' % (name,))


# Callback table for specific events on specific channels
def _midi_cb_default(mev):
    return [mev]

midi_cbs_chan = {
        'NoteOff': _midi_cb_default,
        'NoteOn': _midi_cb_default,
        'PolyKeyPress': _midi_cb_default,
        'ControlChange': _midi_cb_default,
        'ProgramChange': _midi_cb_default,
        'ChanPress': _midi_cb_default,
        'PitchBend': _midi_cb_default
        # Others not yet supported
}
midi_cbs=[]
for i in range(16):
    midi_cbs.append(midi_cbs_chan.copy())
def set_midi_cb(chan,name,cb):
    """
    chan: channel on which to set the callback
    name: event for which to set the callback
    cb: the callback
        should accept a MIDIEvent and return a list of MIDIEvents based on the
        MIDIEvent. This list can be empty, in that case the MIDI event is
        swallowed by the application.
    """
    midi_cbs[chan][name]=cb
def reset_midi_cb(chan,name):
    midi_cbs[chan][name]=_midi_cb_default

def print_midi_from_bytes(b):
    sz=len(b)
    if sz >= 2:
        chan=int(b[0])&0xf
        kind=midi_event_table[int(b[0])&0xf0]
#        try:
#            kind=midi_event_table[int(b[0])&0xf0]
#        except IndexError:
#            kind='Unknown'
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
    msg+=bytes(BUFLEN-size)
    tail=tail_struct.pack(size,tme_rel,tme_mon)
    msg+=tail
    # TODO: May need padding in order to work with C...
    return msg


def proc_midi(msg):
    #... use structure depending on length, parsing different message components
    # each event can have callback, processing event
    buf=msg[:BUFLEN]
    msg=msg[BUFLEN:]
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

class MIDIEvent:
    def __init__(self,tme_mon,typ,chan,dat):
        """
        tme_mon: absolute time in samples
        typ: typ of MIDI message
        chan: MIDI channel
        dat: bytearray of values, if too short for typ, pads with 0s, if too
             short, array truncated. Status byte written properly with code and
             channel.
        """
        self.size=get_midi_event_size(typ)
        self.tme_mon=tme_mon
        self.typ=typ
        self.chan=chan
        self.dat = bytearray(self.size)
        if len(dat) < self.size:
            self.dat[:len(dat)]=dat
        elif len(dat) > self.size:
            self.dat[::1] = dat[:self.size]
        else:
            self.dat[::1] = dat
        self.dat[0] = get_midi_event_code(typ) | chan
    def copy(self):
        return MIDIEvent(self.tme_mon,self.typ,self.chan,self.dat)
    def print(self):
        print("time: %d type: %s chan: %d"%(self.tme_mon,self.typ,self.chan))
        print('\tdata: ',end='')
        for i in range(self.size):
            if (i == 0): print("%#x " % (self.dat[i],),end='')
            else: print("%d " % (self.dat[i],),end='')
        print()
    def to_bytes(self):
        ret=bytearray(BUFLEN+tail_struct.size)
        ret[:self.size]=self.dat
        ret[-tail_struct.size:]=tail_struct.pack(self.size,int(self.tme_mon))
        return ret
    def assert_pitched_type(self):
        if (self.typ not in ['NoteOn','NoteOff','PolyKeyPress']):
            raise TypeError("Doesn't represent pitched type")
    def assert_velocity_type(self):
        if (self.typ not in ['NoteOn','NoteOff','PolyKeyPress','ChanPress']):
            raise TypeError("Doesn't represent velocity type")
    def assert_program_change_type(self):
        if self.typ != 'ProgramChange':
            raise TypeError("Doesn't represent program change type")
    def assert_control_change_type(self):
        if self.typ != 'ControlChange':
            raise TypeError("Doesn't represent control change type")
    def assert_pitch_bend_type(self):
        if self.typ != 'PitchBend':
            raise TypeError("Doesn't represent pitch bend type")
    def get_pitch(self):
        self.assert_pitched_type()
        return self.dat[1]
    def set_pitch(self,p):
        p=int(p)
        self.assert_pitched_type()
        self.dat[1]=p&0x7f
    def get_velocity(self):
        self.assert_velocity_type()
        if (self.typ in ['NoteOn','NoteOff','PolyKeyPress']):
            return self.dat[2]
        else:
            return self.dat[1]
    def set_velocity(self,v):
        v=int(v)
        self.assert_velocity_type()
        if (self.typ in ['NoteOn','NoteOff','PolyKeyPress']):
            self.dat[2]=v&0x7f
        else:
            self.dat[1]=v&0x7f
    def get_program(self):
        self.assert_program_change_type()
        return self.dat[1]
    def set_program(self,p):
        p=int(p)
        self.assert_program_change_type()
        self.dat[1]=p&0x7f
    def get_cc_num(self):
        self.assert_control_change_type()
        return self.dat[1]
    def set_cc_num(self,n):
        n=int(n)
        self.assert_control_change_type()
        self.dat[1]=n&0x7f
    def get_cc_val(self):
        self.assert_control_change_type()
        return self.dat[2]
    def set_cc_val(self,v):
        v=int(v)
        self.assert_control_change_type()
        self.dat[2]=v&0x7f
    def get_pitch_bend(self):
        self.assert_pitch_bend_type()
        return (self.dat[2] << 7) + self.dat[1]
    def set_pitch_bend(self,b):
        b=int(b)
        self.assert_pitch_bend_type()
        self.dat[1] = b&0x7f
        self.dat[2] = (b>>7)&0x7f

    def from_bytes(b):
        """
        b: bytes representing c struct packing the data
        """
        buf=b[:BUFLEN]
        b=b[BUFLEN:]
        size,tme_mon=tail_struct.unpack(b[:tail_struct.size])
        if (size < 1):
            raise ValueError('bytes must contain valid MIDI message')
        return MIDIEvent(tme_mon,
            get_midi_event_name(buf[0]&0xf0),buf[0]&0x0f,buf)

def _midi_cbs_proc(mev):
    """
    Accepts a MIDI event and routes it to the applicable callback.
    If the callback has not yet been set, returns a list only containing the mev, otherwise
    the callback should return a list of MIDI events based on the mev.
    """
    return midi_cbs[mev.chan][mev.typ](mev)

class MIDIProcThread(threading.Thread):
    def __init__(self,key_in=124,key_out=123): 
        threading.Thread.__init__(self)
        self.running=0
        self.key_in=key_in
        self.q_in = ipc.MessageQueue(self.key_in,ipc.IPC_CREAT)
        self.key_out=key_out
        self.q_out = ipc.MessageQueue(self.key_out,ipc.IPC_CREAT)
    def run(self):
        self.running=1
        while self.running:
            while self.q_in.current_messages > 0:
                msg=self.q_in.receive(type=MSGTYPE)[0]
                mev=MIDIEvent.from_bytes(msg)
                mev.print()
                mevs=_midi_cbs_proc(mev)
                print("mevs length: %d" % (len(mevs),))
                for m in mevs:
                    m.print()
                    self.q_out.send(m.to_bytes(),type=MSGTYPE)
            time.sleep(0.001)
        return
