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
static jack_ringbuffer_t *rb = NULL;
/* mutex for incoming midi events / outgoing messages */
static pthread_mutex_t msg_thread_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t data_ready = PTHREAD_COND_INITIALIZER;
/* mutex for outgoing midi events / incoming messages */
static pthread_mutex_t heap_lock = PTHREAD_MUTEX_INITIALIZER;

static int keeprunning = 1;
static uint64_t monotonic_cnt = 0;

#define RBSIZE 512

typedef struct {
/* MIDI message data */
	uint8_t  buffer[128];
    /* number of data in message */
	uint32_t size;
    /* time offset from beginning of process */
	uint32_t tme_rel;
    /* time since application started, also used by scheduler to determine when events should be output i.e., the heap sorts by this value so that the event that should happen soonest is always at the top. */
	uint64_t tme_mon;
} midimsg;

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
        cache_idx * out_midimsg_cache.cache_item_size);
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
    memset(*out_midimsg_cache,0,sizeof(midimsg_cache));
    while ((1 << out_midimsg_cache.cache_item_size) < sizeof(midimsg)) {
        out_midimsg_cache.cache_item_size++;
    }
    out_midimsg_cache.cache_begin =
        malloc((1 << out_midimsg_cache.cache_item_size)*sizeof(unsigned int)*8);
}

int
process (jack_nframes_t frames, void* arg)
{
    /* The count at the beginning of the frame, we need this to calculate the
       offsets into the frame of the outgoing MIDI messages. */
    uint64_t monotonic_cnt_beg_frame = monotonic_cnt;
    jack_nframes_t _frames;
	void* buffer, midioutbuf;
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
        while ((Heap_top(outevheap,&midimsg) == HEAP_ENONE)
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
                Heap_pop(outevheap,&midimsg);
                midimsg_cache_free(midimsg);
            }
        }
        pthread_mutex_unlock(&heap_lock);
    }

	return 0;
}
