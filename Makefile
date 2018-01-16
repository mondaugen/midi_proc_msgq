CFLAGS=-Wall -g
LDLIBS=-ljack -pthread

midi_proc_jack : heap.o midi_proc_jack.o fastcache.o

c_to_py_msg : c_to_py_msg.o
