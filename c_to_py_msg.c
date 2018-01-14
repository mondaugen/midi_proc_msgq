#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>

#define BUFLEN 17

typedef struct {
    long mtype;
    char buf[BUFLEN];
    uint32_t size;
    uint32_t tme_rel;
    uint64_t tme_mon;
} __attribute__((packed)) msgdat_t;

key_t key = 789;
int done = 0;

void
setdone(int sn)
{
    done = 1;
}

int main (void)
{
    int msgqi = msgget(key, 0666 | IPC_CREAT);
    msgdat_t dat;
    signal(SIGINT,setdone);
    printf("size: %zu\n",sizeof(msgdat_t)-sizeof(long));
    while (!done) {
        //msgrcv(msgqi,&dat,sizeof(msgdat_t),1,0);
        if(msgrcv(msgqi,&dat,sizeof(msgdat_t),1,0) < 0) {
            perror("msgrcv");
        }
        printf("msg: %s, size: %u, rel: %u, mon: %llu\n",
                dat.buf,
                dat.size,
                dat.tme_rel,
                dat.tme_mon);
    }
    return 0;
}
        


