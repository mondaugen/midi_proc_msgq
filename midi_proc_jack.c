/* midi_proc_jack.c

   Receive midi events using JACK library.
   Send midi events to a different process via message queue.
   Receive events from a different process via a message queue.
   Send these events using JACK library.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>
#include "heap.h"

#ifdef __MINGW32__
#include <pthread.h>
#endif

#ifndef WIN32
#include <signal.h>
#include <pthread.h>
#include <sys/mman.h>
#endif

#ifndef MAX
#define MAX(a,b) ( (a) < (b) ? (b) : (a) )
#endif

#define MSGQ_MIDIMSG_TYPE 1
#define MIDI_HW_IF_CHAN_MAX 16
#define MIDI_HW_IF_PITCH_MAX 128
#define MIDIMSGBUFSIZE 16

static int debug = 0;

static jack_port_t* port;
static jack_port_t* output_port;
static jack_ringbuffer_t *rb = NULL;
/* mutex for incoming midi events / outgoing messages */
static pthread_mutex_t msg_thread_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t data_ready = PTHREAD_COND_INITIALIZER;
/* mutex for outgoing midi events / incoming messages */
static pthread_mutex_t heap_lock = PTHREAD_MUTEX_INITIALIZER;

static int keeprunning = 1;
static uint64_t monotonic_cnt = 0;
Heap *outevheap;
static int passthrough = 0;

#define RBSIZE 512

typedef struct {
    /* MIDI message data */
	char  buffer[MIDIMSGBUFSIZE];
    /* number of data in message */
	uint32_t size;
    /* time since application started, also used by scheduler to determine when events should be output i.e., the heap sorts by this value so that the event that should happen soonest is always at the top. */
	uint64_t tme_mon;
} midimsg;

static key_t in_key = 123;
static key_t out_key = 124;

static int msgqid_out;
static int msgqid_in;

struct __attribute__ ((__packed__)) msgq_midimsg {
    long mtype;
    midimsg msg;
};
typedef struct msgq_midimsg msgq_midimsg;

static void
push_to_output_msgq (midimsg* event)
{
    msgq_midimsg tosend;
    tosend.mtype = 1;
    tosend.msg = *event;
    if (msgsnd(msgqid_out,&tosend,sizeof(tosend),0) == -1) {
        perror("msgsnd");
    }
}

/* Functions for allocating / deallocating midimsgs for use in realtime context */

typedef struct {
    /* Mask indicating which items are available. There are up to sizeof(unsigned int)*8 items. */
    unsigned int cache_mask;
    /* Address of beginning of cache */
    char *cache_begin;
    /* Size of 1 item of cache as power of 2 */
    size_t cache_item_size;
} midimsg_cache;

static midimsg_cache out_midimsg_cache;

midimsg *
midimsg_cache_alloc(void)
{
    if (out_midimsg_cache.cache_mask == 0) {
        return NULL;
    }
    unsigned int cache_idx = 0;
    while (((1 << cache_idx) & out_midimsg_cache.cache_mask) == 0) { cache_idx++; }
    //= __builtin_clz(out_midimsg_cache.cache_mask); // This doesn't work, why?
    out_midimsg_cache.cache_mask &= ~(1 << cache_idx);
    return (midimsg*)(out_midimsg_cache.cache_begin +
        (cache_idx << out_midimsg_cache.cache_item_size));
}

void
midimsg_cache_free(midimsg *msg)
{
    unsigned int cache_idx = 
        ((char*)msg - out_midimsg_cache.cache_begin) >> out_midimsg_cache.cache_item_size;
    out_midimsg_cache.cache_mask |= 1 << cache_idx;
}

/* Will have failed if out_midimsg_cache.cache_begin == NULL after calling */
void
midimsg_cache_init(void)
{
    memset(&out_midimsg_cache,0,sizeof(midimsg_cache));
    while ((1 << out_midimsg_cache.cache_item_size) < sizeof(midimsg)) {
        out_midimsg_cache.cache_item_size++;
    }
    out_midimsg_cache.cache_begin =
        malloc((1 << out_midimsg_cache.cache_item_size)*sizeof(unsigned int)*8);
    memset(&out_midimsg_cache.cache_mask,0xff,sizeof(unsigned int));
}

typedef enum {
    /* Filter out repeated note ons */
    midi_filter_flag_NOTEONS = 0x1,
    /* Filter out repeated note offs */
    midi_filter_flag_NOTEOFFS = (0x1 < 1u),
} midi_filter_flag_t;

