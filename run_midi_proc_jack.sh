jack_midiseq Sequencer 24000 0 60 8000 12000 63 8000&
seqid=$!
./midi_proc_jack&
procid=$!
jack_midi_dump&
dumpid=$!
python3 run_midi_proc_jack.py&
pyid=$!
fluidsynth -i -s -a jack -m jack /usr/share/sounds/sf2/FluidR3_GM.sf2&
fluidid=$!
sleep 1
jack_connect Sequencer:out midi_proc_jack:input
#jack_connect midi_proc_jack:out midi-monitor:input
jack_connect midi_proc_jack:out fluidsynth:midi
jack_connect fluidsynth:l_00 system:playback_1
jack_connect fluidsynth:r_00 system:playback_2
echo "seqid $seqid"
echo "procid $procid"
echo "dumpid $dumpid"
echo "pyid $pyid"
jack_lsp
doquit()
{ 
    kill -s KILL $seqid
    kill -s KILL $procid
    kill -s KILL $dumpid
    kill -s KILL $pyid
    kill -s KILL $fluidid
    exit 0
}
trap doquit INT TERM
while [ 1 ];
do
    sleep 1
done
