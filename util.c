/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "util.h"

void
die(const char *fmt, ...)
{
	va_list ap;
	int saved_errno;

	saved_errno = errno;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':')
		fprintf(stderr, " %s", strerror(saved_errno));
	fputc('\n', stderr);

	exit(1);
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if (!(p = calloc(nmemb, size)))
		die("calloc:");
	return p;
}

// Derived from picom ( config.c::xdg_config_home() )
char *get_xdg_config_home( void ) {
        char *xdg_config_home = getenv( "XDG_CONFIG_HOME" );
        char *user_home = getenv( "HOME" );

        if ( !xdg_config_home ) {
                const char *default_config_directory = "/.config";
                if ( !user_home ) return NULL;
                xdg_config_home = mstrjoin( user_home, default_config_directory );
        } else {
                xdg_config_home = strdup( xdg_config_home );
        }

        return xdg_config_home;
}

// Derived from picom ( config.c::xdg_config_home() )
char *get_xdg_data_home( void ) {
        char *xdg_data_home = getenv( "XDG_DATA_HOME" );
        char *user_home = getenv( "HOME" );

        if ( !xdg_data_home ) {
                const char *default_data_directory = "/.local/share";
                if ( !user_home ) return NULL;
                xdg_data_home = mstrjoin( user_home, default_data_directory );
        } else {
                xdg_data_home = strdup( xdg_data_home );
        }

        return xdg_data_home;
}

int make_parent_directory( const char *path ) {
        char *normal;
        char *walk;
        size_t normallen;

        normalize_path( path, &normal );
        normallen = strlen( normal );
        walk = normal;

        while ( walk < normal + normallen + 1 ) {
                // Get length from walk to next /
                size_t n = strcspn( walk, "/" );

                // Skip path /
                if ( n == 0 ) {
                        walk++;
                        continue;
                }

                // Length of current path segment
                size_t curpathlen = walk - normal + n;
                char curpath[ curpathlen + 1 ];
                struct stat s;

                // Copy path segment to stat
                strncpy( curpath, normal, curpathlen );
                strcpy( curpath + curpathlen, "" );
                int res = stat( curpath, &s );

                if ( res < 0 ) {
                        if ( errno == ENOENT ) {
                                log_debug( "Making directory %s", curpath );
                                if ( mkdir( curpath, 0700 ) < 0 ) {
                                        log_error( "Failed to make directory %s", curpath );
                                        perror( "" );
                                        free( normal );
                                        return -1;
                                }
                        } else {
                                log_error( "Error stat-ing directory %s", curpath );
                                perror( "" );
                                free( normal );
                                return -1;
                        }
                }

                // Continue to next path segment
                walk += n;
        }

        free( normal );

        return 0;
}

#ifndef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

// gcc warns about legitimate truncation worries in strncpy in mstrjoin.
// strncpy( joined_string, string_1, length_1 ) intentionally truncates the null byte
// from string_1, however. strncpy( joined_string + length_1, string_2, length_2 )
// uses bounds depending on the source argument, but joined_string is allocated with
// length_1 + length_2 + 1, so this strncpy can't overflow.
//
// Allocate the space and join two strings. - Derived from picom ( str.c::mstrjoin() )
char *mstrjoin( const char *string_1, const char *string_2 ) {
        const size_t length_1 = strlen( string_1 );
        const size_t length_2 = strlen( string_2 );
        const size_t total_length = length_1 + length_2 + 1;
        char *joined_string = ecalloc( total_length, sizeof( char ) );
        strncpy( joined_string, string_1, length_1 );
        strncpy( joined_string + length_1, string_2, length_2 );
        joined_string[ total_length - 1 ] = '\0';
        return joined_string;
}

// Concatenate a string on heap with another string. - Derived from picom ( str.c::mstrextend() )
void mstrextend( char **source_string_pointer, const char *addition ) {
        if ( !*source_string_pointer ) {
                *source_string_pointer = strdup( addition );
                return;
        }
        const size_t length_1 = strlen( *source_string_pointer );
        const size_t length_2 = strlen( addition );
        const size_t total_length = length_1 + length_2 + 1;
        *source_string_pointer = realloc( *source_string_pointer, total_length );
        strncpy( *source_string_pointer + length_1, addition, length_2 );
        ( *source_string_pointer )[ total_length - 1 ] = '\0';
}

#ifndef __clang__
#pragma GCC diagnostic pop
#endif

/**
 * @param path
 * @param normal
 * @return
 */
int normalize_path( const char *path, char **normal ) {
        size_t len = strlen( path );
        *normal = (char *) malloc( ( len + 1 ) * sizeof( char ) );
        const char *walk = path;
        const char *match;
        size_t newlen = 0;

        while ( ( match = strchr( walk, '/' ) ) ) {
                // Copy everything between match and walk
                strncpy( *normal + newlen, walk, match - walk );
                newlen += match - walk;
                walk += match - walk;

                // Skip all repeating slashes
                while ( *walk == '/' ) walk++;

                // If not last character in path
                if ( walk != path + len ) ( *normal )[ newlen++ ] = '/';
        }

        ( *normal )[ newlen++ ] = '\0';

        // Copy remaining path
        strcat( *normal, walk );
        newlen += strlen( walk );

        *normal = (char *) realloc( *normal, newlen * sizeof( char ) );

        return 0;
}
