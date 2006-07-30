#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <GL/glut.h>
#include <GL/gl.h>
#include <GL/glext.h>

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
    char        display[256];
    char       *argv[] = { "client", "-display", display };
    GLuint      fb;
    int         maxTexSize;
    const char *glRenderer;

    sprintf( display, ":0.%d", childNum );
    printf( "In child #%d: %s %s\n", childNum, argv[1], argv[2] );

    shmBlock = (char *)shmat( idShm, NULL, 0 );
    printf( "Message = %s\n", shmBlock );

    glutInit( &argc, argv );
    glutCreateWindow( "gputrans" );
    glewInit();

#if 0
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, 0, texSize, texSize );
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glViewport(0, 0, texSize, texSize );
#endif

#if 0
    glGenFramebufferEXT(1, &fb);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
#endif

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    glRenderer = (const char *) glGetString(GL_RENDERER);

    printf( "Child %d - Renderer: %s\n", childNum, glRenderer );
    printf( "Child %d - Max Texture Size: %d\n", childNum, maxTexSize );

    sleep(10);
    shmdt( shmBlock );
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
        sleep(20);
        shmdt( shmBlock );
        shmctl( idShm, IPC_RMID, NULL );
    }
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

