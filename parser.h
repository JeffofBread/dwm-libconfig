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
 */

#ifndef PARSER_H_
#define PARSER_H_

#include <ctype.h>
#include <libconfig.h>
#include <stdbool.h>

// Uncomment to enable log printing for debugging. This is just
// a crude compatability macro between my own logging system,
// which I didn't want to bring over just for the config parser.
#define log_trace( ... ) //fprintf( stdout, "TRACE: " __VA_ARGS__ );
#define log_debug( ... ) fprintf( stdout, "DEBUG: " __VA_ARGS__ );
#define log_info( ... ) fprintf( stdout, "INFO: " __VA_ARGS__ );
#define log_warn( ... ) fprintf( stdout, "WARN: " __VA_ARGS__ );
#define log_error( ... ) fprintf( stdout, "ERROR: " __VA_ARGS__ );
#define log_fatal( ... ) fprintf( stdout, "FATAL: " __VA_ARGS__ );

// Simple wrappers for free/fclose to improve null safety. It is not flawless or a catch-all however
#define SAFE_FREE( p ) do { if ( p ) { free( ( void * ) ( p ) ); ( p ) = NULL; } } while ( 0 )
#define SAFE_FCLOSE( f ) do { if ( f ) { fclose( f ); ( f ) = NULL; } } while ( 0 )

typedef struct Configuration {
        bool default_binds_loaded;
        unsigned int max_keys;
        unsigned int rule_array_size;
        unsigned int buttonbind_array_size;
        unsigned int keybind_array_size;
        char *config_filepath;
        Rule *rule_array;
        Key *keybind_array;
        Button *buttonbind_array;
        config_t *libconfig_config;
} Configuration;

// Struct to hold some parser internal data and
// some of the configuration data that can't
// be written to variables in `config.(def.).h`
extern Configuration dwm_config;

extern void config_cleanup( Configuration *master_config );
extern int parse_config( Configuration *master_config );
extern void setlayout_floating( const Arg *arg );
extern void setlayout_monocle( const Arg *arg );
extern void setlayout_tiled( const Arg *arg );
extern void spawn_simple( const Arg *arg );

/**
 * @brief Clamp an integer value to an inclusive range.
 *
 * This function takes in a value @p i to be clamped between
 * the inclusive ranges @p min and @p max. If the value is
 * outside this range, the value is clamped and a warning
 * is logged.
 *
 * @param i Value to be clamped.
 * @param min Inclusive range minimum.
 * @param max Inclusive range maximum.
 * @return Clamped value of @p i.
 */
static inline int clamp_range_int( const int i, const int min, const int max ) {
        if ( i > max ) {
                log_warn( "Value %d above max of %d, value clamped to %d\n", i, max, max );
                return max;
        }
        if ( i < min ) {
                log_warn( "Value %d under min of %d, value clamped to %d\n", i, min, min );
                return min;
        }
        return i;
}

/**
 * @brief Clamp an unsigned integer value to an inclusive range.
 *
 * This function takes in a value @p i to be clamped between
 * the inclusive ranges @p min and @p max. If the value is
 * outside this range, the value is clamped and a warning
 * is logged.
 *
 * @param i Value to be clamped.
 * @param min Inclusive range minimum.
 * @param max Inclusive range maximum.
 * @return Clamped value of @p i.
 */
static inline unsigned int clamp_range_uint( const unsigned int i, const unsigned int min, const unsigned int max ) {
        if ( i > max ) {
                log_warn( "Value %u above max of %u, value clamped to %u\n", i, max, max );
                return max;
        }
        if ( i < min ) {
                log_warn( "Value %u under min of %u, value clamped to %u\n", i, min, min );
                return min;
        }
        return i;
}

/**
 * @brief Clamp a long integer value to an inclusive range.
 *
 * This function takes in a value @p i to be clamped between
 * the inclusive ranges @p min and @p max. If the value is
 * outside this range, the value is clamped and a warning
 * is logged.
 *
 * @param i Value to be clamped.
 * @param min Inclusive range minimum.
 * @param max Inclusive range maximum.
 * @return Clamped value of @p i.
 */
