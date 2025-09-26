/**
 * @file parser.c
 * @brief Runtime configuration parser using [libconfig](https://github.com/hyperrealm/libconfig).
 *
 * This file replaces the need to edit @ref config.h or @ref config.def.h and recompile to make changes to
 * dwm's configuration by parsing a configuration file (see example @ref dwm.conf) at runtime. That configuration
 * file contains all the configuration values traditionally found in @ref config.h or @ref config.def.h, and can
 * be edited at any time without the need to change dwm's source code, allowing you to install dwm once and
 * configure it any time you wish without the source code. For more information, please see the patch's GitHub
 * page: https://github.com/JeffofBread/dwm-libconfig.
 *
 * @authors JeffOfBread <jeffofbreadcoding@gmail.com>
 * @authors TODO: Add authors of specific code/functions
 *
 * @see https://github.com/JeffofBread/dwm-libconfig
 *
 * @note I (JeffOfBread) did not write every bit of code present in this file. Though I have made minor changes,
 * it's still not fair to say I wrote the code. Specific credit has been given to each author for their respective
 * functions/code where it is present.
 */

#include <ctype.h>
#include <errno.h>
#include <libconfig.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

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
#define SAFE_FREE( p )     do { if ( p ) { free( ( void * ) ( p ) ); ( p ) = NULL; } } while ( 0 )
#define SAFE_FCLOSE( f )   do { if ( f ) { fclose( f ); ( f ) = NULL; } } while ( 0 )

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

static Configuration dwm_config = { 0 };

// Public parser functions
static void config_cleanup( Configuration *master_config );
static int parse_config( Configuration *master_config );

// Parser specific functions
static void _backup_config( config_t *config );
static void _load_default_buttonbind_config( Button **buttonbind_config, unsigned int *buttonbind_count );
static void _load_default_keybind_config( Key **keybind_config, unsigned int *keybind_count );
static void _load_default_master_config( Configuration *master_config );
static int _open_config( config_t *config, char **config_filepath, Configuration *master_config );
static int _parse_bind_argument( const char *argument_string, const enum Argument_Type *arg_type, Arg *arg, long double range_min, long double range_max );
static int _parse_bind_function( const char *function_string, enum Argument_Type *arg_type, void ( **function )( const Arg * ), long double *range_min, long double *range_max );
static int _parse_bind_modifier( const char *modifier_string, unsigned int *modifier );
static int _parse_buttonbind( const char *buttonbind_string, Button *buttonbind, unsigned int max_keys );
static int _parse_buttonbind_button( const char *button_string, unsigned int *button );
static int _parse_buttonbind_click( const char *click_string, unsigned int *click );
static int _parse_buttonbinds_config( const config_t *config, Button **buttonbind_config, unsigned int *buttonbind_count, unsigned int max_keys );
static int _parse_generic_settings( const config_t *config, Configuration *master_config );
static int _parse_keybind( const char *keybind_string, Key *keybind, unsigned int max_keys );
static int _parse_keybind_keysym( const char *keysym_string, KeySym *keysym );
static int _parse_keybinds_config( const config_t *config, Key **keybind_config, unsigned int *keybinds_count, unsigned int max_keys );
static int _parse_rules_string( const char *input_string, char **output_string );
static int _parse_rules_config( const config_t *config, Rule **rules_config, unsigned int *rules_count );
static int _parse_tags_config( const config_t *config );
static int _parse_theme( const config_setting_t *theme );
static int _parse_theme_config( const config_t *config );

// Wrapper functions for better compatability
static void spawn_simple( const Arg *arg );
static void setlayout_floating( const Arg *arg );
static void setlayout_monocle( const Arg *arg );
static void setlayout_tiled( const Arg *arg );

// Utility functions
static char *_get_xdg_config_home( void );
static char *_get_xdg_data_home( void );
static int _make_parent_directory( const char *path );
static char *_join_strings( const char *string_1, const char *string_2 );
static void _extend_string( char **source_string_pointer, const char *addition );
static int _normalize_path( const char *path, char **normal );