typedef struct {
    uint32_t          counts[MIDI_HW_IF_CHAN_MAX][MIDI_HW_IF_PITCH_MAX];
    midi_filter_flag_t flags;
} midi_ev_filter_t;

void
midi_ev_filter_init(midi_ev_filter_t *ef, midi_filter_flag_t flags)
{
    memset(ef, 0, sizeof(midi_ev_filter_t));
    ef->flags |= flags;
}

/* Event type */
typedef enum {
    midi_ev_type_NOTEON = 0x90,
    midi_ev_type_NOTEOFF = 0x80,
} midi_ev_type_t;

/* Bytes must at least be of length 3 */
int
midi_ev_filter_should_play(midi_ev_filter_t *ef,
                                 char *bytes)
{
    int chan = bytes[0]&0x0f, pitch = bytes[1], vel = bytes[2];
    switch (bytes[0]&0xf0) {
        case midi_ev_type_NOTEON:
            if (vel > 0) {
                if ((chan >= MIDI_HW_IF_CHAN_MAX) ||
                    (pitch >= MIDI_HW_IF_PITCH_MAX) ||
                    ef->counts[chan][pitch] == UINT32_MAX) {
                    return 0;
                }
                ef->counts[chan][pitch] += 1;
                if (ef->flags & midi_filter_flag_NOTEONS) {
                    return ef->counts[chan][pitch] == 1 ? 1
                        : 0;
                }
                return 1;
            }
            /* Otherwise interpreted as note off */
        case midi_ev_type_NOTEOFF:
            if ((chan >= MIDI_HW_IF_CHAN_MAX) ||
                (pitch >= MIDI_HW_IF_PITCH_MAX) ||
                ef->counts[chan][pitch] == 0) {
                return 0;
            }
            ef->counts[chan][pitch] -= 1;
            if (ef->flags & midi_filter_flag_NOTEOFFS) {
                return ef->counts[chan][pitch] == 0 ? 1
                    : 0;
            }
        default: return 1;
    }
}

midi_ev_filter_t midi_ev_filt;

int
process (jack_nframes_t frames, void* arg)
{
    /* The count at the beginning of the frame, we need this to calculate the
       offsets into the frame of the outgoing MIDI messages. */
    uint64_t monotonic_cnt_beg_frame = monotonic_cnt;
    jack_nframes_t _frames;
	void *buffer, *midioutbuf;
	jack_nframes_t N;
	jack_nframes_t i;

	buffer = jack_port_get_buffer (port, frames);
    midioutbuf = jack_port_get_buffer(output_port, frames);
	jack_midi_clear_buffer(midioutbuf);
	assert (buffer);

    /* We assume events returned sorted by their order in time, which seems
       to be true if you check out the midi_dump.c example. */
	N = jack_midi_get_event_count (buffer);

    if (passthrough) {
        if (debug) { fprintf(stderr,"passing through\n"); }
        for (i = 0; i < N; ++i) {
            jack_midi_event_t event;
            int r;

            r = jack_midi_event_get (&event, buffer, i);

            unsigned char *midimsgbuf = 
                jack_midi_event_reserve(midioutbuf, event.time, 
                        event.size);
            if (r == 0 && midimsgbuf) {
                memcpy(midimsgbuf,event.buffer,event.size);
            } else {
                if (debug) { fprintf(stderr,"midi_proc_jack: MIDI msg dropped\n"); }
            }
        }
        return 0;
    }
    _frames = frames;
	for (i = 0; i < N; ++i) {
		jack_midi_event_t event;
		int r;

		r = jack_midi_event_get (&event, buffer, i);

		if (r == 0 && jack_ringbuffer_write_space (rb) >= sizeof(midimsg)) {
			midimsg m;
			m.tme_mon = monotonic_cnt;
			m.size    = event.size;
			memcpy (m.buffer, event.buffer, MAX(sizeof(m.buffer), event.size));
			jack_ringbuffer_write (rb, (void *) &m, sizeof(midimsg));
            monotonic_cnt += event.time;
            _frames = event.time > _frames ? 0 : _frames - event.time;

		}
	}

	monotonic_cnt += _frames;

	if (pthread_mutex_trylock (&msg_thread_lock) == 0) {
		pthread_cond_signal (&data_ready);
		pthread_mutex_unlock (&msg_thread_lock);
	}

    if (pthread_mutex_trylock (&heap_lock) == 0) {
        midimsg *soonestmsg = NULL;
        if (debug) { fprintf(stderr,"Heap size: %zu\n",Heap_size(outevheap)); }
        if (debug) { fprintf(stderr,"current time, frame start: %lu frame end: %lu\n",monotonic_cnt_beg_frame,monotonic_cnt); }
        while ((Heap_top(outevheap,(void**)&soonestmsg) == HEAP_ENONE)
                && (soonestmsg != NULL) 
                && (soonestmsg->tme_mon < monotonic_cnt)) {
            if (debug) { fprintf(stderr,"message address: %p\n",(void*)soonestmsg); }
            unsigned char *midimsgbuf = NULL;
            jack_nframes_t currel;
            /* message can maybe get sent, it is time, first filter repeated note ons and note offs. */
            int shouldplay = 0;
            if ((shouldplay = midi_ev_filter_should_play(&midi_ev_filt,soonestmsg->buffer))) {
                currel = soonestmsg->tme_mon >= monotonic_cnt_beg_frame ?
                    soonestmsg->tme_mon - monotonic_cnt_beg_frame : 0;
                midimsgbuf = 
                    jack_midi_event_reserve(midioutbuf, currel, 
                            soonestmsg->size);
            }
            if (midimsgbuf) {
                if (debug) { fprintf(stderr,"play MIDI msg, time: %lu status: %#x ",soonestmsg->tme_mon,soonestmsg->buffer[0]);
                    int i;
                    for (i = 1; i < soonestmsg->size; i++) { fprintf(stderr,"%d ",soonestmsg->buffer[i]); }
                    fprintf(stderr,"\n");
                }
                memcpy(midimsgbuf,soonestmsg->buffer,soonestmsg->size);
            } else {
                if (shouldplay) {
                    if (debug) { fprintf(stderr,"Returned NULL when requesting MIDI event of size %u at time %u, MIDI msg not sent\n",
                            soonestmsg->size,currel); }
                }
            }
            Heap_pop(outevheap,(void**)&soonestmsg);
            midimsg_cache_free(soonestmsg);
        }
        if (soonestmsg) {
            if (debug) { fprintf(stderr,"Soonest message at time %lu\n",soonestmsg->tme_mon); }
        }
        pthread_mutex_unlock(&heap_lock);
    }

	return 0;
}

