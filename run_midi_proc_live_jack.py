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
    if (ccn >= 107 and ccn <= 124):
        pch=pitch_tab[ccn]
        if (ccv > 0):
            m=midi_proc_jack.MIDIEvent(mev.tme_mon,'NoteOn',mev.chan,bytes([0,pch,ccv]))
        else:
            m=midi_proc_jack.MIDIEvent(mev.tme_mon,'NoteOff',mev.chan,bytes([0,pch,ccv]))
    else:
        return [mev]
    return [m]

midi_proc_jack.set_midi_cb(0,'ControlChange',makenote)

th.run()