// Inline helper functions
static inline int normalize_range_int( const int i, const int min, const int max ) {
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

static inline unsigned int normalize_range_uint( const unsigned int i, const unsigned int min, const unsigned int max ) {
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

static inline long normalize_range_long( const long i, const long min, const long max ) {
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

static inline unsigned long normalize_range_ulong( const unsigned long i, const unsigned long min, const unsigned long max ) {
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

static inline float normalize_range_float( const float i, const float min, const float max ) {
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
        *value = normalize_range_int( *value, range_min, range_max );
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
        *value = normalize_range_int( *value, range_min, range_max );
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
        *value = normalize_range_uint( tmp, range_min, range_max );
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
        *value = normalize_range_uint( tmp, range_min, range_max );
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
        *value = normalize_range_float( tmp, range_min, range_max );
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

// ----- Function Definitions -----

/**
 * @brief Backs up a libconfig configuration to disk.
 *
 * This function backs up the given libconfig configuration
 * context @p config following the XDG specification. If
 * @ref _get_xdg_data_home() fails to find the XDG data directory
 * (which is likely), we will default to using "~/.local/share/"
 * instead. Meaning that, in the latter case, the filepath to
 * the backup file will be "~/.local/share/dwm/dwm_last.conf".
 * "/dwm/dwm_last.conf" will always be appended to the end of
 * whatever path is returned by @ref _get_xdg_data_home().
 * The backup file is then created and written to by libconfig's
 * config_write_file().
 *
 * @param config[in] Pointer to the libconfig configuration to be
 * backed up.
 */
static void _backup_config( config_t *config ) {

        // Save xdg data folder to buffer (~/.local/share)
        char *buffer = _get_xdg_data_home();

        if ( buffer == NULL ) {
                log_error( "Unable to get necessary directory to backup config\n" );
        } else {

                // Append buffer (already has "~/.local/share" or other xdg data directory)
                // with the directory we want to backup the config to, create the directory
                // if it doesn't exist, and then append with the filename we want to backup
                // to config in.
                _extend_string( &buffer, "/dwm/" );
                _make_parent_directory( buffer );
                _extend_string( &buffer, "dwm_last.conf" );

                if ( config_write_file( config, buffer ) == CONFIG_FALSE ) {
                        log_error( "Problem backing up current config to \"%s\"\n", buffer );
                } else {
                        log_info( "Current config backed up to \"%s\"\n", buffer );
                }

                SAFE_FREE( buffer );
        }
}

static void _load_default_buttonbind_config( Button **buttonbind_config, unsigned int *buttonbind_count ) {
        *buttonbind_count = LENGTH( buttons );
        *buttonbind_config = (Button *) buttons;
}

// Default binds from dwm's `config.def.h`
static void _load_default_keybind_config( Key **keybind_config, unsigned int *keybind_count ) {
        *keybind_count = LENGTH( keys );
        *keybind_config = (Key *) keys;
}

static void _load_default_master_config( Configuration *master_config ) {

        if ( master_config == NULL ) {
                log_fatal( "master_config is NULL, can't load default configuration\n" );
                exit( EXIT_FAILURE );
        }

        // Unique values to the Configuration struct
        master_config->config_filepath = NULL;
        master_config->max_keys = 4;
        master_config->default_binds_loaded = false;

        master_config->rule_array_size = 0;
        master_config->rule_array = NULL;

        master_config->keybind_array_size = 0;
        master_config->keybind_array = NULL;

        master_config->buttonbind_array_size = 0;
        master_config->buttonbind_array = NULL;

        // This is a bit lazy, but simplifies cleanup logic.
        // We dynamically allocate all the values here so they
        // can universally be freed instead of having to keep
        // track of which are dynamic and which are static.
        fonts[ 0 ] = strdup( fonts[ 0 ] );
        for ( int i = 0; i < LENGTH( colors ); i++ ) {
                for ( int j = 0; j < LENGTH( colors[ i ] ); j++ ) {
                        colors[ i ][ j ] = strdup( colors[ i ][ j ] );
                }
        }
}

static int _open_config( config_t *config, char **config_filepath, Configuration *master_config ) {

        int i, config_filepaths_length = 0;
        char *config_filepaths[ 5 ];

        // Check if a custom user config was passed in and copy it if it was
        if ( *config_filepath != NULL ) {
                config_filepaths[ config_filepaths_length++ ] = strdup( *config_filepath );
                SAFE_FREE( *config_filepath );
        }

        // ~/.config/dwm.conf
        char *config_top_directory = _get_xdg_config_home();
        _extend_string( &config_top_directory, "/dwm.conf" );
        config_filepaths[ config_filepaths_length++ ] = config_top_directory;

        // ~/.config/dwm/dwm.conf
        char *config_sub_directory = _get_xdg_config_home();
        _extend_string( &config_sub_directory, "/dwm/dwm.conf" );
        config_filepaths[ config_filepaths_length++ ] = config_sub_directory;

        // ~/.local/share/dwm/dwm_last.conf
        char *config_backup = _get_xdg_data_home();
        _extend_string( &config_backup, "/dwm/dwm_last.conf" );
        config_filepaths[ config_filepaths_length++ ] = config_backup;

        // /etc/dwm/dwm.conf
        char *config_fallback = strdup( "/etc/dwm/dwm.conf" );
        config_filepaths[ config_filepaths_length++ ] = config_fallback;

        FILE *tmp_file = NULL;
        for ( i = 0; i < config_filepaths_length; i++ ) {
                log_debug( "Attempting to open config file \"%s\"\n", config_filepaths[ i ] );

                if ( config_filepaths[ i ] == NULL ) {
                        log_warn( "config_filepaths[%d] was null, unable to lookup intended config. Likely a memory allocation error\n", i );
                        continue;
                }

                tmp_file = fopen( config_filepaths[ i ], "r" );

                if ( tmp_file == NULL ) {
                        log_warn( "Unable to open config file \"%s\"\n", config_filepaths[ i ] );
                        continue;
                }

                if ( config_read( config, tmp_file ) == CONFIG_FALSE ) {
                        log_warn( "Problem parsing config file \"%s\", line %d: %s\n", config_filepaths[ i ], config_error_line( config ), config_error_text( config ) );
                        SAFE_FCLOSE( tmp_file );
                        continue;
                }

                // Save found config filepath
                *config_filepath = strdup( config_filepaths[ i ] );

                // Check if it's a user's custom configuration
                if ( strcmp( config_filepaths[ i ], config_backup ) == 0 || strcmp( config_filepaths[ i ], config_fallback ) == 0 ) {
                        master_config->default_binds_loaded = true;
                }

                for ( i = 0; i < config_filepaths_length; i++ ) {
                        SAFE_FREE( config_filepaths[ i ] );
                }

                SAFE_FCLOSE( tmp_file );

                return 0;
        }

        log_error( "Unable to load any configs. Loading hardcoded default config values and exiting parsing\n" );

        master_config->default_binds_loaded = true;
        _load_default_keybind_config( &master_config->keybind_array, &master_config->keybind_array_size );
        _load_default_buttonbind_config( &master_config->buttonbind_array, &master_config->buttonbind_array_size );

        for ( i = 0; i < config_filepaths_length; i++ ) {
                SAFE_FREE( config_filepaths[ i ] );
        }

        config_destroy( config );
        SAFE_FCLOSE( tmp_file );

        return -1;
}

void config_cleanup( Configuration *master_config ) {
        int i;

        SAFE_FREE( master_config->config_filepath );

        for ( i = 0; i < LENGTH( tags ); i++ ) {
                SAFE_FREE( tags[ i ] );
        }

        for ( i = 0; i < master_config->rule_array_size; i++ ) {
                SAFE_FREE( master_config->rule_array[ i ].class );
                SAFE_FREE( master_config->rule_array[ i ].instance );
                SAFE_FREE( master_config->rule_array[ i ].title );
        }

        for ( i = 0; i < LENGTH( colors ); i++ ) {
                for ( int j = 0; j < LENGTH( colors[ i ] ); j++ ) {
                        SAFE_FREE( colors[ i ][ j ] );
                }
        }

        if ( !master_config->default_binds_loaded ) {
                for ( i = 0; i < master_config->keybind_array_size; i++ ) {
                        if ( master_config->keybind_array[ i ].argument_type == ARG_TYPE_POINTER ) {
                                SAFE_FREE( master_config->keybind_array[ i ].arg.v );
                        }
                }
                SAFE_FREE( master_config->keybind_array );

                for ( i = 0; i < master_config->buttonbind_array_size; i++ ) {
                        if ( master_config->buttonbind_array[ i ].argument_type == ARG_TYPE_POINTER ) {
                                SAFE_FREE( master_config->buttonbind_array[ i ].arg.v );
                        }
                }
                SAFE_FREE( master_config->buttonbind_array );
        }

        config_destroy( master_config->libconfig_config );
}

int parse_config( Configuration *master_config ) {

        static config_t libconfig_config;
        char *config_filepath = NULL;
        int total_errors = 0;

        // Initialize libconfig context
        config_init( &libconfig_config );
        master_config->libconfig_config = &libconfig_config;

        // Simple way of passing users custom config to open_config()
        // This strdup is freed at the start of open_config()
        if ( master_config->config_filepath != NULL ) config_filepath = strdup( master_config->config_filepath );

        // Populate master dwm configuration with default values
        _load_default_master_config( master_config );

        if ( _open_config( &libconfig_config, &config_filepath, master_config ) ) return -1;

        log_info( "Path to config file: \"%s\"\n", config_filepath );
        master_config->config_filepath = strdup( config_filepath );

        char *absolute_config_filepath = realpath( config_filepath, NULL );
        const char *config_include_directory = dirname( absolute_config_filepath );

        if ( config_include_directory ) {
                config_set_include_dir( &libconfig_config, config_include_directory );
        } else {
                log_error( "Unable to resolve configuration include directory\n" );
        }

        SAFE_FREE( absolute_config_filepath );

        config_set_options( &libconfig_config, CONFIG_OPTION_AUTOCONVERT | CONFIG_OPTION_SEMICOLON_SEPARATORS );
        config_set_tab_width( &libconfig_config, 4 );

        // Note: I may want to come back to this and think about how I handle these returns.
        // The return values from the functions aren't the greatest and I may want a threshold
        // or severity based on the error.
        total_errors += _parse_generic_settings( &libconfig_config, master_config );
        total_errors += _parse_keybinds_config( &libconfig_config, &master_config->keybind_array, &master_config->keybind_array_size, master_config->max_keys );
        total_errors += _parse_buttonbinds_config( &libconfig_config, &master_config->buttonbind_array, &master_config->buttonbind_array_size, master_config->max_keys );
        total_errors += _parse_rules_config( &libconfig_config, &master_config->rule_array, &master_config->rule_array_size );
        total_errors += _parse_tags_config( &libconfig_config );
        total_errors += _parse_theme_config( &libconfig_config );

        // The error requirement being 0 may be a bit strict, I am not sure yet. May need
        // some relaxing or possibly come up with a better way of calculating if a config
        // passes, or is valid enough to warrant backing up.
        if ( total_errors == 0 && !master_config->default_binds_loaded ) {
                _backup_config( &libconfig_config );
        } else {
                if ( master_config->default_binds_loaded ) {
                        log_warn( "Not saving config as backup, as current working config is not the user's\n" );
                }
                if ( total_errors != 0 ) {
                        log_warn( "Not saving config as backup, as the parsed config had too many errors\n" );
                }
        }

        SAFE_FREE( config_filepath );

        return 0;
}

static int _parse_bind_argument( const char *argument_string, const enum Argument_Type *arg_type, Arg *arg, const long double range_min, const long double range_max ) {

        log_trace( "Argument being parsed: \"%s\"\n", argument_string );

        if ( !argument_string || argument_string[ 0 ] == '\0' ) {
                log_error( "NULL or empty string passed to parse_bind_argument()\n" );
                return -1;
        }

        char *end_pointer;
        switch ( *arg_type ) {
                case ARG_TYPE_INT:
                        arg->i = normalize_range_long( strtol( argument_string, &end_pointer, 10 ), (long) range_min, (long) range_max );
                        if ( *end_pointer != '\0' ) return -1;
                        log_trace( "Argument type int: %d\n", arg->i );
                        break;
                case ARG_TYPE_UINT:
                        arg->ui = normalize_range_ulong( strtoul( argument_string, &end_pointer, 10 ), (long) range_min, (long) range_max );
                        if ( *end_pointer != '\0' ) return -1;
                        log_trace( "Argument type unsigned int: %u\n", arg->ui );
                        break;
                case ARG_TYPE_FLOAT:
                        arg->f = normalize_range_float( strtof( argument_string, &end_pointer ), (float) range_min, (float) range_max );
                        if ( *end_pointer != '\0' ) return -1;
                        log_trace( "Argument type float: %f\n", arg->f );
                        break;
                case ARG_TYPE_POINTER:
                        arg->v = strdup( argument_string );
                        if ( !arg->v ) {
                                perror( "strdup failed" );
                                return -1;
                        }
                        log_trace( "Argument type pointer (string): \"%s\", (pointer): %p\n", argument_string, arg->v );
                        break;
                default: log_error( "Unknown argument type during bind parsing: %d\n", *arg_type );
                        return -1;
        }

        return 0;
}

static int _parse_bind_function( const char *function_string, enum Argument_Type *arg_type, void ( **function )( const Arg * ), long double *range_min, long double *range_max ) {

        const struct {
                const char *name;
                void ( *func )( const Arg * );
                enum Argument_Type arg_type;
                const long double range_min, range_max;
        } function_alias_map[ ] = {
                { "focusmon", focusmon, ARG_TYPE_INT, -99, 99 },
                { "focusstack", focusstack, ARG_TYPE_INT, -99, 99 },
                { "incnmaster", incnmaster, ARG_TYPE_INT, -99, 99 },
                { "killclient", killclient, ARG_TYPE_NONE },
                { "movemouse", movemouse, ARG_TYPE_NONE },
                { "quit", quit, ARG_TYPE_NONE },
                { "resizemouse", resizemouse, ARG_TYPE_NONE },
                { "setlayout-tiled", setlayout_tiled, ARG_TYPE_NONE },
                { "setlayout-floating", setlayout_floating, ARG_TYPE_NONE },
                { "setlayout-monocle", setlayout_monocle, ARG_TYPE_NONE },
                { "setlayout-toggle", setlayout, ARG_TYPE_NONE },
                { "setmfact", setmfact, ARG_TYPE_FLOAT, -0.95f, 1.95f },
                { "spawn", spawn_simple, ARG_TYPE_POINTER },
                { "tag", tag, ARG_TYPE_INT, -1, TAGMASK },
                { "tagmon", tagmon, ARG_TYPE_INT, -99, 99 },
                { "togglebar", togglebar, ARG_TYPE_NONE },
                { "togglefloating", togglefloating, ARG_TYPE_NONE },
                { "toggletag", toggletag, ARG_TYPE_INT, -1, TAGMASK },
                { "toggleview", toggleview, ARG_TYPE_INT, -1, TAGMASK },
                { "view", view, ARG_TYPE_INT, -1, TAGMASK },
                { "zoom", zoom, ARG_TYPE_NONE },
        };

        log_trace( "Function being parsed: \"%s\"\n", function_string );
        for ( int i = 0; i < LENGTH( function_alias_map ); i++ ) {
                if ( strcasecmp( function_string, function_alias_map[ i ].name ) == 0 ) {
                        *function = function_alias_map[ i ].func;
                        *arg_type = function_alias_map[ i ].arg_type;
                        *range_min = function_alias_map[ i ].range_min;
                        *range_max = function_alias_map[ i ].range_max;
                        log_trace( "Function successfully parsed as %p\n", (void *) function );
                        return 0;
                }
        }

        return -1;
}

static int _parse_bind_modifier( const char *modifier_string, unsigned int *modifier ) {

        const struct {
                const char *name;
                unsigned int mask;
        } modifier_alias_map[ ] = {
                { "super", Mod4Mask },
                { "control", ControlMask },
                { "ctrl", ControlMask },
                { "shift", ShiftMask },
                { "alt", Mod1Mask },
                { "caps", LockMask },
                { "capslock", LockMask },
                { "mod1", Mod1Mask },
                { "mod2", Mod2Mask },
                { "mod3", Mod3Mask },
                { "mod4", Mod4Mask },
                { "mod5", Mod5Mask },
        };

        log_trace( "Modifier being parsed: \"%s\"\n", modifier_string );

        unsigned int found_modifier = 0;
        for ( int i = 0; i < LENGTH( modifier_alias_map ); i++ ) {
                if ( strcasecmp( modifier_string, modifier_alias_map[ i ].name ) == 0 ) {
                        found_modifier = modifier_alias_map[ i ].mask;
                }
        }

        if ( found_modifier == 0 ) return -1;

        log_trace( "Modifier successfully parsed as %u\n", found_modifier );
        *modifier |= found_modifier;
        return 0;
}

static int _parse_buttonbind( const char *buttonbind_string, Button *buttonbind, const unsigned int max_keys ) {

        log_debug( "Buttonbind string to parse: \"%s\"\n", buttonbind_string );

        char buttonbind_string_copy[ 128 ];
        snprintf( buttonbind_string_copy, LENGTH( buttonbind_string_copy ), "%s", buttonbind_string );

        char *modifier_token_list = strtok( buttonbind_string_copy, "," );

        char *click_token = strtok( NULL, "," );
        if ( click_token ) click_token = trim_whitespace( click_token );

        char *function_token = strtok( NULL, "," );
        if ( function_token ) function_token = trim_whitespace( function_token );

        char *argument_token = strtok( NULL, "," );
        if ( argument_token ) argument_token = trim_whitespace( argument_token );

        if ( !modifier_token_list || !function_token || !click_token || modifier_token_list[ 0 ] == '\0' || function_token[ 0 ] == '\0' || click_token[ 0 ] == '\0' ) {
                log_error( "Invalid buttonbind string (expected format: \"mod+key, click, function, arg (if necessary)\" and got \"%s\"\n", buttonbind_string );
                return -1;
        }

        // Split `modifier_token_list` into tokens
        unsigned int modifier_token_count = 0;
        char *trimmed_modifier_token_list[ max_keys ];
        memset( trimmed_modifier_token_list, 0, sizeof( trimmed_modifier_token_list ) );
        char *tmp_token = strtok( modifier_token_list, "+" );
        while ( tmp_token && modifier_token_count < max_keys ) {
                char *trimmed = trim_whitespace( tmp_token );
                if ( *trimmed ) {
                        trimmed_modifier_token_list[ modifier_token_count ] = trimmed;
                        modifier_token_count++;
                }
                tmp_token = strtok( NULL, "+" );
        }

        if ( modifier_token_count == 0 ) {
                log_error( "Empty modifier+button field in buttonbind \"%s\"\n", buttonbind_string );
                return -1;
        }

        if ( modifier_token_count == max_keys && tmp_token ) {
                log_error( "Too many binds (max_keys = %d) in modifier+button field in buttonbind \"%s\"\n", max_keys, buttonbind_string );
                return -1;
        }

        for ( int i = 0; i < modifier_token_count - 1; i++ ) {
                if ( _parse_bind_modifier( trimmed_modifier_token_list[ i ], &buttonbind->mask ) ) {
                        log_error( "Invalid modifier \"%s\" in buttonbind \"%s\"\n", trimmed_modifier_token_list[ i ], buttonbind_string );
                        return -1;
                }
        }

        if ( _parse_buttonbind_button( trimmed_modifier_token_list[ modifier_token_count - 1 ], &buttonbind->button ) ) {
                log_error( "Invalid button \"%s\" in buttonbind \"%s\"\n", function_token, buttonbind_string );
                return -1;
        }

        if ( _parse_buttonbind_click( click_token, &buttonbind->click ) ) {
                log_error( "Invalid click \"%s\" in buttonbind \"%s\"\n", function_token, buttonbind_string );
                return -1;
        }

        long double range_min, range_max;
        if ( _parse_bind_function( function_token, &buttonbind->argument_type, &buttonbind->func, &range_min, &range_max ) ) {
                log_error( "Invalid function \"%s\" in buttonbind \"%s\"\n", function_token, buttonbind_string );
                return -1;
        }

        if ( buttonbind->argument_type != ARG_TYPE_NONE ) {
                if ( _parse_bind_argument( argument_token, &buttonbind->argument_type, &buttonbind->arg, range_min, range_max ) ) {
                        log_error( "Invalid argument \"%s\" in buttonbind \"%s\"\n", argument_token, buttonbind_string );
                        return -1;
                }
        } else {
                log_trace( "Argument type none\n" );
        }

        return 0;
}

static int _parse_buttonbind_button( const char *button_string, unsigned int *button ) {

        const struct {
                const char *name;
                const int button;
        } button_alias_map[ ] = {
                { "leftclick", 1 },
                { "left-click", 1 },
                { "middleclick", 2 },
                { "middle-click", 2 },
                { "rightclick", 3 },
                { "right-click", 3 },
                { "scrollup", 4 },
                { "scroll-up", 4 },
                { "scrolldown", 5 },
                { "scroll-down", 5 },
        };

        log_trace( "Button string to parse: \"%s\"\n", button_string );
        for ( int i = 0; i < LENGTH( button_alias_map ); i++ ) {
                if ( strcasecmp( button_string, button_alias_map[ i ].name ) == 0 ) {
                        *button = button_alias_map[ i ].button;
                        log_trace( "Button successfully parsed as \"%s\" -> %d\n", button_alias_map[ i ].name, button_alias_map[ i ].button );
                        return 0;
                }
        }

        errno = 0;
        char *end_pointer = NULL;
        const unsigned long parsed_value = strtoul( button_string, &end_pointer, 10 );

        if ( errno != 0 || end_pointer == button_string || *end_pointer != '\0' ) return -1;
        if ( parsed_value < 1 || parsed_value > 255 ) return -1;

        *button = (unsigned int) parsed_value;

        log_trace( "Button successfully parsed as %d\n", *button );
        return 0;
}

static int _parse_buttonbind_click( const char *click_string, unsigned int *click ) {

        const struct {
                const char *name;
                const int click;
        } click_alias_map[ ] = { { "tag", ClkTagBar }, { "layout", ClkLtSymbol }, { "status", ClkStatusText }, { "title", ClkWinTitle }, { "client", ClkClientWin }, { "desktop", ClkRootWin }, };

        log_trace( "Click string to parse: \"%s\"\n", click_string );
        for ( int i = 0; i < LENGTH( click_alias_map ); i++ ) {
                if ( strcasecmp( click_string, click_alias_map[ i ].name ) == 0 ) {
                        *click = click_alias_map[ i ].click;
                        log_trace( "Click successfully parsed as \"%s\" -> %d\n", click_alias_map[ i ].name, click_alias_map[ i ].click );
                        return 0;
                }
        }
        return -1;
}

static int _parse_buttonbinds_config( const config_t *config, Button **buttonbind_config, unsigned int *buttonbind_count, const unsigned int max_keys ) {

        // I may look at adjusting how memory is allocated and used here. For example,
        // if a bind fails and is assigned. This just leaves empty unused memory. Not
        // the worst, as most users should not have many or any failing keybinds in
        // their config, certainly not enough to cause seriously egregious levels of
        // memory waste, but still something to consider.

        int failed_buttonbinds_count = 0;
        const config_setting_t *buttonbinds = config_lookup( config, "buttonbinds" );
        if ( buttonbinds != NULL ) {
                *buttonbind_count = config_setting_length( buttonbinds );

                if ( *buttonbind_count == 0 ) {
                        log_warn( "No buttonbinds listed, assigning minimal default buttonbinds and exiting buttonbind parsing\n" );
                        _load_default_buttonbind_config( buttonbind_config, buttonbind_count );
                        return 1;
                }

                log_debug( "Buttonbinds detected: %d\n", *buttonbind_count );

                *buttonbind_config = ecalloc( *buttonbind_count, sizeof( Button ) );

                const config_setting_t *buttonbind = NULL;

                for ( int i = 0; i < *buttonbind_count; i++ ) {
                        buttonbind = config_setting_get_elem( buttonbinds, i );

                        if ( buttonbind == NULL ) {
                                log_error( "Buttonbind element %d returned null, unable to parse\n", i + 1 );
                                failed_buttonbinds_count++;
                                continue;
                        }

                        if ( _parse_buttonbind( config_setting_get_string( buttonbind ), &( *buttonbind_config )[ i ], max_keys ) == -1 ) {
                                ( *buttonbind_config )[ i ].button = 0;
                                failed_buttonbinds_count++;
                        }

                        buttonbind = NULL;
                }
                log_debug( "%d buttonbinds failed to be parsed\n", failed_buttonbinds_count );
        } else {
                log_error( "Problem reading config value \"buttonbinds\": Not found\n" );
                log_warn( "Default buttonbinds will be loaded. It is recommended you fix the config and reload dwm\n" );
        }

        return failed_buttonbinds_count;
}

static int _parse_generic_settings( const config_t *config, Configuration *master_config ) {

        enum Setting_Type {
                TYPE_BOOL,
                TYPE_INT,
                TYPE_UINT,
                TYPE_FLOAT,
                TYPE_STRING
        };

        const struct {
                const char *name;
                void *value;
                const enum Setting_Type type;
                const bool optional;
                const long double range_min, range_max;
        } setting_map[ ] = {
                // General
                { "showbar", &showbar, TYPE_BOOL, true },
                { "topbar", &topbar, TYPE_BOOL, true },
                { "resizehints", &resizehints, TYPE_BOOL, true },
                { "lockfullscreen", &lockfullscreen, TYPE_BOOL, true },
                { "borderpx", &borderpx, TYPE_UINT, true, 0, 9999 },
                { "snap", &snap, TYPE_UINT, true, 0, 9999 },
                { "nmaster", &nmaster, TYPE_UINT, true, 0, 99 },
                { "refreshrate", &refreshrate, TYPE_UINT, true, 0, 999 },
                { "mfact", &mfact, TYPE_FLOAT, true, 0.05f, 0.95f },

                // Advanced
                { "max-keys", &master_config->max_keys, TYPE_UINT, true, 1, 10 },
        };

        log_debug( "Generic settings available: %lu\n", LENGTH( setting_map ) );

        int settings_failed_count = 0;
        for ( int i = 0; i < LENGTH( setting_map ); ++i ) {
                switch ( setting_map[ i ].type ) {
                        case TYPE_BOOL:
                                settings_failed_count -= libconfig_lookup_bool( config, setting_map[ i ].name, setting_map[ i ].value, setting_map[ i ].optional );
                                break;
                        case TYPE_INT:
                                settings_failed_count -= libconfig_lookup_int( config, setting_map[ i ].name, setting_map[ i ].value, setting_map[ i ].optional, (int) setting_map[ i ].range_min,
                                                                               (int) setting_map[ i ].range_max );
                                break;
                        case TYPE_UINT:
                                settings_failed_count -= libconfig_lookup_uint( config, setting_map[ i ].name, setting_map[ i ].value, setting_map[ i ].optional,
                                                                                (unsigned int) setting_map[ i ].range_min, (unsigned int) setting_map[ i ].range_max );
                                break;
                        case TYPE_FLOAT:
                                settings_failed_count -= libconfig_lookup_float( config, setting_map[ i ].name, setting_map[ i ].value, setting_map[ i ].optional, (float) setting_map[ i ].range_min,
                                                                                 (float) setting_map[ i ].range_max );
                                break;
                        case TYPE_STRING:
                                settings_failed_count -= libconfig_lookup_string( config, setting_map[ i ].name, setting_map[ i ].value, setting_map[ i ].optional );
                                break;
                }
        }

        log_debug( "%d generic settings failed to be parsed\n", settings_failed_count );

        return settings_failed_count;
}

static int _parse_keybind( const char *keybind_string, Key *keybind, const unsigned int max_keys ) {

        log_debug( "Keybind string to parse: \"%s\"\n", keybind_string );

        char keybind_string_copy[ 128 ];
        snprintf( keybind_string_copy, LENGTH( keybind_string_copy ), "%s", keybind_string );

        char *modifier_token_list = strtok( keybind_string_copy, "," );

        char *function_token = strtok( NULL, "," );
        if ( function_token ) function_token = trim_whitespace( function_token );

        char *argument_token = strtok( NULL, "," );
        if ( argument_token ) argument_token = trim_whitespace( argument_token );

        if ( !modifier_token_list || !function_token || modifier_token_list[ 0 ] == '\0' || function_token[ 0 ] == '\0' ) {
                log_error( "Invalid keybind string (expected format: \"mod+key, function, arg (if necessary)\" and got \"%s\"\n", keybind_string );
                return -1;
        }

        long double range_min, range_max;
        if ( _parse_bind_function( function_token, &keybind->argument_type, &keybind->func, &range_min, &range_max ) ) {
                log_error( "Invalid function \"%s\" in keybind \"%s\"\n", function_token, keybind_string );
                return -1;
        }

        if ( keybind->argument_type != ARG_TYPE_NONE ) {
                if ( _parse_bind_argument( argument_token, &keybind->argument_type, &keybind->arg, range_min, range_max ) ) {
                        log_error( "Invalid argument \"%s\" in keybind \"%s\"\n", argument_token, keybind_string );
                        return -1;
                }
        } else {
                log_trace( "Argument type none\n" );
        }

        // Split `modifier_token_list` into tokens
        unsigned int modifier_token_count = 0;
        char *trimmed_modifier_token_list[ max_keys ];
        memset( trimmed_modifier_token_list, 0, sizeof( trimmed_modifier_token_list ) );
        char *tmp_token = strtok( modifier_token_list, "+" );
        while ( tmp_token && modifier_token_count < max_keys ) {
                char *trimmed = trim_whitespace( tmp_token );
                if ( *trimmed ) {
                        trimmed_modifier_token_list[ modifier_token_count ] = trimmed;
                        modifier_token_count++;
                }
                tmp_token = strtok( NULL, "+" );
        }

        if ( modifier_token_count == 0 ) {
                log_error( "Empty modifier+key field in keybind \"%s\"\n", keybind_string );
                return -1;
        }

        if ( modifier_token_count == max_keys && tmp_token ) {
                log_error( "Too many binds (max_keys = %d) in modifier+key field in keybind \"%s\"\n", max_keys, keybind_string );
                return -1;
        }

        for ( int i = 0; i < modifier_token_count - 1; i++ ) {
                if ( _parse_bind_modifier( trimmed_modifier_token_list[ i ], &keybind->mod ) ) {
                        log_error( "Invalid modifier \"%s\" in keybind \"%s\"\n", trimmed_modifier_token_list[ i ], keybind_string );
                        return -1;
                }
        }

        if ( _parse_keybind_keysym( trimmed_modifier_token_list[ modifier_token_count - 1 ], &keybind->keysym ) ) {
                log_error( "Invalid keysym \"%s\" in keybind \"%s\"\n", trimmed_modifier_token_list[ modifier_token_count - 1 ], keybind_string );
                return -1;
        }

        return 0;
}

static int _parse_keybind_keysym( const char *keysym_string, KeySym *keysym ) {

        log_trace( "Keysym being parsed: \"%s\"\n", keysym_string );

        *keysym = XStringToKeysym( keysym_string );
        if ( *keysym == NoSymbol ) return -1;

        KeySym dummy = 0; // Unused, just needs to exist to satisfy compiler
        XConvertCase( *keysym, keysym, &dummy );

        log_trace( "Keysym successfully parsed as parsed: \"%s\" -> 0x%lx\n", XKeysymToString( *keysym ), *keysym );
        return 0;
}

static int _parse_keybinds_config( const config_t *config, Key **keybind_config, unsigned int *keybinds_count, const unsigned int max_keys ) {

        // I may look at adjusting how memory is allocated and used here. For example,
        // if a bind fails and is assigned. This just leaves empty unused memory. Not
        // the worst, as most users should not have many or any failing keybinds in
        // their config, certainly not enough to cause seriously egregious levels of
        // memory waste, but still something to consider.

        int failed_keybinds = 0;
        const config_setting_t *keybinds = config_lookup( config, "keybinds" );
        if ( keybinds != NULL ) {
                *keybinds_count = config_setting_length( keybinds );

                if ( *keybinds_count == 0 ) {
                        log_warn( "No keybinds listed, assigning minimal default keybinds and exiting keybinds parsing\n" );
                        _load_default_keybind_config( keybind_config, keybinds_count );
                        return 1;
                }

                log_debug( "Keybinds detected: %d\n", *keybinds_count );

                *keybind_config = ecalloc( *keybinds_count, sizeof( Key ) );
                const config_setting_t *keybind = NULL;
                for ( int i = 0; i < *keybinds_count; i++ ) {
                        keybind = config_setting_get_elem( keybinds, i );

                        if ( keybind == NULL ) {
                                log_error( "Keybind element %d returned null, unable to parse\n", i + 1 );
                                failed_keybinds++;
                                continue;
                        }

                        if ( _parse_keybind( config_setting_get_string( keybind ), &( *keybind_config )[ i ], max_keys ) == -1 ) {
                                ( *keybind_config )[ i ].keysym = NoSymbol;
                                failed_keybinds++;
                        }

                        keybind = NULL;
                }
                log_debug( "%d keybinds failed to be parsed\n", failed_keybinds );
        } else {
                log_error( "Problem reading config value \"keybinds\": Not found\n" );
                log_warn( "Default keybinds will be loaded. It is recommended you fix the config and reload dwm\n" );
        }

        return failed_keybinds;
}

static int _parse_rules_string( const char *input_string, char **output_string ) {

        if ( input_string == NULL ) return -1;

        if ( strcasecmp( input_string, "null" ) == 0 ) {
                *output_string = strdup( "\0" );
                return 0;
        }

        *output_string = strdup( input_string );

        return 0;
}

static int _parse_rules_config( const config_t *config, Rule **rules_config, unsigned int *rules_count ) {

        int failed_rules_count = 0;
        int failed_rules_elements_count = 0;

        const config_setting_t *rules_setting = config_lookup( config, "rules" );
        if ( rules_setting != NULL ) {
                *rules_count = config_setting_length( rules_setting );

                if ( *rules_count == 0 ) {
                        log_warn( "No rules listed, exiting rules parsing\n" );
                        return 1;
                }

                log_debug( "Rules detected: %d\n", *rules_count );

                *rules_config = ecalloc( *rules_count, sizeof( Rule ) );

                const char *tmp_string = NULL;
                const config_setting_t *rule = NULL;

                for ( int i = 0; i < *rules_count; i++ ) {
                        rule = config_setting_get_elem( rules_setting, i );
                        if ( rule != NULL ) {

                                libconfig_setting_lookup_string( rule, "class", &tmp_string, false );
                                if ( _parse_rules_string( tmp_string, &( *rules_config )[ i ].class ) ) {
                                        log_error( "Problem parsing \"class\" value of rule %d\n", i + 1 );
                                        failed_rules_elements_count++;
                                }

                                libconfig_setting_lookup_string( rule, "instance", &tmp_string, false );
                                if ( _parse_rules_string( tmp_string, &( *rules_config )[ i ].instance ) ) {
                                        log_error( "Problem parsing \"instance\" value of rule %d\n", i + 1 );
                                        failed_rules_elements_count++;
                                }

                                libconfig_setting_lookup_string( rule, "title", &tmp_string,false );
                                if ( _parse_rules_string( tmp_string, &( *rules_config )[ i ].title ) ) {
                                        log_error( "Problem parsing \"title\" value of rule %d\n", i + 1 );
                                        failed_rules_elements_count++;
                                }

                                failed_rules_elements_count -= libconfig_setting_lookup_uint( rule, "tag-mask", &( *rules_config )[ i ].tags, false, 0, TAGMASK );
                                failed_rules_elements_count -= libconfig_setting_lookup_int( rule, "monitor", &( *rules_config )[ i ].monitor, false, -1, 99 );
                                failed_rules_elements_count -= libconfig_setting_lookup_int( rule, "floating", &( *rules_config )[ i ].isfloating, false, 0, 1 );
                        } else {
                                log_error( "Rule %d returned null, unable to parse\n", i + 1 );
                                failed_rules_count++;
                        }
                }
        } else {
                log_error( "Problem reading config value \"rules\": Not found\n" );
                return 1;
        }

        log_debug( "%d rules failed to be parsed\n", failed_rules_count );
        log_debug( "Of those rules, %d rule elements failed to be parsed\n", failed_rules_elements_count );

        return failed_rules_count + failed_rules_elements_count;
}

static int _parse_tags_config( const config_t *config ) {

        int tags_failed_count = 0;

        const config_setting_t *tag_names = config_lookup( config, "tag-names" );
        if ( tag_names != NULL ) {
                const char *tag_name = NULL;
                int tags_count = config_setting_length( tag_names );

                if ( tags_count == 0 ) {
                        log_warn( "No tag names detected while parsing config, default tag names will be used\n" );
                        return 1;
                }

                log_debug( "Tags detected: %d\n", tags_count );

                if ( tags_count > LENGTH( tags ) ) {
                        log_warn( "More than %lu tag names detected (%d were detected) while parsing config, only the first %lu will be used\n", LENGTH( tags ), tags_count, LENGTH( tags ) );
                        tags_count = LENGTH( tags );
                } else if ( tags_count < LENGTH( tags ) ) {
                        log_warn( "Less than %lu tag names detected while parsing config, filler tags will be used for the remainder\n", LENGTH( tags ) );
                }

                for ( int i = 0; i < tags_count; i++ ) {
                        tag_name = config_setting_get_string_elem( tag_names, i );

                        if ( tag_name == NULL ) {
                                log_error( "Problem reading tag array element %d: Value doesn't exist or isn't a string\n", i + 1 );
                                tags_failed_count++;
                                continue;
                        }

                        char fallback_tag_name[ 32 ];
                        snprintf( fallback_tag_name, sizeof( fallback_tag_name ), "%d", i + 1 );

                        tags[ i ] = strdup( tag_name );
                        if ( tags[ i ] == NULL ) {
                                log_error( "strdup failed while copying parsed tag %d\n", i );
                                tags[ i ] = strdup( fallback_tag_name );
                                tags_failed_count++;
                                continue;
                        }
                }
        } else {
                log_error( "Problem reading config value \"tag-names\": Not found\n" );
                return 1;
        }

        log_debug( "%d tags failed to be parsed\n", tags_failed_count );

        return tags_failed_count;
}

static int _parse_theme( const config_setting_t *theme ) {

        const char *tmp_string = NULL;

        int theme_elements_failed_count = 0;

        const struct {
                const char *path;
                const char **value;
        } Theme_Mapping[ ] = {
                { "font", &fonts[ 0 ] },
                { "normal-foreground", &colors[ SchemeNorm ][ ColFg ] },
                { "normal-background", &colors[ SchemeNorm ][ ColBg ] },
                { "normal-border", &colors[ SchemeNorm ][ ColBorder ] },
                { "selected-foreground", &colors[ SchemeSel ][ ColFg ] },
                { "selected-background", &colors[ SchemeSel ][ ColBg ] },
                { "selected-border", &colors[ SchemeSel ][ ColBorder ] },
        };

        for ( int i = 0; i < LENGTH( Theme_Mapping ); i++ ) {
                if ( !libconfig_setting_lookup_string( theme, Theme_Mapping[ i ].path, &tmp_string,false ) ) {
                        SAFE_FREE( *Theme_Mapping[ i ].value );
                        *Theme_Mapping[ i ].value = strdup( tmp_string );
                } else {
                        theme_elements_failed_count++;
                }
        }

        return theme_elements_failed_count;
}

static int _parse_theme_config( const config_t *config ) {

        int failed_themes_count = 0;
        int failed_theme_elements_count = 0;

        const config_setting_t *themes = config_lookup( config, "themes" );
        if ( themes != NULL ) {

                int detected_theme_count = config_setting_length( themes );

                if ( detected_theme_count == 0 ) {
                        log_error( "Problem reading config value \"themes\": Not themes provided\n" );
                        log_warn( "Default theme will be loaded\n" );
                        return 1;
                }

                log_debug( "Themes detected: %d\n", detected_theme_count );

                // TODO: Add a simple config setting to choose what theme index to load
                // Example: "theme-to-use = 2;" will use theme number 2
                if ( detected_theme_count > 1 ) {
                        log_warn( "More than 1 theme detected. dwm can only use the first theme in list \"themes\"\n" );
                        detected_theme_count = 1;
                }

                config_setting_t *theme = NULL;

                for ( int i = 0; i < detected_theme_count; i++ ) {

                        theme = config_setting_get_elem( themes, i );

                        if ( theme == NULL ) {
                                log_error( "Theme %d returned null, unable to parse\n", i + 1 );
                                failed_themes_count++;
                                continue;
                        }

                        failed_theme_elements_count += _parse_theme( theme );
                        log_debug( "%d elements failed to be parsed in theme number %d\n", failed_theme_elements_count, i + 1 );
                }

                log_debug( "%d themes failed to be parsed\n", failed_themes_count );

        } else {
                log_error( "Problem reading config value \"themes\": Not found\n" );
                log_warn( "Default theme will be loaded\n" );
                return 1;
        }

        return failed_themes_count + failed_theme_elements_count;
}

// Wrapper functions for compatability

void spawn_simple( const Arg *arg ) {

        // Process argv to work with default spawn() behavior
        const char *cmd = arg->v;
        char *argv[ ] = { "/bin/sh", "-c", (char *) cmd, NULL };
        log_debug( "Attempting to spawn \"%s\"\n", (char *) cmd );

        // Call spawn with our new processed value
        const Arg tmp = { .v = argv };
        spawn( &tmp );
}

static Arg find_layout( void ( *arrange )( Monitor * ) ) {
        for ( int i = 0; i < LENGTH( layouts ); i++ ) {
                if ( layouts[ i ].arrange == arrange ) {
                        return (Arg){ .v = &layouts[ i ] };
                }
        }
        return (Arg){ .i = 0 };
}

static void setlayout_floating( const Arg *arg ) {
        Arg tmp = find_layout( NULL );
        if ( !tmp.i ) {
                log_warn( "setlayout_floating() failed to find floating layout in \"layouts\" array\n" );
        } else {
                setlayout( &tmp );
        }
}

static void setlayout_monocle( const Arg *arg ) {
        Arg tmp = find_layout( monocle );
        if ( !tmp.i ) {
                log_warn( "setlayout_monocle() failed to find monocle layout in \"layouts\" array\n" );
        } else {
                setlayout( &tmp );
        }
}

static void setlayout_tiled( const Arg *arg ) {
        Arg tmp = find_layout( tile );
        if ( !tmp.i ) {
                log_warn( "setlayout_tiled() failed to find tile layout in \"layouts\" array\n" );
        } else {
                setlayout( &tmp );
        }
}

static unsigned long _length_wrapper( const Configuration *master_config, const void *pointer, const unsigned long precalculated_length ) {

        // Return custom lengths if they match elements from the master configuration
        if ( pointer == master_config->rule_array ) return master_config->rule_array_size;
        if ( pointer == master_config->keybind_array ) return master_config->keybind_array_size;
        if ( pointer == master_config->buttonbind_array ) return master_config->buttonbind_array_size;

        // Else return the computed length from sizeof(pointer)/sizeof(pointer)[0]
        return precalculated_length;
}

// Derived from picom ( config.c::xdg_config_home() )
char *_get_xdg_config_home( void ) {
        char *xdg_config_home = getenv( "XDG_CONFIG_HOME" );
        char *user_home = getenv( "HOME" );

        if ( !xdg_config_home ) {
                const char *default_config_directory = "/.config";
                if ( !user_home ) return NULL;
                xdg_config_home = _join_strings( user_home, default_config_directory );
        } else {
                xdg_config_home = strdup( xdg_config_home );
        }

        return xdg_config_home;
}

// Derived from picom ( config.c::xdg_config_home() )
char *_get_xdg_data_home( void ) {
        char *xdg_data_home = getenv( "XDG_DATA_HOME" );
        char *user_home = getenv( "HOME" );

        if ( !xdg_data_home ) {
                const char *default_data_directory = "/.local/share";
                if ( !user_home ) return NULL;
                xdg_data_home = _join_strings( user_home, default_data_directory );
        } else {
                xdg_data_home = strdup( xdg_data_home );
        }

        return xdg_data_home;
}

/**
 * @param path
 * @return
 *
 * @author Mihir Lad - <mihirlad55@gmail>
 * @see https://github.com/mihirlad55/dwm-ipc
 */
// Derived from dwm-ipc ( util.c::mkdirp() )
int _make_parent_directory( const char *path ) {
        char *normal;
        char *walk;
        size_t normallen;

        _normalize_path( path, &normal );
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
                                log_debug( "Making directory %s\n", curpath );
                                if ( mkdir( curpath, 0700 ) < 0 ) {
                                        log_error( "Failed to make directory %s\n", curpath );
                                        perror( "" );
                                        free( normal );
                                        return -1;
                                }
                        } else {
                                log_error( "Error stat-ing directory %s\n", curpath );
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

// gcc warns about legitimate truncation worries in strncpy in _join_strings.
// strncpy( joined_string, string_1, length_1 ) intentionally truncates the null byte
// from string_1, however. strncpy( joined_string + length_1, string_2, length_2 )
// uses bounds depending on the source argument, but joined_string is allocated with
// length_1 + length_2 + 1, so this strncpy can't overflow.
//
// Allocate the space and join two strings. - Derived from picom ( str.c::mstrjoin() )
char *_join_strings( const char *string_1, const char *string_2 ) {
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
void _extend_string( char **source_string_pointer, const char *addition ) {
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
 *
 * @author Mihir Lad - <mihirlad55@gmail>
 * @see https://github.com/mihirlad55/dwm-ipc
 */
// Derived from dwm-ipc ( util.c::normalizepath() )
int _normalize_path( const char *path, char **normal ) {
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

// This is to silence compiler warnings from the new length macro, feel free to remove this.
#pragma GCC diagnostic ignored "-Wsizeof-pointer-div"

// Undefine original length macro in favor of this modified wrapper. Lose compile
// time calculation under most optimizations, but is required for how the parser
// overrides the variables from config.h
#undef LENGTH
#define LENGTH( X ) _length_wrapper( &dwm_config, X, ( sizeof( X ) / sizeof( X )[ 0 ] ) )
static unsigned long _length_wrapper( const Configuration *master_config, const void *pointer, unsigned long precalculated_length );

// Override config.h variables with parsed values. This is done at the end of the file
// to reduce the headaches above, where some values from config.h are used as fallbacks
// if parsing fails.
#define rules dwm_config.rule_array
#define keys dwm_config.keybind_array
#define buttons dwm_config.buttonbind_array
