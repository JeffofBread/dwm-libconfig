/**
 * @file parser.h
 * @brief Configuration parser public interface and data
 *
 * Contains the public data and interface to interact with the runtime
 * configuration parser. For more information, please check @ref parser.c,
 * or go to the patch's GitHub page: https://github.com/JeffofBread/dwm-libconfig.
 *
 * @author JeffOfBread <jeffofbreadcoding@gmail.com>
 *
 * @see @ref parser.c
 * @see https://github.com/JeffofBread/dwm-libconfig
 *
 * @warning This file must be included after the definition of the structs
 * @ref Arg, @ref Button, @ref Key, and @ref Rule.
 *
 * @todo Finish documentation
 */

#ifndef PARSER_H_
#define PARSER_H_

#include <ctype.h>
#include <libconfig.h>
#include <stdbool.h>

// Uncomment to enable log printing for debugging. This is just
// a crude compatability macro between my own logging system,
// which I didn't want to bring over just for the config parser.
#define log_trace( ... ) //_log( "TRACE", __VA_ARGS__ )
#define log_debug( ... ) _log( "DEBUG", __VA_ARGS__ )
#define log_info( ... ) _log( "INFO", __VA_ARGS__ )
#define log_warn( ... ) _log( "WARN", __VA_ARGS__ )
#define log_error( ... ) _log( "ERROR", __VA_ARGS__ )
#define log_fatal( ... ) _log( "FATAL", __VA_ARGS__ )

#define _TOSTRING( X ) #X
#define TOSTRING( X ) _TOSTRING( X )
#define _log( LEVEL, ... ) fprintf( stdout, LEVEL ": [" __FILE__ "::" TOSTRING(__LINE__) "]: "  __VA_ARGS__ )

// Simple wrappers for free/fclose to improve null safety. It is not flawless or a catch-all however
#define SAFE_FREE( p ) do { if ( p ) { free( ( void * ) ( p ) ); ( p ) = NULL; } } while ( 0 )
#define SAFE_FCLOSE( f ) do { if ( f ) { fclose( f ); ( f ) = NULL; } } while ( 0 )

// Define some repetitive clamping functions using a macro.
// This equally could be a macro like this:
//      #define CLAMP(A, MIN, MAX) ((A) < (MIN) ? (MIN) : ((A) > (MAX) ? (MAX) : (A)))
// But that clamps the range silently. No print or notification a value was out of range.
#define DEFINE_CLAMP_FUNCTION( NAME, TYPE, FORMAT )                                                     \
        static inline TYPE clamp_range_##NAME( TYPE input, TYPE min, TYPE max ) {                       \
                if ( input < min ) {                                                                    \
                        log_warn( "Clamped \"" FORMAT "\" to a min of \"" FORMAT "\"\n", input, min );  \
                        return min;                                                                     \
                } else if ( input > max ) {                                                             \
                        log_warn( "Clamped \"" FORMAT "\" to a max of \"" FORMAT "\"\n", input, max );  \
                        return max;                                                                     \
                }                                                                                       \
                return input;                                                                           \
        }

DEFINE_CLAMP_FUNCTION( int, int, "%d" )
DEFINE_CLAMP_FUNCTION( uint, unsigned int, "%d" )
DEFINE_CLAMP_FUNCTION( long, long, "%ld" )
DEFINE_CLAMP_FUNCTION( ulong, unsigned long, "%ld" )
DEFINE_CLAMP_FUNCTION( float, float, "%f" )

// Some macros to make the structure a little easer smaller
#define BUTTONBINDS dwm_config.buttonbind_array
#define KEYBINDS dwm_config.keybind_array

typedef struct Configuration {
        bool fallback_config_loaded;
        bool default_keybinds_loaded;
        bool default_buttonbinds_loaded;
        bool default_rules_loaded;
        unsigned int max_keys;
        unsigned int rule_array_size;
        unsigned int buttonbind_array_size;
        unsigned int keybind_array_size;
        char *config_filepath;
        Rule *rule_array;
        Key *keybind_array;
        Button *buttonbind_array;
        config_t libconfig_config;
} Configuration;

