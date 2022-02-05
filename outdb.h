#pragma once
#include "reader.h"
#include "graph.h"

typedef struct
{
    int fd;
    int ptr;
    const char *file;
    char *tmp;
    char buffer[ BUFFER ];
} writer_t;

bool writer_flush( writer_t *w )
{
    int wrote = write( w->fd, w->buffer, w->ptr );
    if ( wrote < 0 )
        sys_error( "writing %s", w->tmp );
    w->ptr -= wrote;
    memmove( w->buffer, w->buffer + wrote, w->ptr );
    return w->ptr > 0;
}

int writer_print( writer_t *w, const char *fmt, ... )
{
    va_list ap;

    while ( true )
    {
        va_start( ap, fmt );
        int need = vsnprintf( w->buffer + w->ptr, BUFFER - w->ptr, fmt, ap );
        va_end( ap );

        if ( need < 0 )
            sys_error( "vsnprintf" );

        if ( need >= BUFFER - w->ptr )
            writer_flush( w );
        else
            return w->ptr += need, need;
    }
}

void writer_append( writer_t *w, span_t span )
{
    if ( span_len( span ) >= BUFFER )
    {
        while ( writer_flush( w ) );
        int bytes = write( w->fd, span.str, span_len( span ) );
        if ( bytes < 0 )
            sys_error( "writing %s", w->tmp );
        span.str += bytes;
    }
    else
    {
        while ( span_len( span ) > BUFFER - w->ptr )
            writer_flush( w );

        span_copy( w->buffer + w->ptr, span );
        w->ptr += span_len( span );
    }
}

void writer_open( writer_t *w, const char *path )
{
    if ( asprintf( &w->tmp, "%s.%d", path, getpid() ) < 0 )
        sys_error( "asprintf" );

    w->file = path;
    w->ptr = 0;
    w->fd = open( w->tmp, O_CREAT | O_WRONLY | O_TRUNC | O_EXCL, 0666 );

    if ( w->fd < 0 )
        sys_error( "creating %s", w->tmp );
}

void writer_close( writer_t *w )
{
    while ( writer_flush( w ) );

    if ( rename( w->tmp, w->file ) == -1 )
        sys_error( "renaming %s to %s", w->tmp, w->file );
    close( w->fd );
    free( w->tmp );
}

void write_stamps( cb_tree *nodes, const char *path )
{
    writer_t w;
    writer_open( &w, path );

    for ( cb_iterator i = cb_begin( nodes ); !cb_end( &i ); cb_next( &i ) )
    {
        node_t *n = cb_get( &i );
        span_t name = span_lit( n-> name );

        if ( n->type != out_node )
            continue;

        writer_print( &w, "%llx ", n->stamp );
        writer_append( &w, name );
        writer_append( &w, span_lit( "\n" ) );
    }

    writer_close( &w );
}

void load_stamps( cb_tree *nodes, const char *file )
{
    reader_t r;

    if ( !reader_init( &r, file ) )
    {
        if ( errno == ENOENT )
            return;
        else
            sys_error( "opening %s", file );
    }

    while ( fetch_line( &r ) )
    {
        span_t path = r.span;
        span_t stamp = fetch_word( &path );
        char *endptr;

        errno = 0;
        uint64_t value = strtoll( stamp.str, &endptr, 16 );
        assert( !errno );
        assert( endptr == stamp.end );

        node_t *node = graph_get( nodes, path );
        if ( !node )
            node = graph_add( nodes, path );

        node->stamp = value;
    }
}
