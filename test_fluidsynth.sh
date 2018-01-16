jack_midiseq Sequencer 24000 0 60 8000 12000 63 8000&
seqid=$!
fluidsynth -i -s -a jack -m jack /usr/share/sounds/sf2/FluidR3_GM.sf2&
fluidid=$!
sleep 2

jack_connect Sequencer:out fluidsynth:midi
jack_connect fluidsynth:l_00 system:playback_1
jack_connect fluidsynth:r_00 system:playback_2

doquit()
{
    kill -9 $fluidid
    kill -9 $seqid
    exit 0
}
trap doquit INT TERM
while [ 1 ]; do sleep 1; done
