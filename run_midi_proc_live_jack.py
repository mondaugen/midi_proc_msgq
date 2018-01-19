import midi_proc_jack

th=midi_proc_jack.MIDIProcThread()

pitch_tab = {
        107: 43,
        108: 45,
        109: 47,
        110: 48,
        111: 50,
        112: 52,
        113: 53,
        114: 54,
        115: 55,
        116: 48,
        117: 50,
        118: 52,
        119: 53,
        120: 55,
        121: 57,
        122: 58,
        123: 59,
        124: 60,
        }

def makenote(mev):
    ccn = mev.get_cc_num() 
    ccv = mev.get_cc_val()
    mev.print()
    pchs=[0,4,7,9]
    times=[(i*48000) for i in [0,0.25,0.5,3./4+1./8]]
    m=[]
    if (ccn >= 107 and ccn <= 124):
        pch=pitch_tab[ccn]
        for p,t in zip(pchs,times):
            if (ccv > 0):
                m.append(midi_proc_jack.MIDIEvent(mev.tme_mon+t,'NoteOn',mev.chan,bytes([0,pch+p,ccv])))
            else:
                m.append(midi_proc_jack.MIDIEvent(mev.tme_mon+t,'NoteOff',mev.chan,bytes([0,pch+p,ccv])))
    else:
        return [mev]
    return m

midi_proc_jack.set_midi_cb(0,'ControlChange',makenote)

th.run()

