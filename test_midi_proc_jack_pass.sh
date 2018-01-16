jack_midiseq Sequencer 24000 0 60 8000 12000 63 8000&
seqid=$!
jack_midi_dump&
dumpid=$!
python3 ./midi_proc_jack_pass.py&
pyid=$!
./midi_proc_jack&
procid=$!
sleep 1
jack_lsp
jack_connect Sequencer:out midi_proc_jack:input
jack_connect midi_proc_jack:out midi-monitor:input
doquit()
{
    kill -s KILL $procid
    kill -s INT $pyid
    kill -9 $seqid
    kill -9 $dumpid
    ipcrm -Q 123
    ipcrm -Q 124
    exit 0
}
trap doquit INT TERM
while [ 1 ]; do sleep 1; done
