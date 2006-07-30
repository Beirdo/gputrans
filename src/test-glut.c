#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <GL/glut.h>

int main( int argc, char **argv );
void do_parent( pid_t child );
void do_child( int childNum );

int         idShm, idSem, idMsg;

void do_parent( pid_t child )
{
    printf( "In parent - child = %d\n", child );
}

void do_child( int childNum )
{
    char       *shmBlock;
    int         argc = 3;
    char       *arg0 = "client";
    char       *arg1 = "-display";
    char        display[256];
    char       *argv[] = { arg0, arg1, display };

    sprintf( display, ":0.%d", childNum );
    printf( "In child #%d: %s %s\n", childNum, argv[1], argv[2] );

    shmBlock = (char *)shmat( idShm, NULL, 0 );
    printf( "Message = %s\n", shmBlock );

    while( 1 ) {
        sleep(1);
    }
}

int main( int argc, char **argv )
{
    pid_t       child;
    char       *shmBlock;
    int         i;

    printf( "Starting, %d\n", argc );

    idShm = shmget( IPC_PRIVATE, 1024, IPC_CREAT | 0600 );
    shmBlock = (char *)shmat( idShm, NULL, 0 );

    strcpy( shmBlock, "Hello there!" );

    child = -1;
    for( i = 0; i < 5 && child; i++ ) {
        child = fork();
        if( !child ) {
            do_child( i );
        } else {
            do_parent( child );
        }
    }

    if( child ) {
        while( 1 ) {
            sleep(1);
        }
    }
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

