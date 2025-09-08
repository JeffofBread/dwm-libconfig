/* See LICENSE file for copyright and license details. */

#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define BETWEEN(X, A, B)        ((A) <= (X) && (X) <= (B))
#define LENGTH(X)               (sizeof (X) / sizeof (X)[0])

// Uncomment to enable log printing for debugging. This is just
// a crude compatability macro between my own logging system,
// which I didn't want to bring over just for the config parser.
#define log_print( ... ) fprintf( stdout, __VA_ARGS__ );

#define SAFE_FREE( p )     do { if ( p ) { free( ( void * ) ( p ) ); ( p ) = NULL; } } while ( 0 )
#define SAFE_FCLOSE( f )   do { if ( f ) { fclose( f ); ( f ) = NULL; } } while ( 0 )

void die(const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);
char *get_xdg_config_home( void );
char *get_xdg_data_home( void );
int make_parent_directory( const char *path );
char *mstrjoin( const char *string_1, const char *string_2 );
void mstrextend( char **source_string_pointer, const char *addition );
int normalize_path( const char *path, char **normal );

static inline int normalize_range_int( const int i, const int min, const int max ) {
        if ( i > max ) {
                log_print( "WARN: Value %d above max of %d, value clamped to %d\n", i, max, max );
                return max;
        }
        if ( i < min ) {
                log_print( "WARN: Value %d under min of %d, value clamped to %d\n", i, min, min );
                return min;
        }
        return i;
}

static inline unsigned int normalize_range_uint( const unsigned int i, const unsigned int min, const unsigned int max ) {
        if ( i > max ) {
                log_print( "WARN: Value %u above max of %u, value clamped to %u\n", i, max, max );
                return max;
        }
        if ( i < min ) {
                log_print( "WARN: Value %u under min of %u, value clamped to %u\n", i, min, min );
                return min;
        }
        return i;
}

static inline long normalize_range_long( const long i, const long min, const long max ) {
        if ( i > max ) {
                log_print( "WARN: Value %ld above max of %ld, value clamped to %ld\n", i, max, max );
                return max;
        }
        if ( i < min ) {
                log_print( "WARN: Value %ld under min of %ld, value clamped to %ld\n", i, min, min );
                return min;
        }
        return i;
}

static inline unsigned long normalize_range_ulong( const unsigned long i, const unsigned long min, const unsigned long max ) {
        if ( i > max ) {
                log_print( "WARN: Value %lu above max of %lu, value clamped to %lu\n", i, max, max );
                return max;
        }
        if ( i < min ) {
                log_print( "WARN: Value %lu under min of %lu, value clamped to %lu\n", i, min, min );
                return min;
        }
        return i;
}

static inline float normalize_range_float( const float i, const float min, const float max ) {
        if ( i > max ) {
                log_print( "WARN: Value %f above max of %f, value clamped to %f\n", i, max, max );
                return max;
        }
        if ( i < min ) {
                log_print( "WARN: Value %f under min of %f, value clamped to %f\n", i, min, min );
                return min;
        }
        return i;
}