static inline long clamp_range_long( const long i, const long min, const long max ) {
        if ( i > max ) {
                log_warn( "Value %ld above max of %ld, value clamped to %ld\n", i, max, max );
                return max;
        }
        if ( i < min ) {
                log_warn( "Value %ld under min of %ld, value clamped to %ld\n", i, min, min );
                return min;
        }
        return i;
}

/**
 * @brief Clamp an unsigned long integer value to an inclusive range.
 *
 * This function takes in a value @p i to be clamped between
 * the inclusive ranges @p min and @p max. If the value is
 * outside this range, the value is clamped and a warning
 * is logged.
 *
 * @param i Value to be clamped.
 * @param min Inclusive range minimum.
 * @param max Inclusive range maximum.
 * @return Clamped value of @p i.
 */
static inline unsigned long clamp_range_ulong( const unsigned long i, const unsigned long min, const unsigned long max ) {
        if ( i > max ) {
                log_warn( "Value %lu above max of %lu, value clamped to %lu\n", i, max, max );
                return max;
        }
        if ( i < min ) {
                log_warn( "Value %lu under min of %lu, value clamped to %lu\n", i, min, min );
                return min;
        }
        return i;
}

/**
 * @brief Clamp a float value to an inclusive range.
 *
 * This function takes in a value @p i to be clamped between
 * the inclusive ranges @p min and @p max. If the value is
 * outside this range, the value is clamped and a warning
 * is logged.
 *
 * @param i Value to be clamped.
 * @param min Inclusive range minimum.
 * @param max Inclusive range maximum.
 * @return Clamped value of @p i.
 */
static inline float clamp_range_float( const float i, const float min, const float max ) {
        if ( i > max ) {
                log_warn( "Value %f above max of %f, value clamped to %f\n", i, max, max );
                return max;
        }
        if ( i < min ) {
                log_warn( "Value %f under min of %f, value clamped to %f\n", i, min, min );
                return min;
        }
        return i;
}

/**
 * @brief Look up a boolean value in a libconfig configuration.
 *
 * This function searches the libconfig configuration @p config for a boolean
 * value at the location @p path using libconfig's config_lookup_bool().
 * If the lookup succeeds, the result is stored in @p value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p value is left unchanged, and -1 is returned.
 *
 * @param[in] config Pointer to the libconfig configuration.
 * @param[in] path Path expression to search within @p config.
 * @param[out] value Pointer to where the parsed value will be stored on success.
 * @param[in] optional If the value being looked up is optional.
 * @return 0 on success, -1 on failure.
 *
 * @see config_lookup_bool() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005flookup_005fbool)
 */
static inline int libconfig_lookup_bool( const config_t *config, const char *path, bool *value, const bool optional ) {
        int tmp = 0;
        if ( config_lookup_bool( config, path, &tmp ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }
                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }
        *value = tmp;
        return 0;
}

/**
 * @brief Look up a boolean value in a libconfig setting.
 *
 * This function searches the libconfig setting context @p setting for a boolean
 * value at the location @p path using libconfig's config_setting_lookup_bool().
 * If the lookup succeeds, the result is stored in @p value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p value is left unchanged, and -1 is returned.
 *
 * @param[in] setting Pointer to the libconfig setting.
 * @param[in] path Path expression to search within @p setting.
 * @param[out] value Pointer to where the parsed value will be stored on success.
 * @param[in] optional If the value being looked up is optional.
 * @return 0 on success, -1 on failure.
 *
 * @see config_setting_lookup_bool() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005fsetting_005flookup_005fbool)
 */
static inline int libconfig_setting_lookup_bool( const config_setting_t *setting, const char *path, bool *value, const bool optional ) {
        int tmp = 0;
        if ( config_setting_lookup_bool( setting, path, &tmp ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }
                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }
        *value = tmp;
        return 0;
}

/**
 * @brief Look up an integer value in a libconfig configuration.
 *
 * This function searches the libconfig configuration @p config for an integer
 * value at the location @p path using libconfig's config_lookup_int().
 * If the lookup succeeds, the result is clamped to a minimum of @p range_min
 * and maximum of @p range_max, then stored in @p value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p value is left unchanged, and -1 is returned.
 *
 * @param[in] config Pointer to the libconfig config.
 * @param[in] path Path expression to search within @p config.
 * @param[out] value Pointer to where the parsed value will be stored on success.
 * @param[in] optional If the value being looked up is optional.
 * @param[in] range_min Minimum value that can be saved to @p value.
 * @param[in] range_max Maximum value that can be saved to @p value.
 * @return 0 on success, -1 on failure.
 *
 * @see config_lookup_int() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005fsetting_005flookup_005fint)
 */
