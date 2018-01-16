import midi_proc_jack

th=midi_proc_jack.MIDIProcThread()

def transpose(mev):
    mev.set_pitch(mev.get_pitch()-7)
    return [mev]

def ornament(mev):
    mev.set_pitch(mev.get_pitch()-24)
    r=[mev]
    pat=[2,2,3,2,3]
    curp=0
    curt=0
    for i in range(len(pat)):
        m=mev.copy()
        curp+=pat[i]
        sc = 8/(i+1)
        if sc <= 0: sc = 1
        curt+=48000/sc
        m.tme_mon+=curt
        m.set_velocity(max(m.get_velocity()-(sc*5),0))
        m.set_pitch(m.get_pitch()+curp)
        r.append(m)
    return r

midi_proc_jack.set_midi_cb(0,'NoteOn',ornament)
midi_proc_jack.set_midi_cb(0,'NoteOff',ornament)

th.run()