/* Thread that manages sending messages via Message Queues to the other application */

typedef struct {
    pthread_mutex_t *msg_thread_lock;
    int *keeprunning;
    jack_ringbuffer_t *rb;
    pthread_cond_t *data_ready;
} outthread_data;

static void *
output_thread(void *aux)
{
    outthread_data *thread_data = aux;
    pthread_mutex_lock (thread_data->msg_thread_lock);

    if (debug) { fprintf(stderr,"output thread running\n"); }
    while (*thread_data->keeprunning) {
        const int mqlen = jack_ringbuffer_read_space (thread_data->rb) / sizeof(midimsg);
        int i;
        for (i=0; i < mqlen; ++i) {
            midimsg m;
            jack_ringbuffer_read(thread_data->rb, (char*) &m, sizeof(midimsg));

            push_to_output_msgq(&m);
            //fprintf(stderr,"message sent\n");
        }
        fflush (stdout);
        pthread_cond_wait (thread_data->data_ready, thread_data->msg_thread_lock);
    }
    pthread_mutex_unlock (thread_data->msg_thread_lock);
    if (debug) { fprintf(stderr,"output thread stopping\n"); }
    return thread_data;
}

/* Thread that manages receiving messages from other application */

typedef struct {
    int *keeprunning;
    int msgqid_in;
    pthread_mutex_t *heap_lock;
    Heap *outevheap;
} inthread_data;

static void *
input_thread(void *aux)
{
    inthread_data *thread_data = aux;
    msgq_midimsg just_recvd;
    if (debug) { fprintf(stderr,"input thread running\n"); }
    while (*thread_data->keeprunning) {
        /* msgrcv waits for messages on Message Queue */
        if (msgrcv(thread_data->msgqid_in, &just_recvd,
                    sizeof(msgq_midimsg), MSGQ_MIDIMSG_TYPE, 0) < 0) {
            perror("msgrcv");
            *thread_data->keeprunning = 0;
            break;
        }
        //fprintf(stderr,"message received\n");
        /* once one is obtained, push to heap */
        pthread_mutex_lock(thread_data->heap_lock);
        midimsg *mmsg_topush = midimsg_cache_alloc();
        if (!mmsg_topush) {
            fprintf(stderr,"cache full, incoming msg dropped\n");
            pthread_mutex_unlock(thread_data->heap_lock);
            continue;
        }
        *mmsg_topush = just_recvd.msg;
        if (Heap_push(thread_data->outevheap,mmsg_topush) 
                != HEAP_ENONE) {
            fprintf(stderr,"heap push failed, incoming msg dropped, heap full?\n");
            midimsg_cache_free(mmsg_topush);
        }
        if (debug) { fprintf(stderr,"Pushed message to heap.\n"); }
        pthread_mutex_unlock(thread_data->heap_lock);
    }
    if (debug) { fprintf(stderr,"input thread stopping\n"); }
    return thread_data;
}

