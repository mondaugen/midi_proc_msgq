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

#define RBSIZE 512

typedef struct {
/* MIDI message data */
	uint8_t  buffer[16];
    /* number of data in message */
	uint32_t size;
    /* time offset from beginning of process */
	uint32_t tme_rel;
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
    unsigned int cache_idx = __builtin_clz(out_midimsg_cache.cache_mask);
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
    midioutbuf = jack_port_get_buffer(output_port, nframes);
	jack_midi_clear_buffer(midioutbuf);
	assert (buffer);

    /* We assume events returned sorted by their order in time, which seems
       to be true if you check out the midi_dump.c example. */
	N = jack_midi_get_event_count (buffer);
    _frames = frames;
	for (i = 0; i < N; ++i) {
		jack_midi_event_t event;
		int r;

		r = jack_midi_event_get (&event, buffer, i);

		if (r == 0 && jack_ringbuffer_write_space (rb) >= sizeof(midimsg)) {
			midimsg m;
			m.tme_mon = monotonic_cnt;
			m.tme_rel = event.time;
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
        midimsg *soonestmsg;
        while ((Heap_top(outevheap,&soonestmsg) == HEAP_ENONE)
                && (soonestmsg != NULL) 
                && (soonestmsg->tme_mon <= monotonic_cnt)) {
            /* message can be sent, it is time */
            jack_nframes_t currel = 
                soonestmsg->tme_mon - monotonic_cnt_beg_frame;
            unsigned char *midimsgbuf = 
                jack_midi_event_reserve(midioutbuf, currel, 
                        soonestmsg->size);
            if (midimsgbuf) {
                memcpy(midimsgbuf,soonestmsg->buffer,soonestmsg->size);
                Heap_pop(outevheap,&soonestmsg);
                midimsg_cache_free(soonestmsg);
            }
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

    while (*thread_data->keeprunning) {
        const int mqlen = jack_ringbuffer_read_space (thread_data->rb) / sizeof(midimsg);
        int i;
        for (i=0; i < mqlen; ++i) {
            size_t j;
            midimsg m;
            jack_ringbuffer_read(thread_data->rb, (char*) &m, sizeof(midimsg));

            push_to_output_msgq(&m);
        }
        fflush (stdout);
        pthread_cond_wait (thread_data->data_ready, thread_data->msg_thread_lock);
    }
    pthread_mutex_unlock (thread_data->msg_thread_lock);
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
    while (*thread_data->keeprunning) {
        /* msgrcv waits for messages on Message Queue */
        if (msgrcv(inthread_data->msgqid_in, &just_recvd,
                    sizeof(msgq_midimsg), MSGQ_MIDIMSG_TYPE, 0)) {
            perror("msgrcv");
            *thread_data->keeprunning = 0;
            break;
        }
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
        pthread_mutex_unlock(thread_data->heap_lock);
    }
    return thread_data;
}

/* Comparison function for sorting the midi message heap */
static int
mmsg_cmp(void *a, void *b)
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
	jack_client_t* client;
	char const default_name[] = "midi_proc_jack";
	char const * client_name;
	int time_format = 0;
	int r;

	int cn = 1;

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
        .msg_thread_lock = &msg_thread_lock;
        .keeprunning = &keeprunning;
        .rb = rb;
        .data_ready = &data_ready;
    };

    inthread_data it_data = {
        .keeprunning = &keeprunning;
        .msgqid_in = msgqid_in;
        .heap_lock = &heap_lock;
        .outevheap = outevheap;
    };

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
