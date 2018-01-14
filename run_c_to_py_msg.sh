./c_to_py_msg&
cproc=$!
python3 c_to_py_msg.py&
pyproc=$!
running=1
_quit()
{
    kill -s INT $cproc
    kill -s INT $pyproc
    ipcrm -Q 789
    running=0
    exit 0
}
trap _quit INT TERM
while [ $running ]
do
    sleep 1
done
echo "Exiting..."
exit 0