/* Comparison function for sorting the midi message heap */
static int
mmsg_cmp(void *_a, void *_b)
{
    midimsg *a = _a;
    midimsg *b = _b;
    return a->tme_mon >= b->tme_mon;
}

/* heap doesn't need to know the index */
static void
set_idx_ignore(void *a, size_t idx)
{
    return;
}

static void
stopsig(int sn)
{
    keeprunning = 0;
}

/* TODO: Make better exit on error that cleans up. */
int
main (int argc, char* argv[])
{
#ifdef DEBUG
    debug = 1;
#else
    debug = 0;
#endif
	jack_client_t* client;
	char const default_name[] = "midi_proc_jack";
	char const * client_name;
	int r;

	int cn = 1;

	if (argc > 1) {
		if (!strcmp (argv[1], "-p")) {
            passthrough = 1;
            cn = 2; 
        }
    }

	if (argc > cn) {
		client_name = argv[cn];
	} else {
		client_name = default_name;
	}

	client = jack_client_open (client_name, JackNullOption, NULL);
	if (client == NULL) {
		fprintf (stderr, "Could not create JACK client.\n");
		exit (EXIT_FAILURE);
	}

	rb = jack_ringbuffer_create (RBSIZE * sizeof(midimsg));

	jack_set_process_callback (client, process, 0);

	port = jack_port_register (client, "input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	output_port = jack_port_register (client, "out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	if (port == NULL) {
		fprintf (stderr, "Could not register port.\n");
		exit (EXIT_FAILURE);
	}

    msgqid_out = msgget(out_key,0666|IPC_CREAT);
    if (msgqid_out == -1) {
        perror("msgget out");
    }

    msgqid_in = msgget(in_key,0666|IPC_CREAT);
    if (msgqid_in == -1) {
        perror("msgget in");
    }

    outevheap = Heap_new(sizeof(unsigned int)*8,
            mmsg_cmp,
            set_idx_ignore);

    if (!outevheap) {
        fprintf(stderr, "Could not allocate heap\n");
        exit (EXIT_FAILURE);
    }

    midimsg_cache_init();

    if (!out_midimsg_cache.cache_begin) {
        fprintf(stderr, "Could not allocate midimsg cache\n");
        exit(EXIT_FAILURE);
    }

    outthread_data ot_data = {
        .msg_thread_lock = &msg_thread_lock,
        .keeprunning = &keeprunning,
        .rb = rb,
        .data_ready = &data_ready,
    };

    inthread_data it_data = {
        .keeprunning = &keeprunning,
        .msgqid_in = msgqid_in,
        .heap_lock = &heap_lock,
        .outevheap = outevheap,
    };

    /* I would reckon only filtering note offs is the most common configuration */
    midi_ev_filter_init(&midi_ev_filt,midi_filter_flag_NOTEOFFS);

	r = jack_activate (client);
	if (r != 0) {
		fprintf (stderr, "Could not activate client.\n");
		exit (EXIT_FAILURE);
	}

    pthread_t midi_to_msgq;
    pthread_t msgq_to_midi;
    
    signal(SIGINT,stopsig);

    if (pthread_create(&midi_to_msgq,NULL,output_thread,&ot_data)) {
        perror("pthread create midi_to_msgq");
        exit (EXIT_FAILURE);
    }

    if (pthread_create(&msgq_to_midi,NULL,input_thread,&it_data)) {
        perror("pthread create msgq_to_midi");
        exit (EXIT_FAILURE);
    }
    
    pthread_join(midi_to_msgq,NULL);
    pthread_join(msgq_to_midi,NULL);

	jack_deactivate (client);
	jack_client_close (client);
	jack_ringbuffer_free (rb);

	return 0;
}
