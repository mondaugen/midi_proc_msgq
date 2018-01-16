hwinputport=$(jack_lsp|grep 'alsa_pcm:in'|grep nanoKONTROL)
./midi_proc_jack&
procid=$!
jack_midi_dump&
dumpid=$!
python3 run_midi_proc_live_jack.py&
pyid=$!
fluidsynth -g 1 -r 48000 -z 1024 -i -s -a jack -m jack /usr/share/sounds/sf2/FluidR3_GM.sf2&
fluidid=$!
sleep 1
jack_connect $hwinputport midi_proc_jack:input
jack_connect midi_proc_jack:out fluidsynth:midi
jack_connect fluidsynth:l_00 system:playback_1
jack_connect fluidsynth:r_00 system:playback_2
echo "procid $procid"
echo "dumpid $dumpid"
echo "pyid $pyid"
jack_lsp
doquit()
{ 
    jack_disconnect $hwinputport midi_proc_jack:input
    kill -s KILL $procid
    kill -s KILL $dumpid
    kill -s KILL $pyid
    kill -s KILL $fluidid
    ipcrm -Q 123
    ipcrm -Q 124
    exit 0
}
trap doquit INT TERM
while [ 1 ];
do
    sleep 1
done