// Struct to hold some parser internal data and
// some of the configuration data that can't
// be written to variables in `config.(def.).h`
extern Configuration dwm_config;

extern void config_cleanup( Configuration *config );
extern int parse_config( Configuration *config );
extern void setlayout_floating( const Arg *arg );
extern void setlayout_monocle( const Arg *arg );
extern void setlayout_tiled( const Arg *arg );
extern void spawn_simple( const Arg *arg );

/**
 * @brief Simple wrapper around @ref strdup() to provide error logging.
 *
 * TODO
 *
 * @param[in] string String to be copied using @ref strdup().
 *
 * @return Pointer of the string dynamically allocated using @ref strdup()
 * or NULL if @ref strdup() failed.
 *
 * @note String is dynamically allocated using @ref strdup(), and will need
 * to be manually freed.
 */
static inline char *estrdup( const char *string ) {
        if ( !string ) return NULL;
        char *return_string = strdup( string );
        if ( !return_string ) {
                log_error( "strdup failed: %s\n", strerror(errno) );
        }
        return return_string;
}

/**
 * @brief Look up a boolean value in a libconfig configuration.
 *
 * This function searches the libconfig configuration @p config for a boolean
 * value at the location @p path using libconfig's config_lookup_bool().
 * If the lookup succeeds, the result is stored in @p parsed_value and 0 is
 * returned. If the lookup fails (value not found or wrong type), a warning
 * is logged, @p parsed_value is left unchanged, and -1 is returned.
 *
 * @param[in] config Pointer to the libconfig configuration.
 * @param[in] path Path expression to search within @p config.
 * @param[in] optional If the value being looked up is optional.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return 0 on success, -1 on failure.
 *
 * @see config_lookup_bool() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005flookup_005fbool)
 */
static inline int libconfig_lookup_bool( const config_t *config, const char *path, const bool optional, bool *parsed_value ) {
        if ( !config ) {
                log_error( "libconfig configuration context is NULL, cannot perform lookup of \"%s\"\n", path );
                return -1;
        }

        int tmp = 0;
        if ( config_lookup_bool( config, path, &tmp ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }

                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }

        *parsed_value = tmp;
        return 0;
}

/**
 * @brief Look up a boolean value in a libconfig setting.
 *
 * This function searches the libconfig setting context @p setting for a boolean
 * value at the location @p path using libconfig's config_setting_lookup_bool().
 * If the lookup succeeds, the result is stored in @p parsed_value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p parsed_value is left unchanged, and -1 is returned.
 *
 * @param[in] setting Pointer to the libconfig setting.
 * @param[in] path Path expression to search within @p setting.
 * @param[in] optional If the value being looked up is optional.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return 0 on success, -1 on failure.
 *
 * @see config_setting_lookup_bool() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005fsetting_005flookup_005fbool)
 */
static inline int libconfig_setting_lookup_bool( const config_setting_t *setting, const char *path, const bool optional, bool *parsed_value ) {
        if ( !setting ) {
                log_error( "libconfig setting context is NULL, cannot perform lookup of \"%s\"\n", path );
                return -1;
        }

        int tmp = 0;
        if ( config_setting_lookup_bool( setting, path, &tmp ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }

                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }

        *parsed_value = tmp;
        return 0;
}

/**
 * @brief Look up an integer value in a libconfig configuration.
 *
 * This function searches the libconfig configuration @p config for an integer
 * value at the location @p path using libconfig's config_lookup_int().
 * If the lookup succeeds, the result is clamped to a minimum of @p range_min
 * and maximum of @p range_max, then stored in @p parsed_value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p parsed_value is left unchanged, and -1 is returned.
 *
 * @param[in] config Pointer to the libconfig config.
 * @param[in] path Path expression to search within @p config.
 * @param[in] optional If the value being looked up is optional.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return 0 on success, -1 on failure.
 *
 * @see config_lookup_int() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005fsetting_005flookup_005fint)
 */
