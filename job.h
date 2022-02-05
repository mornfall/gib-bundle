#include "common.h"
#include "env.h"
#include <sys/socket.h>
#define MAX_ARGS 128

typedef struct job
{
    node_t *node;
    pid_t pid;
    int pipe_fd;
    cb_tree blocking;
    struct job *next;
    char name[];
} job_t;

void job_fork( job_t *j )
{
    int fds[ 2 ];

    if ( socketpair( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds ) )
        sys_error( "socketpair %s", j->name );

    value_t *cmd = j->node->cmd;
    j->pipe_fd = fds[ 0 ];
    j->pid = fork();

    if ( j->pid == 0 ) /* child */
    {
        char *argv[ MAX_ARGS ];
        value_t *n = cmd;
        int i = 0;

        while ( n )
        {
            argv[ i++ ] = n->data;
            n = n->next;
        }

        dup2( fds[ 1 ], 3 );
        execv( cmd->data, argv );
        sys_error( "execv %s (job %s):", cmd->data, j->name );
    }

    if ( j->pid == -1 )
        sys_error( "fork %s [%s]:", j->name, cmd->data );
}

job_t *job_wanted( cb_tree *jobs, node_t *build, node_t *blocked )
{
    char *name = build->name;
    int name_len = strlen( name );

    job_t *j = cb_find( jobs, name, name_len ).leaf;

    if ( strcmp( j->name, build->name ) )
    {
        j = malloc( sizeof( job_t ) + name_len + 1 );
        strcpy( j->name, name );
        j->node = build;
        j->next = 0;
        build->changed = true;
    }

    cb_insert( &j->blocking, blocked, VSIZE( blocked, name ), -1 );
    blocked->waiting ++;
    return j;
}

bool job_update( job_t *j )
{
    return false;
}