static inline int libconfig_lookup_int( const config_t *config, const char *path, int *value, const bool optional, const int range_min, const int range_max ) {
        if ( config_lookup_int( config, path, value ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }
                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }
        *value = clamp_range_int( *value, range_min, range_max );
        return 0;
}

/**
 * @brief Look up an integer value in a libconfig setting.
 *
 * This function searches the libconfig setting context @p setting for an integer
 * value at the location @p path using libconfig's config_setting_lookup_int().
 * If the lookup succeeds, the result is clamped to a minimum of @p range_min
 * and maximum of @p range_max, then stored in @p value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p value is left unchanged, and -1 is returned.
 *
 * @param[in] setting Pointer to the libconfig setting.
 * @param[in] path Path expression to search within @p setting.
 * @param[out] value Pointer to where the parsed value will be stored on success.
 * @param[in] optional If the value being looked up is optional.
 * @param[in] range_min Minimum value that can be saved to @p value.
 * @param[in] range_max Maximum value that can be saved to @p value.
 * @return 0 on success, -1 on failure.
 *
 * @see config_setting_lookup_int() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005flookup_005fint)
 */
static inline int libconfig_setting_lookup_int( const config_setting_t *setting, const char *path, int *value, const bool optional, const int range_min, const int range_max ) {
        if ( config_setting_lookup_int( setting, path, value ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }
                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }
        *value = clamp_range_int( *value, range_min, range_max );
        return 0;
}

/**
 * @brief Look up an unsigned integer value in a libconfig configuration.
 *
 * This function searches the libconfig configuration @p config for an unsigned
 * integer value at the location @p path using libconfig's config_lookup_int().
 * If the lookup succeeds, the result is clamped to a minimum of @p range_min
 * and maximum of @p range_max, then stored in @p value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p value is left unchanged, and -1 is returned.
 *
 * @param[in] config Pointer to the libconfig config.
 * @param[in] path Path expression to search within @p config.
 * @param[out] value Pointer to where the parsed value will be stored on success.
 * @param[in] optional If the value being looked up is optional.
 * @param[in] range_min Minimum value that can be saved to @p value.
 * @param[in] range_max Maximum value that can be saved to @p value.
 * @return 0 on success, -1 on failure.
 *
 * @see config_lookup_int() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005fsetting_005flookup_005fint)
 */
static inline int libconfig_lookup_uint( const config_t *config, const char *path, unsigned int *value, const bool optional, const unsigned int range_min, const unsigned int range_max ) {
        int tmp = 0;
        if ( config_lookup_int( config, path, &tmp ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }
                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }
        *value = clamp_range_uint( tmp, range_min, range_max );
        return 0;
}

/**
 * @brief Look up an unsigned integer value in a libconfig setting.
 *
 * This function searches the libconfig setting context @p setting for an unsigned
 * integer value at the location @p path using libconfig's config_setting_lookup_int().
 * If the lookup succeeds, the result is clamped to a minimum of @p range_min
 * and maximum of @p range_max, then stored in @p value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p value is left unchanged, and -1 is returned.
 *
 * @param[in] setting Pointer to the libconfig setting.
 * @param[in] path Path expression to search within @p setting.
 * @param[out] value Pointer to where the parsed value will be stored on success.
 * @param[in] optional If the value being looked up is optional.
 * @param[in] range_min Minimum value that can be saved to @p value.
 * @param[in] range_max Maximum value that can be saved to @p value.
 * @return 0 on success, -1 on failure.
 *
 * @see config_setting_lookup_int() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005flookup_005fint)
 */
static inline int libconfig_setting_lookup_uint( const config_setting_t *setting, const char *path, unsigned int *value, const bool optional, const unsigned int range_min,
                                                 const unsigned int range_max ) {
        int tmp = 0;
        if ( config_setting_lookup_int( setting, path, &tmp ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }
                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }
        *value = clamp_range_uint( tmp, range_min, range_max );
        return 0;
}