static inline int libconfig_lookup_int( const config_t *config, const char *path, const bool optional, const int range_min, const int range_max, int *parsed_value ) {
        if ( !config ) {
                log_error( "libconfig configuration context is NULL, cannot perform lookup of \"%s\"\n", path );
                return -1;
        }

        if ( config_lookup_int( config, path, parsed_value ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }

                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }

        *parsed_value = clamp_range_int( *parsed_value, range_min, range_max );
        return 0;
}

/**
 * @brief Look up an integer value in a libconfig setting.
 *
 * This function searches the libconfig setting context @p setting for an integer
 * value at the location @p path using libconfig's config_setting_lookup_int().
 * If the lookup succeeds, the result is clamped to a minimum of @p range_min
 * and maximum of @p range_max, then stored in @p parsed_value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p parsed_value is left unchanged, and -1 is returned.
 *
 * @param[in] setting Pointer to the libconfig setting.
 * @param[in] path Path expression to search within @p setting.
 * @param[in] optional If the value being looked up is optional.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return 0 on success, -1 on failure.
 *
 * @see config_setting_lookup_int() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005flookup_005fint)
 */
static inline int libconfig_setting_lookup_int( const config_setting_t *setting, const char *path, const bool optional, const int range_min, const int range_max, int *parsed_value ) {
        if ( !setting ) {
                log_error( "libconfig setting context is NULL, cannot perform lookup of \"%s\"\n", path );
                return -1;
        }

        if ( config_setting_lookup_int( setting, path, parsed_value ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }

                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }

        *parsed_value = clamp_range_int( *parsed_value, range_min, range_max );
        return 0;
}

/**
 * @brief Look up an unsigned integer value in a libconfig configuration.
 *
 * This function searches the libconfig configuration @p config for an unsigned
 * integer value at the location @p path using libconfig's config_lookup_int().
 * If the lookup succeeds, the result is clamped to a minimum of @p range_min
 * and maximum of @p range_max, then stored in @p parsed_value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p parsed_value is left unchanged, and -1 is returned.
 *
 * @param[in] config Pointer to the libconfig config.
 * @param[in] path Path expression to search within @p config.
 * @param[in] optional If the value being looked up is optional.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return 0 on success, -1 on failure.
 *
 * @see config_lookup_int() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005fsetting_005flookup_005fint)
 */
static inline int libconfig_lookup_uint( const config_t *config, const char *path, const bool optional, const unsigned int range_min, const unsigned int range_max, unsigned int *parsed_value ) {
        if ( !config ) {
                log_error( "libconfig configuration context is NULL, cannot perform lookup of \"%s\"\n", path );
                return -1;
        }

        int tmp = 0;
        if ( config_lookup_int( config, path, &tmp ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }

                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }

        *parsed_value = clamp_range_uint( tmp, range_min, range_max );
        return 0;
}

/**
 * @brief Look up an unsigned integer value in a libconfig setting.
 *
 * This function searches the libconfig setting context @p setting for an unsigned
 * integer value at the location @p path using libconfig's config_setting_lookup_int().
 * If the lookup succeeds, the result is clamped to a minimum of @p range_min
 * and maximum of @p range_max, then stored in @p parsed_value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p parsed_value is left unchanged, and -1 is returned.
 *
 * @param[in] setting Pointer to the libconfig setting.
 * @param[in] path Path expression to search within @p setting.
 * @param[in] optional If the value being looked up is optional.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return 0 on success, -1 on failure.
 *
 * @see config_setting_lookup_int() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005flookup_005fint)
 */
static inline int libconfig_setting_lookup_uint( const config_setting_t *setting, const char *path, const bool optional, const unsigned int range_min, const unsigned int range_max, unsigned int *parsed_value ) {
        if ( !setting ) {
                log_error( "libconfig setting context is NULL, cannot perform lookup of \"%s\"\n", path );
                return -1;
        }

        int tmp = 0;
        if ( config_setting_lookup_int( setting, path, &tmp ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }

                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }

        *parsed_value = clamp_range_uint( tmp, range_min, range_max );
        return 0;
}

