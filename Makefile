CFLAGS=-Wall
LDLIBS=-ljack -pthread
midi_proc_jack : heap.o midi_proc_jack.o