/**
 * @brief Look up a float value in a libconfig configuration.
 *
 * This function searches the libconfig configuration @p config for a floating
 * point value at the location @p path using libconfig's config_lookup_float().
 * If the lookup succeeds, the result is clamped to a minimum of @p range_min
 * and maximum of @p range_max, then stored in @p value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p value is left unchanged, and -1 is returned.
 *
 * @param[in] config Pointer to the libconfig config.
 * @param[in] path Path expression to search within @p config.
 * @param[out] value Pointer to where the parsed value will be stored on success.
 * @param[in] optional If the value being looked up is optional.
 * @param[in] range_min Minimum value that can be saved to @p value.
 * @param[in] range_max Maximum value that can be saved to @p value.
 * @return 0 on success, -1 on failure.
 *
 * @see config_lookup_float() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005fsetting_005flookup_005ffloat)
 */
static inline int libconfig_lookup_float( const config_t *config, const char *path, float *value, const bool optional, const float range_min, const float range_max ) {
        double tmp = 0;
        if ( config_lookup_float( config, path, &tmp ) != CONFIG_TRUE ) {
                if ( optional ) {
                        log_debug( "Optional value \"%s\" not found, skipping\n", path );
                        return 0;
                }
                log_warn( "Problem reading required config value \"%s\": Not found or of wrong type\n", path );
                return -1;
        }
        *value = clamp_range_float( tmp, range_min, range_max );
        return 0;
}

/**
 * @brief Look up a string value in a libconfig configuration.
 *
 * This function searches the libconfig configuration @p config for a string
 * value at the location @p path using libconfig's config_lookup_string().
 * If the lookup succeeds, the result is stored in @p value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p value is left unchanged, and -1 is returned.
 *
 * @param[in] config Pointer to the libconfig configuration.
 * @param[in] path Path expression to search within @p config.
 * @param[out] value Pointer to where the parsed value will be stored on success.
 * @param[in] optional If the value being looked up is optional.
 * @return 0 on success, -1 on failure.
 *
 * @see config_lookup_string() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005flookup_005fstring)
 */
static inline int libconfig_lookup_string( const config_t *config, const char *path, const char **value, const bool optional ) {
        if ( config_lookup_string( config, path, value ) != CONFIG_TRUE ) {
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
 * If the lookup succeeds, the result is stored in @p value and 0 is returned.
 * If the lookup fails (value not found or wrong type), a warning is logged,
 * @p value is left unchanged, and -1 is returned.
 *
 * @param[in] setting Pointer to the libconfig setting.
 * @param[in] path Path expression to search within @p setting.
 * @param[out] value Pointer to where the parsed value will be stored on success.
 * @param[in] optional If the value being looked up is optional.
 * @return 0 on success, -1 on failure.
 *
 * @see config_setting_lookup_string() in the [Libconfig manual](https://hyperrealm.github.io/libconfig/libconfig_manual.html#index-config_005fsetting_005flookup_005fstring)
 */
static inline int libconfig_setting_lookup_string( const config_setting_t *setting, const char *path, const char **value, const bool optional ) {
        if ( config_setting_lookup_string( setting, path, value ) == CONFIG_FALSE ) {
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
 * @brief Removes the whitespace before and after @p string.
 *
 * This function trims the whitespace before and after @p string.
 * The trim is performed in place on @p string, and assumes it is
 * both mutable and null-terminated.
 *
 * @param string[in,out] Null-terminated, mutable string to be trimmed.
 *
 * @return On success, a pointer to the first non-space character in
 * the string is returned. If @p string was entirely whitespace, an
 * empty string is returned. If @p string was NULL, NULL is returned.
 */
static inline char *trim_whitespace( char *string ) {
        if ( !string ) return NULL;
        while ( isspace( (unsigned char) *string ) ) string++;
        if ( *string == '\0' ) return string;
        char *end = string + strlen( string ) - 1;
        while ( end > string && isspace( (unsigned char) *end ) ) end--;
        *( end + 1 ) = '\0';
        return string;
}

#endif /* PARSER_H_ */