/**
 * @brief Look up a float value in a libconfig configuration.
 *
 * This function searches the libconfig configuration @p config for a floating
 * point value at the location @p path using libconfig's config_lookup_float().
 * If the lookup succeeds, the result is clamped to a minimum of @p range_min
 * and maximum of @p range_max, then stored in @p parsed_value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p parsed_value is left unchanged, and -1 is returned.
 *
 * @param[in] config Pointer to the libconfig config.
 * @param[in] path Path expression to search within @p config.
 * @param[in] optional If the value being looked up is optional.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return 0 on success, -1 on failure.
 *
 * @see config_lookup_float() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005fsetting_005flookup_005ffloat)
 */
static inline int libconfig_lookup_float( const config_t *config, const char *path, const bool optional, const float range_min, const float range_max, float *parsed_value ) {
        if ( !config ) {
                log_error( "libconfig configuration context is NULL, cannot perform lookup of \"%s\"\n", path );
                return -1;
        }

        double tmp = 0;
        if ( config_lookup_float( config, path, &tmp ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }

                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }

        *parsed_value = clamp_range_float( tmp, range_min, range_max );
        return 0;
}

/**
 * @brief Look up a string value in a libconfig configuration.
 *
 * This function searches the libconfig configuration @p config for a string
 * value at the location @p path using libconfig's config_lookup_string().
 * If the lookup succeeds, the result is stored in @p parsed_value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p parsed_value is left unchanged, and -1 is returned.
 *
 * @param[in] config Pointer to the libconfig configuration.
 * @param[in] path Path expression to search within @p config.
 * @param[in] optional If the value being looked up is optional.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return 0 on success, -1 on failure.
 *
 * @see config_lookup_string() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005flookup_005fstring)
 */
static inline int libconfig_lookup_string( const config_t *config, const char *path, const bool optional, const char **parsed_value ) {
        if ( !config ) {
                log_error( "libconfig configuration context is NULL, cannot perform lookup of \"%s\"\n", path );
                return -1;
        }

        if ( config_lookup_string( config, path, parsed_value ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }

                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }

        return 0;
}

/**
 * @brief Look up a string value in a libconfig setting.
 *
 * This function searches the libconfig setting context @p setting for a string
 * value at the location @p path using libconfig's config_setting_lookup_string().
 * If the lookup succeeds, the result is stored in @p parsed_value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p parsed_value is left unchanged, and -1 is returned.
 *
 * @param[in] setting Pointer to the libconfig setting.
 * @param[in] path Path expression to search within @p setting.
 * @param[in] optional If the value being looked up is optional.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return 0 on success, -1 on failure.
 *
 * @see config_setting_lookup_string() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005fsetting_005flookup_005fstring)
 */
static inline int libconfig_setting_lookup_string( const config_setting_t *setting, const char *path, const bool optional, const char **parsed_value ) {
        if ( !setting ) {
                log_error( "libconfig setting context is NULL, cannot perform lookup of \"%s\"\n", path );
                return -1;
        }

        if ( config_setting_lookup_string( setting, path, parsed_value ) == CONFIG_FALSE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }

                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }

        return 0;
}

/**
 * @brief Removes the whitespace before and after @p input_string.
 *
 * This function trims the whitespace before and after @p input_string.
 * The trim is performed in place on @p input_string, and assumes it is
 * both mutable and null-terminated.
 *
 * @param[in,out] input_string Null-terminated, mutable string to be trimmed.
 *
 * @return On success, a pointer to the first non-space character in
 * the string is returned. If @p input_string was entirely whitespace, an
 * empty string is returned. If @p input_string was NULL, NULL is returned.
 */
static inline char *trim_whitespace( char *input_string ) {
        if ( !input_string ) return NULL;
        while ( isspace( (unsigned char) *input_string ) ) input_string++;
        if ( *input_string == '\0' ) return input_string;
        char *end = input_string + strlen( input_string ) - 1;
        while ( end > input_string && isspace( (unsigned char) *end ) ) end--;
        *( end + 1 ) = '\0';
        return input_string;
}

#endif /* PARSER_H_ */
