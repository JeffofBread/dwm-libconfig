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
 * @authors Yuxuan Shui - <yshuiv7@gmail.com>, author of [picom](https://github.com/yshui/picom), which I (JeffOfBread) copied code from. Code from picom is credited as such.
 * @authors Mihir Lad - <mihirlad55@gmail.com>, author of [dwm-ipc](https://github.com/mihirlad55/dwm-ipc), which I (JeffOfBread) copied code from. Code from dwm-ipc is credited as such.
 *
 * @see https://github.com/hyperrealm/libconfig
 * @see https://github.com/JeffofBread/dwm-libconfig
 * @see https://github.com/yshui/picom
 * @see https://github.com/mihirlad55/dwm-ipc
 *
 * @warning This file must be included after the definition of the structs
 * @ref Arg, @ref Button, @ref Key, and @ref Rule.
 *
 * @note I (JeffOfBread) did not write all the code present in this file. Though I have made minor changes,
 * it's still not fair to say I wrote the code. I have listed them above as authors, and all code I used from
 * them (most of the utility functions) has been credited accordingly.
 *
 * @todo Finish documentation
 * @todo Cleanup or make more consistent the use of trace logs
 * @todo Better error handling. The current error handling is horrific.
 * @todo Move alias mapping to a header file for easier tweaking. Maybe also delim characters.
 * @todo Make sure function arguments are noted for being dynamically allocated in that function or its sub functions.
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
#include <sys/stat.h>

// Simple wrappers for free/fclose to improve null safety. It is not flawless or a catch-all however
#define SAFE_FREE( p ) do { if ( p ) { free( ( void * ) ( p ) ); ( p ) = NULL; } } while ( 0 )
#define SAFE_FCLOSE( f ) do { if ( f ) { fclose( f ); ( f ) = NULL; } } while ( 0 )

// Preprocessor string manipulation
#define _TOSTRING( X ) #X
#define TOSTRING( X ) _TOSTRING( X )

// Uncomment to enable log printing for debugging. This is just
// a crude compatability macro between my own logging system,
// which I didn't want to bring over just for the config parser.
#define log_trace( ... ) //_log( "TRACE", __VA_ARGS__ )
#define log_debug( ... ) _log( "DEBUG", __VA_ARGS__ )
#define log_info( ... ) _log( "INFO", __VA_ARGS__ )
#define log_warn( ... ) _log( "WARN", __VA_ARGS__ )
#define log_error( ... ) _log( "ERROR", __VA_ARGS__ )
#define log_fatal( ... ) _log( "FATAL", __VA_ARGS__ )
#define _log( LEVEL, ... ) fprintf( stdout, LEVEL ": [" __FILE__ "::" TOSTRING(__LINE__) "]: "  __VA_ARGS__ )

// Some macros to make later code in dwm.c
// a little smaller and easier to read.
#define BUTTONBINDS dwm_config.buttonbind_array
#define KEYBINDS dwm_config.keybind_array

// Define some repetitive clamping functions using a macro.
// This equally could be a macro like this:
//      #define CLAMP(A, MIN, MAX) ((A) < (MIN) ? (MIN) : ((A) > (MAX) ? (MAX) : (A)))
// But that clamps the range silently. No print or notification a value was out of range.
#define DEFINE_CLAMP_FUNCTION( NAME, TYPE, FORMAT )                                                     \
        static TYPE clamp_range_##NAME( TYPE input, TYPE min, TYPE max ) {                       \
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

/**
 * @enum Argument_Type
 * @brief Enum to inform the program what data is stored in an @ref Arg union.
 *
 * The Argument_Type enum is a simple way of noting what kind of data was stored in an
 * argument. It acts as a simple safeguard against incorrect usage of an argument's data.
 *
 * @see @ref Arg
 */
enum Argument_Type {
        ARG_TYPE_NONE,
        ARG_TYPE_INT,
        ARG_TYPE_UINT,
        ARG_TYPE_FLOAT,
        ARG_TYPE_POINTER,
};

// Struct to hold some parser internal data and
// some of the configuration data that can't
// be written to variables in `config.(def.).h`
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

Configuration dwm_config = { 0 };

/// Public parser functions ///
void config_cleanup( Configuration *config );
int parse_config( Configuration *config );
void setlayout_floating( const Arg *arg );
void setlayout_monocle( const Arg *arg );
void setlayout_tiled( const Arg *arg );
void spawn_simple( const Arg *arg );

/// Parser internal functions declaration ///
static void _backup_config( config_t *libconfig_config );
static void _load_default_config( Configuration *config );
static int _open_config( Configuration *config );
static int _parse_bind_argument( const char *argument_string, enum Argument_Type arg_type, long double range_min, long double range_max, Arg *parsed_arg );
static int _parse_bind_function( const char *function_string, void ( **parsed_function )( const Arg * ), enum Argument_Type *parsed_arg_type, long double *parsed_range_min,
                                 long double *parsed_range_max );
static int _parse_bind_modifier( const char *modifier_string, unsigned int *parsed_modifier );
static int _parse_buttonbind( const char *buttonbind_string, unsigned int max_keys, Button *parsed_buttonbind );
static int _parse_buttonbind_button( const char *button_string, unsigned int *parsed_button );
static int _parse_buttonbind_click( const char *click_string, unsigned int *parsed_click );
static int _parse_buttonbinds_config( const config_t *libconfig_config, unsigned int max_keys, Button **buttonbind_config, unsigned int *buttonbind_count, bool *default_buttonbinds_loaded );
static int _parse_generic_settings( const config_t *libconfig_config, unsigned int *max_keys );
static int _parse_keybind( const char *keybind_string, unsigned int max_keys, Key *parsed_keybind );
static int _parse_keybind_keysym( const char *keysym_string, KeySym *parsed_keysym );
static int _parse_keybinds_config( const config_t *libconfig_config, unsigned int max_keys, Key **keybind_config, unsigned int *keybinds_count, bool *default_keybinds_loaded );
static int _parse_rule( const config_setting_t *rule_libconfig_setting, int rule_index, Rule *parsed_rule );
static int _parse_rule_string( const config_setting_t *rule_libconfig_setting, const char *path, int rule_index, char **parsed_value );
static int _parse_rules_config( const config_t *libconfig_config, Rule **rules_config, unsigned int *rules_count, bool *default_rules_loaded );
static int _parse_tags_config( const config_t *libconfig_config );
static int _parse_theme( const config_setting_t *theme_libconfig_setting );
static int _parse_theme_config( const config_t *libconfig_config );

/// Utility functions declaration ///
static char *_estrdup( const char *string );
static void _extend_string( char **source_string_pointer, const char *addition );
static Arg _find_layout( void ( *arrange )( Monitor * ) );
static char *_get_xdg_config_home( void );
static char *_get_xdg_data_home( void );
static char *_join_strings( const char *string_1, const char *string_2 );
static int _libconfig_lookup_bool( const config_t *config, const char *path, bool optional, bool *parsed_value );
static int _libconfig_lookup_int( const config_t *config, const char *path, bool optional, int range_min, int range_max, int *parsed_value );
static int _libconfig_setting_lookup_int( const config_setting_t *setting, const char *path, bool optional, int range_min, int range_max, int *parsed_value );
static int _libconfig_lookup_uint( const config_t *config, const char *path, bool optional, unsigned int range_min, unsigned int range_max, unsigned int *parsed_value );
static int _libconfig_setting_lookup_uint( const config_setting_t *setting, const char *path, bool optional, unsigned int range_min, unsigned int range_max, unsigned int *parsed_value );
static int _libconfig_lookup_float( const config_t *config, const char *path, bool optional, float range_min, float range_max, float *parsed_value );
static int _libconfig_lookup_string( const config_t *config, const char *path, bool optional, const char **parsed_value );
static int _libconfig_setting_lookup_string( const config_setting_t *setting, const char *path, bool optional, const char **parsed_value );
static int _make_directory_path( const char *path );
static int _normalize_path( const char *original_path, char **normalized_path );
static char *_trim_whitespace( char *input_string );

/// Parser public function definitions ///

/**
 * @brief Frees all members of a @ref Configuration struct.
 *
 * Frees all members of a @ref Configuration struct. Intended
 * for use after @ref parse_config().
 *
 * @param[in,out] config Pointer to the @ref Configuration
 * struct to be cleaned.
 */
void config_cleanup( Configuration *config ) {

        int i;

        SAFE_FREE( config->config_filepath );

        for ( i = 0; i < LENGTH( tags ); i++ ) {
                SAFE_FREE( tags[ i ] );
        }

        for ( i = 0; i < LENGTH( colors ); i++ ) {
                for ( int j = 0; j < LENGTH( colors[ i ] ); j++ ) {
                        SAFE_FREE( colors[ i ][ j ] );
                }
        }

        if ( !config->default_rules_loaded ) {
                for ( i = 0; i < config->rule_array_size; i++ ) {
                        SAFE_FREE( config->rule_array[ i ].class );
                        SAFE_FREE( config->rule_array[ i ].instance );
                        SAFE_FREE( config->rule_array[ i ].title );
                }
        }

        if ( !config->default_keybinds_loaded ) {
                for ( i = 0; i < config->keybind_array_size; i++ ) {
                        if ( config->keybind_array[ i ].argument_type == ARG_TYPE_POINTER ) {
                                SAFE_FREE( config->keybind_array[ i ].arg.v );
                        }
                }
                SAFE_FREE( config->keybind_array );
        }

        if ( !config->default_buttonbinds_loaded ) {
                for ( i = 0; i < config->buttonbind_array_size; i++ ) {
                        if ( config->buttonbind_array[ i ].argument_type == ARG_TYPE_POINTER ) {
                                SAFE_FREE( config->buttonbind_array[ i ].arg.v );
                        }
                }
                SAFE_FREE( config->buttonbind_array );
        }

        config_destroy( &config->libconfig_config );
}

/**
 * @brief Parse program configuration from a configuration file.
 *
 * This function serves as the entry point into the configuration parser of the
 * [dwm-libconfig patch](https://github.com/JeffofBread/dwm-libconfig). This function
 * serves as a high level entry to the configuration parser. It largely calls helper
 * functions to handle the actual parsing of the configuration file in various segmented
 * and modular steps.
 *
 * For more information on the specific details on what actions the parser performs,
 * please reference the helper function related to the functionality you want to learn
 * more about, they all have their own documentation and comments.
 *
 * @param[in,out] config Pointer to the @ref Configuration struct. It is expected to be
 * a valid and mutable pointer to an already allocated struct.
 *
 * @return -1 if no configuration file is successfully found or parsed, 0 on complete
 * success, or >0 if there were errors while parsing the configuration. Any number over
 * 0 represents the number of errors during the process.
 *
 * @authors JeffOfBread <jeffofbreadcoding@gmail.com>
 *
 * @see https://github.com/JeffofBread/dwm-libconfig
 */
int parse_config( Configuration *config ) {

        _load_default_config( config );

        if ( _open_config( config ) ) return -1;

        log_info( "Path to config file: \"%s\"\n", config->config_filepath );

        char *config_include_directory = realpath( config->config_filepath, NULL );
        config_include_directory = dirname( config_include_directory );

        if ( config_include_directory ) {
                config_set_include_dir( &config->libconfig_config, config_include_directory );
        } else {
                log_error( "Unable to resolve configuration include directory\n" );
        }

        SAFE_FREE( config_include_directory );

        config_set_options( &config->libconfig_config, CONFIG_OPTION_AUTOCONVERT | CONFIG_OPTION_SEMICOLON_SEPARATORS );
        config_set_tab_width( &config->libconfig_config, 4 );

        // TODO: This error handing scheme sucks and needs to be replaced.
        int total_errors = 0;
        total_errors += _parse_generic_settings( &config->libconfig_config, &config->max_keys );
        total_errors += _parse_keybinds_config( &config->libconfig_config, config->max_keys, &config->keybind_array, &config->keybind_array_size, &config->default_keybinds_loaded );
        total_errors += _parse_buttonbinds_config( &config->libconfig_config, config->max_keys, &config->buttonbind_array, &config->buttonbind_array_size, &config->default_buttonbinds_loaded );
        total_errors += _parse_rules_config( &config->libconfig_config, &config->rule_array, &config->rule_array_size, &config->default_rules_loaded );
        total_errors += _parse_tags_config( &config->libconfig_config );
        total_errors += _parse_theme_config( &config->libconfig_config );

        // The error requirement being 0 may be a bit strict, I am not sure. May need
        // some relaxing or possibly come up with a better way of calculating if a config
        // passes, or is valid enough to warrant backing up.
        if ( total_errors == 0 && !( config->default_keybinds_loaded || config->default_buttonbinds_loaded || config->fallback_config_loaded ) ) {
                _backup_config( &config->libconfig_config );
        } else {
                if ( config->default_keybinds_loaded || config->default_buttonbinds_loaded ) {
                        log_warn( "Not saving config as backup, as hardcoded default bind values were used, not the user's\n" );
                }
                if ( config->fallback_config_loaded ) {
                        log_warn( "Not saving config as backup, as the parsed configuration file is a system fallback configuration\n" );
                }
                if ( total_errors != 0 ) {
                        log_warn( "Not saving config as backup, as the parsed config had too many errors\n" );
                }
        }

        return total_errors;
}

/**
 * @brief Set current layout to floating.
 *
 * This is a wrapper for the @ref setlayout() function. It sets the
 * currently focused monitor's layout to the floating layout found
 * in the @ref layouts array.
 *
 * @param arg Unused.
 */
void setlayout_floating( const Arg *arg ) {

        Arg tmp = _find_layout( NULL );
        if ( !tmp.i ) {
                log_warn( "setlayout_floating() failed to find floating layout in \"layouts\" array\n" );
        } else {
                setlayout( &tmp );
        }
}

/**
 * @brief Set current layout to monocle.
 *
 * This is a wrapper for the @ref setlayout() function. It sets the
 * currently focused monitor's layout to the monocle layout found in
 * the @ref layouts array.
 *
 * @param arg Unused.
 */
void setlayout_monocle( const Arg *arg ) {

        Arg tmp = _find_layout( monocle );
        if ( !tmp.i ) {
                log_warn( "setlayout_monocle() failed to find monocle layout in \"layouts\" array\n" );
        } else {
                setlayout( &tmp );
        }
}

/**
 * @brief Set current layout to tile.
 *
 * This is a wrapper for the @ref setlayout() function. It sets the
 * currently focused monitor's layout to the tile layout found in the
 * @ref layouts array.
 *
 * @param arg Unused.
 */
void setlayout_tiled( const Arg *arg ) {

        Arg tmp = _find_layout( tile );
        if ( !tmp.i ) {
                log_warn( "setlayout_tiled() failed to find tile layout in \"layouts\" array\n" );
        } else {
                setlayout( &tmp );
        }
}

/**
 * @brief Wrapper around @ref spawn() for simpler program spawning.
 *
 * This wrapper is to simplify the parsers interaction with the
 * @ref spawn() function. It takes in the program name and arguments
 * as a string from @p arg, prepares them with the necessary shell
 * executable and flag, and then passes it all to @ref spawn().
 *
 * @param[in] arg Pointer to the @ref Arg struct containing a null
 * terminated string containing the name and arguments of the program
 * to spawn.
 */
void spawn_simple( const Arg *arg ) {

        // Process argv to work with default spawn() behavior
        const char *cmd = arg->v;
        char *argv[ ] = { "/bin/sh", "-c", (char *) cmd, NULL };
        log_debug( "Attempting to spawn \"%s\"\n", (char *) cmd );

        // Call spawn with our new processed value
        const Arg tmp = { .v = argv };
        spawn( &tmp );
}

/// Parser internal function definitions ///

/**
 * @brief Backs up a libconfig @ref config_t to disk.
 *
 * This function backs up the given libconfig @ref config_t
 * @p libconfig_config following the XDG specification. If
 * @ref _get_xdg_data_home() fails to find the XDG data directory
 * (which is likely), we will default to using "~/.local/share/"
 * instead. Meaning that, in the latter case, the filepath to
 * the backup file will be "~/.local/share/dwm/dwm_last.conf".
 * "/dwm/dwm_last.conf" will always be appended to the end of
 * whatever path is returned by @ref _get_xdg_data_home().
 * The backup file is then created and written to by libconfig's
 * config_write_file().
 *
 * @param[in] libconfig_config Pointer to the libconfig @ref config_t
 * to be backed up.
 */
static void _backup_config( config_t *libconfig_config ) {

        // Save xdg data directory to buffer (~/.local/share)
        char *buffer = _get_xdg_data_home();

        if ( buffer == NULL ) {
                log_error( "Unable to get necessary directory to backup config\n" );
        } else {

                // Append buffer (already has "~/.local/share" or other xdg data directory)
                // with the directory we want to backup the config to, create the directory
                // if it doesn't exist, and then append with the filename we want to backup
                // to config in.
                _extend_string( &buffer, "/dwm/" );
                _make_directory_path( buffer );
                _extend_string( &buffer, "dwm_last.conf" );

                if ( config_write_file( libconfig_config, buffer ) == CONFIG_FALSE ) {
                        log_error( "Problem backing up current config to \"%s\"\n", buffer );
                } else {
                        log_info( "Current config backed up to \"%s\"\n", buffer );
                }

                SAFE_FREE( buffer );
        }
}

/**
 * @brief Loads default values from @ref config.h into a @ref Configuration struct.
 *
 * Initializes the given @ref Configuration struct with default values from
 * @ref config.h and other misc hardcoded default values. This is intended to
 * proceed any use of the configuration to ensure consistent behavior.
 *
 * @param[in,out] config Pointer to the @ref Configuration struct to load default values
 * into. If the pointer is NULL, the program will exit with a failure exit code.
 *
 * @note This function is an exit point in the program. If @p config
 * is NULL, the program will be unable to continue. Later attempts to access
 * values in @p config would just cause the program to either enter
 * undefined behavior or crash. Instead, the program will log the fatal error
 * and return a failure exit code.
 *
 * @todo Replace current colors array with a better solution. It should follow
 * a similar system to that of the other rules, keys, and buttons arrays, in that
 * it uses the original arrays from @ref config.h alongside a boolean to keep track
 * of the this until it is later dynamically allocated in the parser.
 */
static void _load_default_config( Configuration *config ) {

        if ( config == NULL ) {
                log_fatal( "config is NULL, can't load default configuration\n" );
                exit( EXIT_FAILURE );
        }

        config->max_keys = 4;
        config->fallback_config_loaded = false;

        config->default_rules_loaded = true;
        config->rule_array_size = LENGTH( rules );
        config->rule_array = (Rule *) rules;

        config->default_keybinds_loaded = true;
        config->keybind_array_size = LENGTH( keys );
        config->keybind_array = (Key *) keys;

        config->default_buttonbinds_loaded = true;
        config->buttonbind_array_size = LENGTH( buttons );
        config->buttonbind_array = (Button *) buttons;

        // This is a bit lazy, but simplifies cleanup logic.
        // We dynamically allocate all the values here so they
        // can universally be freed instead of having to keep
        // track of which are dynamic and which are static.
        fonts[ 0 ] = _estrdup( fonts[ 0 ] );
        for ( int i = 0; i < LENGTH( colors ); i++ ) {
                for ( int j = 0; j < LENGTH( colors[ i ] ); j++ ) {
                        colors[ i ][ j ] = _estrdup( colors[ i ][ j ] );
                }
        }

        config_init( &config->libconfig_config );
}

/**
 * @brief Attempts to find, open, and store a valid libconfig configuration file.
 *
 * This function handles most of the high level logic regarding finding, opening,
 * validating, and storing a libconfig configuration file. This function will first
 * search for a custom configuration file, passed in using @p config. If that is not
 * found, a list of default directories are searched. If still no user configuration
 * is found, a user's last configuration backup will be attempted to be loaded. If again
 * unsuccessful, a system default configuration file will be loaded. Finally, if all else
 * fails, the function will return and the program will rely on the hardcoded default values
 * loaded during @ref _load_default_config().
 *
 * @param[in,out] config Pointer to the @ref Configuration. From @p config, we read
 * the value of @ref Configuration::config_filepath to see if the user passed in a
 * custom configuration filepath from the cli. In @p config, we also store the filepath
 * and libconfig @ref config_t if we find and parse a valid configuration file.
 *
 * @return 0 on success, -1 on failure.
 */
static int _open_config( Configuration *config ) {

        int i, config_filepaths_length = 0;

        // Yes this uses a "magic number", but I didn't see a way that was less
        // cumbersome than just hardcoding this value. 5 is just the 1 CLI custom
        // filepath + 4 fallback config locations created a few lines later.
        char *config_filepaths[ 5 ];

        // Check if a custom user config was passed through the CLI.
        // If a path was given, copy it to the first index of the filepaths array.
        if ( config->config_filepath != NULL ) {
                config_filepaths[ config_filepaths_length++ ] = _estrdup( config->config_filepath );
                SAFE_FREE( config->config_filepath );
        }

        // $XDG_CONFIG_HOME/.config/dwm.conf or $HOME/.config/dwm.conf
        char *config_top_directory = _get_xdg_config_home();
        if ( !config_top_directory ) {
                log_warn( "Unable to acquire top level configuration directory\n" );
        } else {
                _extend_string( &config_top_directory, "/dwm.conf" );
                config_filepaths[ config_filepaths_length++ ] = config_top_directory;
        }

        // $XDG_CONFIG_HOME/.config/dwm/dwm.conf or $HOME/.config/dwm/dwm.conf
        char *config_sub_directory = _get_xdg_config_home();
        if ( !config_sub_directory ) {
                log_warn( "Unable to acquire dwm configuration directory\n" );
        } else {
                _extend_string( &config_sub_directory, "/dwm/dwm.conf" );
                config_filepaths[ config_filepaths_length++ ] = config_sub_directory;
        }

        // $XDG_DATA_HOME/.local/share/dwm/dwm_last.conf or $HOME/.local/share/dwm/dwm_last.conf
        char *config_backup = _get_xdg_data_home();
        if ( !config_backup ) {
                log_warn( "Unable to acquire dwm configuration backup directory\n" );
        } else {
                _extend_string( &config_backup, "/dwm/dwm_last.conf" );
                config_filepaths[ config_filepaths_length++ ] = config_backup;
        }

        // /etc/dwm/dwm.conf
        char *config_fallback = _estrdup( "/etc/dwm/dwm.conf" );
        if ( !config_fallback ) {
                log_warn( "Unable to acquire dwm system configuration fallback directory\n" );
        } else {
                config_filepaths[ config_filepaths_length++ ] = config_fallback;
        }

        FILE *tmp_file = NULL;
        for ( i = 0; i < config_filepaths_length; i++ ) {
                log_debug( "Attempting to open config file \"%s\"\n", config_filepaths[ i ] );

                if ( config_filepaths[ i ] == NULL ) {
                        log_warn( "config_filepaths[%d] was NULL, unable to lookup intended config. Likely a memory allocation error\n", i );
                        continue;
                }

                tmp_file = fopen( config_filepaths[ i ], "r" );

                if ( tmp_file == NULL ) {
                        log_warn( "Unable to open config file \"%s\"\n", config_filepaths[ i ] );
                        continue;
                }

                if ( config_read( &config->libconfig_config, tmp_file ) == CONFIG_FALSE ) {
                        log_warn( "Problem parsing config file \"%s\", line %d: %s\n", config_filepaths[ i ], config_error_line( &config->libconfig_config ),
                                  config_error_text( &config->libconfig_config ) );
                        SAFE_FCLOSE( tmp_file );
                        continue;
                }

                // Save found config filepath
                SAFE_FREE( config->config_filepath );
                config->config_filepath = _estrdup( config_filepaths[ i ] );

                // Check if it's a user's custom configuration
                if ( strcmp( config_filepaths[ i ], config_backup ) == 0 || strcmp( config_filepaths[ i ], config_fallback ) == 0 ) {
                        config->fallback_config_loaded = true;
                }

                for ( i = 0; i < config_filepaths_length; i++ ) {
                        SAFE_FREE( config_filepaths[ i ] );
                }

                SAFE_FCLOSE( tmp_file );

                return 0;
        }

        log_error( "Unable to load any configs. Hardcoded default config values will be used. Exiting parsing\n" );

        for ( i = 0; i < config_filepaths_length; i++ ) {
                SAFE_FREE( config_filepaths[ i ] );
        }

        config_destroy( &config->libconfig_config );
        SAFE_FCLOSE( tmp_file );

        return -1;
}

/**
 * @brief Parses a string containing the argument data of a function.
 *
 * TODO
 *
 * @param[in] argument_string String containing the data to be parsed into @p parsed_arg.
 * @param[in] arg_type Enum describing the type of data stored in @p parsed_arg.
 * @param[in] range_min Minimum value @p parsed_arg can have. Only applies to numerical types.
 * @param[in] range_max Maximum value @p parsed_arg can have. Only applies to numerical types.
 * @param[out] parsed_arg Pointer to an @ref Arg struct where the parsed value of type @p arg_type
 * in range @p range_min to @p range_max from @p argument_string will be stored.
 *
 * @return 0 on success, -1 on failure.
 */
static int _parse_bind_argument( const char *argument_string, const enum Argument_Type arg_type, const long double range_min, const long double range_max, Arg *parsed_arg ) {

        log_trace( "Argument being parsed: \"%s\"\n", argument_string );

        if ( !argument_string || argument_string[ 0 ] == '\0' ) {
                log_error( "Null or empty string passed to parse_bind_argument()\n" );
                return -1;
        }

        // @formatter:off
        char *end_pointer;
        switch ( arg_type ) {
                case ARG_TYPE_INT:
                        parsed_arg->i = clamp_range_long( strtol( argument_string, &end_pointer, 10 ), (long) range_min, (long) range_max );
                        if ( *end_pointer != '\0' ) return -1;
                        log_trace( "Argument type int: %d\n", parsed_arg->i );
                        break;

                case ARG_TYPE_UINT:
                        parsed_arg->ui = clamp_range_ulong( strtoul( argument_string, &end_pointer, 10 ), (long) range_min, (long) range_max );
                        if ( *end_pointer != '\0' ) return -1;
                        log_trace( "Argument type unsigned int: %u\n", parsed_arg->ui );
                        break;

                case ARG_TYPE_FLOAT:
                        parsed_arg->f = clamp_range_float( strtof( argument_string, &end_pointer ), (float) range_min, (float) range_max );
                        if ( *end_pointer != '\0' ) return -1;
                        log_trace( "Argument type float: %f\n", parsed_arg->f );
                        break;

                case ARG_TYPE_POINTER:
                        parsed_arg->v = _estrdup( argument_string );
                        if ( !parsed_arg->v )return -1;
                        log_trace( "Argument type pointer (string): \"%s\", (pointer): %p\n", argument_string, parsed_arg->v );
                        break;

                case ARG_TYPE_NONE:
                        log_trace( "Argument type none\n" );
                        return 0;

                default:
                        log_error( "Unknown argument type during bind parsing: %d\n", arg_type );
                        return -1;
        }
        // @formatter:on

        return 0;
}

/**
 * @brief Parses a string containing the name of a function and all its necessary arguments.
 *
 * TODO
 *
 * @param[in] function_string String to parse the function from.
 * @param[out] parsed_function Pointer to where to store the pointer to the function parsed from @p function_string.
 * @param[out] parsed_arg_type Pointer to where to store the data type of the @ref Arg that can be passed to @p parsed_function.
 * @param[out] parsed_range_min Pointer to where to store the minimum value of the @ref Arg that can be passed to @p parsed_function.
 * @param[out] parsed_range_max Pointer to where to store the maximum value of the @ref Arg that can be passed to @p parsed_function.
 *
 * @return 0 on success, -1 on failure.
 */
static int _parse_bind_function( const char *function_string, void ( **parsed_function )( const Arg * ), enum Argument_Type *parsed_arg_type, long double *parsed_range_min,
                                 long double *parsed_range_max ) {

        const struct {
                const char *name;
                void ( *func )( const Arg * );
                const enum Argument_Type arg_type;
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
                        *parsed_function = function_alias_map[ i ].func;
                        *parsed_arg_type = function_alias_map[ i ].arg_type;
                        *parsed_range_min = function_alias_map[ i ].range_min;
                        *parsed_range_max = function_alias_map[ i ].range_max;
                        log_trace( "Function successfully parsed as %p\n", (void *) parsed_function );
                        return 0;
                }
        }

        return -1;
}

/**
 * @brief Parse a string containing the name of a modifier key.
 *
 * TODO
 *
 * @param[in] modifier_string String to parse the modifier from.
 * @param[out] parsed_modifier Pointer to where to the modifier mask value parsed from @p modifier_string.
 *
 * @return 0 on success, -1 on failure.
 *
 * @see https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/master/include/X11/X.h
 */
static int _parse_bind_modifier( const char *modifier_string, unsigned int *parsed_modifier ) {

        const struct {
                const char *name;
                const unsigned int mask;
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
        *parsed_modifier |= found_modifier;
        return 0;
}

/**
 * @brief Parse a string containing a complete buttonbind.
 *
 * TODO
 *
 * @param[in] buttonbind_string String to parse the buttonbind from.
 * @param[in] max_keys Maximum number of modifiers + buttons allowed in a buttonbind.
 * @param[out] parsed_buttonbind Pointer to @ref Button struct to store the buttonbind parsed from @p buttonbind_string.
 *
 * @return 0 on success, -1 on failure.
 */
static int _parse_buttonbind( const char *buttonbind_string, const unsigned int max_keys, Button *parsed_buttonbind ) {

        log_debug( "Buttonbind string to parse: \"%s\"\n", buttonbind_string );

        static char buttonbind_string_copy[ 512 ];
        snprintf( buttonbind_string_copy, LENGTH( buttonbind_string_copy ), "%s", buttonbind_string );

        char *modifier_token_list = strtok( buttonbind_string_copy, "," );

        char *click_token = strtok( NULL, "," );
        if ( click_token ) click_token = _trim_whitespace( click_token );

        char *function_token = strtok( NULL, "," );
        if ( function_token ) function_token = _trim_whitespace( function_token );

        char *argument_token = strtok( NULL, "," );
        if ( argument_token ) argument_token = _trim_whitespace( argument_token );

        if ( !modifier_token_list || !function_token || !click_token || modifier_token_list[ 0 ] == '\0' || function_token[ 0 ] == '\0' || click_token[ 0 ] == '\0' ) {
                log_error( "Invalid buttonbind string. Expected format: \"mod+key, click, function, arg (if necessary)\" and got \"%s\"\n", buttonbind_string );
                return -1;
        }

        // Split `modifier_token_list` into tokens
        unsigned int modifier_token_count = 0;
        char *trimmed_modifier_token_list[ max_keys ];
        memset( trimmed_modifier_token_list, 0, sizeof( trimmed_modifier_token_list ) );
        char *tmp_token = strtok( modifier_token_list, "+" );
        while ( tmp_token && modifier_token_count < max_keys ) {
                char *trimmed = _trim_whitespace( tmp_token );
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
                if ( _parse_bind_modifier( trimmed_modifier_token_list[ i ], &parsed_buttonbind->mask ) ) {
                        log_error( "Invalid modifier \"%s\" in buttonbind \"%s\"\n", trimmed_modifier_token_list[ i ], buttonbind_string );
                        return -1;
                }
        }

        if ( _parse_buttonbind_button( trimmed_modifier_token_list[ modifier_token_count - 1 ], &parsed_buttonbind->button ) ) {
                log_error( "Invalid button \"%s\" in buttonbind \"%s\"\n", function_token, buttonbind_string );
                return -1;
        }

        if ( _parse_buttonbind_click( click_token, &parsed_buttonbind->click ) ) {
                log_error( "Invalid click \"%s\" in buttonbind \"%s\"\n", function_token, buttonbind_string );
                return -1;
        }

        long double range_min, range_max;
        if ( _parse_bind_function( function_token, &parsed_buttonbind->func, (enum Argument_Type *) &parsed_buttonbind->argument_type, &range_min, &range_max ) ) {
                log_error( "Invalid function \"%s\" in buttonbind \"%s\"\n", function_token, buttonbind_string );
                return -1;
        }

        if ( parsed_buttonbind->argument_type != ARG_TYPE_NONE ) {
                if ( _parse_bind_argument( argument_token, parsed_buttonbind->argument_type, range_min, range_max, &parsed_buttonbind->arg ) ) {
                        log_error( "Invalid argument \"%s\" in buttonbind \"%s\"\n", argument_token, buttonbind_string );
                        return -1;
                }
        } else {
                log_trace( "Argument type none\n" );
        }

        return 0;
}

/**
 * @brief Parse a string containing the name of a button.
 *
 * TODO
 *
 * @param[in] button_string String to parse the button from.
 * @param[out] parsed_button Pointer to where to store the button value parsed from @p button_string.
 *
 * @return 0 on success, -1 on failure.
 *
 * @see https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/master/include/X11/X.h
 */
static int _parse_buttonbind_button( const char *button_string, unsigned int *parsed_button ) {

        const struct {
                const char *name;
                const int button;
        } button_alias_map[ ] = {
                { "leftclick", Button1 },
                { "left-click", Button1 },
                { "middleclick", Button2 },
                { "middle-click", Button2 },
                { "rightclick", Button3 },
                { "right-click", Button3 },
                { "scrollup", Button4 },
                { "scroll-up", Button4 },
                { "scrolldown", Button5 },
                { "scroll-down", Button5 },
        };

        log_trace( "Button string to parse: \"%s\"\n", button_string );
        for ( int i = 0; i < LENGTH( button_alias_map ); i++ ) {
                if ( strcasecmp( button_string, button_alias_map[ i ].name ) == 0 ) {
                        *parsed_button = button_alias_map[ i ].button;
                        log_trace( "Button successfully parsed as \"%s\" -> %d\n", button_alias_map[ i ].name, button_alias_map[ i ].button );
                        return 0;
                }
        }

        errno = 0;
        char *end_pointer = NULL;
        const unsigned long parsed_value = strtoul( button_string, &end_pointer, 10 );

        if ( errno != 0 || end_pointer == button_string || *end_pointer != '\0' ) return -1;
        if ( parsed_value < 1 || parsed_value > 255 ) return -1;

        *parsed_button = (unsigned int) parsed_value;

        log_trace( "Button successfully parsed as %d\n", *parsed_button );
        return 0;
}

/**
 * @brief Parse a string containing the name of a clickable element.
 *
 * TODO
 *
 * @param[in] click_string String to parse the clickable element from.
 * @param[out] parsed_click Pointer to where to store the clickable element value parsed from @p click_string.
 *
 * @return 0 on success, -1 on failure.
 */
static int _parse_buttonbind_click( const char *click_string, unsigned int *parsed_click ) {

        // @formatter:off
        const struct {
                const char *name;
                const int click;
        } click_alias_map[ ] = {
                { "tag", ClkTagBar },
                { "layout", ClkLtSymbol },
                { "status", ClkStatusText },
                { "title", ClkWinTitle },
                { "client", ClkClientWin },
                { "desktop", ClkRootWin },
        };
        // @formatter:on

        log_trace( "Click string to parse: \"%s\"\n", click_string );

        for ( int i = 0; i < LENGTH( click_alias_map ); i++ ) {
                if ( strcasecmp( click_string, click_alias_map[ i ].name ) == 0 ) {
                        *parsed_click = click_alias_map[ i ].click;
                        log_trace( "Click successfully parsed as \"%s\" -> %d\n", click_alias_map[ i ].name, click_alias_map[ i ].click );
                        return 0;
                }
        }

        return -1;
}

/**
 * @brief Parse a list of buttonbinds from a libconfig @ref config_t into a list of @ref Button structs.
 *
 * TODO
 *
 * @param[in] libconfig_config Pointer to the libconfig @ref config_t containing the buttonbinds to
 * be parsed into @p buttonbind_config.
 * @param[in] max_keys Maximum number of modifiers + buttons allowed in a buttonbind.
 * @param[out] buttonbind_config Pointer to where to dynamically allocate and store all the parsed buttonbinds.
 * @param[out] buttonbind_count Pointer to where to store the number of buttonbinds to be parsed.
 * This value does not take into account any failures during parsing the buttonbinds.
 * @param[out] default_buttonbinds_loaded Boolean to track if @p buttonbind_config has been dynamically
 * allocated yet or not, analogous to if the default buttonbinds are being used or not. Value is set
 * to `false` after allocation.
 *
 * @return 0 on success, >0 on failure.
 *
 * @note @p buttonbind_config can be dynamically allocated here. It will need to be freed later
 * if @p default_buttonbinds_loaded is `false`.
 *
 * @todo Overhaul the error return value. It sucks.
 */
static int _parse_buttonbinds_config( const config_t *libconfig_config, const unsigned int max_keys, Button **buttonbind_config, unsigned int *buttonbind_count, bool *default_buttonbinds_loaded ) {

        // I may look at adjusting how memory is allocated and used here. For example,
        // if a bind fails and is assigned. This just leaves empty unused memory. Not
        // the worst, as most users should not have many or any failing keybinds in
        // their config, certainly not enough to cause seriously egregious levels of
        // memory waste, but still something to consider.

        int failed_buttonbinds_count = 0;

        const config_setting_t *buttonbinds_libconfig_setting = config_lookup( libconfig_config, "buttonbinds" );
        if ( buttonbinds_libconfig_setting != NULL ) {
                *buttonbind_count = config_setting_length( buttonbinds_libconfig_setting );

                if ( *buttonbind_count == 0 ) {
                        log_warn( "No buttonbinds listed, minimal default buttonbinds will be used. Exiting buttonbind parsing\n" );
                        return 1;
                }

                log_debug( "Buttonbinds detected: %d\n", *buttonbind_count );

                // TODO: This probably shouldn't use ecalloc()
                *buttonbind_config = ecalloc( *buttonbind_count, sizeof( Button ) );
                *default_buttonbinds_loaded = false;

                const config_setting_t *buttonbind_libconfig_setting = NULL;
                for ( int i = 0; i < *buttonbind_count; i++ ) {
                        buttonbind_libconfig_setting = config_setting_get_elem( buttonbinds_libconfig_setting, i );

                        if ( buttonbind_libconfig_setting == NULL ) {
                                log_error( "Buttonbind element %d returned NULL, unable to parse\n", i + 1 );
                                failed_buttonbinds_count++;
                                continue;
                        }

                        if ( _parse_buttonbind( config_setting_get_string( buttonbind_libconfig_setting ), max_keys, &( *buttonbind_config )[ i ] ) == -1 ) {
                                ( *buttonbind_config )[ i ].button = 0;
                                failed_buttonbinds_count++;
                        }

                        buttonbind_libconfig_setting = NULL;
                }
                log_debug( "%d buttonbinds failed to be parsed\n", failed_buttonbinds_count );
        } else {
                log_error( "Problem reading config value \"buttonbinds\": Not found\n" );
                log_warn( "Default buttonbinds will be loaded. It is recommended you fix the config and reload dwm\n" );
        }

        return failed_buttonbinds_count;
}

/**
 * @brief Parse generic configuration settings from a libconfig @ref config_t.
 *
 * TODO
 *
 * @param[in] libconfig_config Pointer to the libconfig @ref config_t containing the generic settings to be parsed.
 * @param[out] max_keys Pointer to where to store the parsed value of the setting defining the max number of keys,
 * buttons, and modifiers allowed in a bind.
 *
 * @return 0 on success, >0 equals the number of failed settings.
 *
 * @note Author's note here on why @p max_keys is passed as an argument but no other settings value is.
 * Simply put, this was just to make the port/integration into base dwm easier. Originally, this function
 * just takes in @p libconfig_config and a struct containing all the program settings, but that isn't as
 * easily possible in base dwm here, which is why this function looks so strange now.
 */
static int _parse_generic_settings( const config_t *libconfig_config, unsigned int *max_keys ) {

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
                { "max-keys", max_keys, TYPE_UINT, true, 1, 10 },
        };

        log_debug( "Generic settings available: %lu\n", LENGTH( setting_map ) );

        int settings_failed_count = 0;
        for ( int i = 0; i < LENGTH( setting_map ); ++i ) {
                switch ( setting_map[ i ].type ) {
                        case TYPE_BOOL:
                                settings_failed_count -= _libconfig_lookup_bool( libconfig_config, setting_map[ i ].name, setting_map[ i ].optional, setting_map[ i ].value );
                                break;
                        case TYPE_INT:
                                settings_failed_count -= _libconfig_lookup_int( libconfig_config, setting_map[ i ].name, setting_map[ i ].optional, (int) setting_map[ i ].range_min,
                                                                                (int) setting_map[ i ].range_max, setting_map[ i ].value );
                                break;
                        case TYPE_UINT:
                                settings_failed_count -= _libconfig_lookup_uint( libconfig_config, setting_map[ i ].name, setting_map[ i ].optional, (unsigned int) setting_map[ i ].range_min,
                                                                                 (unsigned int) setting_map[ i ].range_max, setting_map[ i ].value );
                                break;
                        case TYPE_FLOAT:
                                settings_failed_count -= _libconfig_lookup_float( libconfig_config, setting_map[ i ].name, setting_map[ i ].optional, (float) setting_map[ i ].range_min,
                                                                                  (float) setting_map[ i ].range_max, setting_map[ i ].value );
                                break;
                        case TYPE_STRING:
                                settings_failed_count -= _libconfig_lookup_string( libconfig_config, setting_map[ i ].name, setting_map[ i ].optional, setting_map[ i ].value );
                                break;
                }
        }

        log_debug( "%d generic settings failed to be parsed\n", settings_failed_count );

        return settings_failed_count;
}

/**
 * @brief Parse a string containing a complete keybind.
 *
 * TODO
 *
 * @param[in] keybind_string String to parse the keybind from.
 * @param[in] max_keys Maximum number of modifiers + keys allowed in a keybind.
 * @param[out] parsed_keybind Pointer to @ref Key struct to store the keybind parsed from @p keybind_string.
 *
 * @return 0 on success, -1 on failure.
 */
static int _parse_keybind( const char *keybind_string, const unsigned int max_keys, Key *parsed_keybind ) {

        log_debug( "Keybind string to parse: \"%s\"\n", keybind_string );

        static char keybind_string_copy[ 512 ];
        snprintf( keybind_string_copy, LENGTH( keybind_string_copy ), "%s", keybind_string );

        char *modifier_token_list = strtok( keybind_string_copy, "," );

        char *function_token = strtok( NULL, "," );
        if ( function_token ) function_token = _trim_whitespace( function_token );

        char *argument_token = strtok( NULL, "," );
        if ( argument_token ) argument_token = _trim_whitespace( argument_token );

        if ( !modifier_token_list || !function_token || modifier_token_list[ 0 ] == '\0' || function_token[ 0 ] == '\0' ) {
                log_error( "Invalid keybind string. Expected format: \"mod+key, function, arg (if necessary)\" and got \"%s\"\n", keybind_string );
                return -1;
        }

        long double range_min, range_max;
        if ( _parse_bind_function( function_token, &parsed_keybind->func, (enum Argument_Type *) &parsed_keybind->argument_type, &range_min, &range_max ) ) {
                log_error( "Invalid function \"%s\" in keybind \"%s\"\n", function_token, keybind_string );
                return -1;
        }

        if ( parsed_keybind->argument_type != ARG_TYPE_NONE ) {
                if ( _parse_bind_argument( argument_token, parsed_keybind->argument_type, range_min, range_max, &parsed_keybind->arg ) ) {
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
                char *trimmed = _trim_whitespace( tmp_token );
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
                if ( _parse_bind_modifier( trimmed_modifier_token_list[ i ], &parsed_keybind->mod ) ) {
                        log_error( "Invalid modifier \"%s\" in keybind \"%s\"\n", trimmed_modifier_token_list[ i ], keybind_string );
                        return -1;
                }
        }

        if ( _parse_keybind_keysym( trimmed_modifier_token_list[ modifier_token_count - 1 ], &parsed_keybind->keysym ) ) {
                log_error( "Invalid keysym \"%s\" in keybind \"%s\"\n", trimmed_modifier_token_list[ modifier_token_count - 1 ], keybind_string );
                return -1;
        }

        return 0;
}

/**
 * @brief Parse a string containing the name of a keysym.
 *
 * TODO
 *
 * @param[in] keysym_string String to parse the keysym from.
 * @param[out] parsed_keysym Pointer to where to store the keysym value parsed from @p keysym_string.
 *
 * @return 0 on success, -1 on failure.
 *
 * @note `xev` is likely your best bet at finding the keysym values that will work with @ref XStringToKeysym().
 * If someone knows a better way, please reach out and let me know.
 *
 * @see https://gitlab.freedesktop.org/xorg/app/xev
 * @see https://gitlab.freedesktop.org/xorg/lib/libx11/-/blob/master/src/StrKeysym.c?ref_type=heads#L74
 */
static int _parse_keybind_keysym( const char *keysym_string, KeySym *parsed_keysym ) {

        log_trace( "Keysym being parsed: \"%s\"\n", keysym_string );

        *parsed_keysym = XStringToKeysym( keysym_string );
        if ( *parsed_keysym == NoSymbol ) return -1;

        // The upper case return of XConvertCase(), which we don't use.
        KeySym unused = 0;

        XConvertCase( *parsed_keysym, parsed_keysym, &unused );

        log_trace( "Keysym successfully parsed as parsed: \"%s\" -> 0x%lx\n", XKeysymToString( *parsed_keysym ), *parsed_keysym );
        return 0;
}

/**
 * @brief Parse a list of keybinds from a libconfig @ref config_t into a list of @ref Key structs.
 *
 * TODO
 *
 * @param[in] libconfig_config Pointer to the libconfig @ref config_t containing the keybinds to
 * be parsed into @p keybind_config.
 * @param[in] max_keys Maximum number of modifiers + keys allowed in a keybind.
 * @param[out] keybind_config Pointer to where to dynamically allocate memory and store all the parsed keybinds.
 * @param[out] keybinds_count Pointer to where to store the number of keybinds to be parsed.
 * This value does not take into account any failures during parsing the keybinds.
 * @param[out] default_keybinds_loaded Boolean to track if @p keybind_config has been dynamically
 * allocated yet or not, analogous to if the default keybinds are being used or not. Value is set
 * to `false` after allocation.
 *
 * @return 0 on success, >0 on failure.
 *
 * @note @p keybind_config can be dynamically allocated here. It will need to be freed later
 * if @p default_keybinds_loaded is `false`.
 *
 * @todo Overhaul the error return value. It sucks.
 */
static int _parse_keybinds_config( const config_t *libconfig_config, const unsigned int max_keys, Key **keybind_config, unsigned int *keybinds_count, bool *default_keybinds_loaded ) {

        // I may look at adjusting how memory is allocated and used here. For example,
        // if a bind fails and is assigned. This just leaves empty unused memory. Not
        // the worst, as most users should not have many or any failing keybinds in
        // their config, certainly not enough to cause seriously egregious levels of
        // memory waste, but still something to consider.

        int failed_keybinds = 0;

        const config_setting_t *keybinds_libconfig_setting = config_lookup( libconfig_config, "keybinds" );
        if ( keybinds_libconfig_setting != NULL ) {
                *keybinds_count = config_setting_length( keybinds_libconfig_setting );

                if ( *keybinds_count == 0 ) {
                        log_warn( "No keybinds listed, minimal default keybinds will be used. Exiting keybinds parsing\n" );
                        return 1;
                }

                log_debug( "Keybinds detected: %d\n", *keybinds_count );

                // TODO: This probably shouldn't use ecalloc()
                *keybind_config = ecalloc( *keybinds_count, sizeof( Key ) );
                *default_keybinds_loaded = false;

                const config_setting_t *keybind_libconfig_setting = NULL;
                for ( int i = 0; i < *keybinds_count; i++ ) {
                        keybind_libconfig_setting = config_setting_get_elem( keybinds_libconfig_setting, i );

                        if ( keybind_libconfig_setting == NULL ) {
                                log_error( "Keybind element %d returned NULL, unable to parse\n", i + 1 );
                                failed_keybinds++;
                                continue;
                        }

                        if ( _parse_keybind( config_setting_get_string( keybind_libconfig_setting ), max_keys, &( *keybind_config )[ i ] ) == -1 ) {
                                ( *keybind_config )[ i ].keysym = NoSymbol;
                                failed_keybinds++;
                        }

                        keybind_libconfig_setting = NULL;
                }
                log_debug( "%d keybinds failed to be parsed\n", failed_keybinds );
        } else {
                log_error( "Problem reading config value \"keybinds\": Not found\n" );
                log_warn( "Default keybinds will be loaded. It is recommended you fix the config and reload dwm\n" );
        }

        return failed_keybinds;
}

/**
 * @brief Parse a rule from a libconfig @ref config_setting_t.
 *
 * TODO
 *
 * @param[in] rule_libconfig_setting Pointer to the libconfig setting containing the rule to be parsed into @p parsed_rule.
 * @param[in] rule_index Index of the current rule in the larger array. Used purely for debug printing.
 * @param[out] parsed_rule Pointer to the @ref Rule struct where the values parsed from @p rule_libconfig_setting are stored.
 *
 * @note @p parsed_rule string struct members can be dynamically allocated and will need to be freed if not NULL.
 *
 * @return 0 on success, >0 equals the number of failed rule elements.
 */
static int _parse_rule( const config_setting_t *rule_libconfig_setting, const int rule_index, Rule *parsed_rule ) {

        int failed_rule_elements_count = 0;

        failed_rule_elements_count -= _parse_rule_string( rule_libconfig_setting, "class", rule_index, &parsed_rule->class );
        failed_rule_elements_count -= _parse_rule_string( rule_libconfig_setting, "instance", rule_index, &parsed_rule->instance );
        failed_rule_elements_count -= _parse_rule_string( rule_libconfig_setting, "title", rule_index, &parsed_rule->title );

        failed_rule_elements_count -= _libconfig_setting_lookup_uint( rule_libconfig_setting, "tag-mask", false, 0, TAGMASK, &parsed_rule->tags );
        failed_rule_elements_count -= _libconfig_setting_lookup_int( rule_libconfig_setting, "monitor", false, -1, 99, &parsed_rule->monitor );
        failed_rule_elements_count -= _libconfig_setting_lookup_int( rule_libconfig_setting, "floating", false, 0, 1, &parsed_rule->isfloating );

        log_debug( "Rule %d: class: \"%s\", instance: \"%s\", title: \"%s\", tag-mask: %d, monitor: %d, floating: %d\n", rule_index, parsed_rule->class, parsed_rule->instance, parsed_rule->title,
                   parsed_rule->tags, parsed_rule->monitor, parsed_rule->isfloating );

        return failed_rule_elements_count;
}

/**
 * @brief Parse a rule's string field value.
 *
 * TODO
 *
 * @param[in] rule_libconfig_setting Pointer to the libconfig setting containing the string to be parsed into @p parsed_value.
 * @param[in] path Path to the string value to be parsed from @p rule_libconfig_setting into @p parsed_value.
 * @param[in] rule_index Index of the current rule in the larger array. Used purely for debug printing.
 * @param[out] parsed_value Pointer to the string where the values parsed from @p rule_libconfig_setting is stored. This value
 * will either be NULL or a dynamically allocated string, which will need to be freed.
 *
 * @return 0 on success, -1 on failure.
 *
 * @note @p The string pointed to by @p parsed_value can be dynamically allocated and will need to be freed if not NULL.
 */
static int _parse_rule_string( const config_setting_t *rule_libconfig_setting, const char *path, const int rule_index, char **parsed_value ) {
        const char *tmp_string = NULL;

        _libconfig_setting_lookup_string( rule_libconfig_setting, path, false, &tmp_string );
        if ( tmp_string == NULL ) {
                log_error( "Problem parsing \"%s\" value of rule %d\n", path, rule_index + 1 );
                return -1;
        }

        if ( strcasecmp( tmp_string, "NULL" ) == 0 ) {
                *parsed_value = NULL;
        } else {
                *parsed_value = _estrdup( tmp_string );
                if ( *parsed_value == NULL ) {
                        log_error( "Out of memory copying \"%s\" into rule %d's %s field\n", tmp_string, rule_index + 1, path );
                        return -1;
                }
        }

        return 0;
}

/**
 * @brief Parse a list of rules from a libconfig @ref config_t into a list of @ref Rule structs.
 *
 * TODO
 *
 * @param[in] libconfig_config Pointer to the libconfig @ref config_t containing the rules to be parsed into @p rules_config.
 * @param[out] rules_config Pointer to where to dynamically allocate memory and store all the parsed rules.
 * @param[out] rules_count Pointer to where to store the number of rules to be parsed. This value does not take into account
 * any failures during parsing the rules.
 * @param[out] default_rules_loaded Boolean to track if @p rules_config has been dynamically allocated yet or not, analogous
 * to if the default rules are being used or not. Value is set to `false` after allocation.
 *
 * @return 0 on success, >0 on failure.
 *
 * @note @p rules_config is dynamically allocated here. It will need to be freed later.
 *
 * @todo Overhaul the error return value. It sucks.
 */
static int _parse_rules_config( const config_t *libconfig_config, Rule **rules_config, unsigned int *rules_count, bool *default_rules_loaded ) {

        int failed_rules_count = 0, failed_rules_elements_count = 0;

        const config_setting_t *rules_libconfig_setting = config_lookup( libconfig_config, "rules" );
        if ( rules_libconfig_setting != NULL ) {
                *rules_count = config_setting_length( rules_libconfig_setting );

                if ( *rules_count == 0 ) {
                        log_warn( "No rules listed, exiting rules parsing\n" );
                        return 0;
                }

                log_debug( "Rules detected: %d\n", *rules_count );

                // TODO: This probably shouldn't use ecalloc()
                *rules_config = ecalloc( *rules_count, sizeof( Rule ) );
                *default_rules_loaded = false;

                // Set some sane default values.
                // TODO: This may be best to separate into a helper function.
                for ( int i = 0; i < *rules_count; i++ ) {
                        ( *rules_config )[ i ].class = NULL;
                        ( *rules_config )[ i ].instance = NULL;
                        ( *rules_config )[ i ].title = NULL;
                        ( *rules_config )[ i ].tags = 0;
                        ( *rules_config )[ i ].isfloating = 0;
                        ( *rules_config )[ i ].monitor = -1;
                }

                const config_setting_t *rule_libconfig_setting = NULL;

                for ( int i = 0; i < *rules_count; i++ ) {
                        rule_libconfig_setting = config_setting_get_elem( rules_libconfig_setting, i );
                        if ( rule_libconfig_setting != NULL ) {
                                failed_rules_elements_count += _parse_rule( rule_libconfig_setting, i, &( *rules_config )[ i ] );
                        } else {
                                log_error( "Error parsing rule %d, unable to parse\n", i + 1 );
                                failed_rules_count++;
                        }
                }
        } else {
                log_error( "Problem reading config value \"rules\": Not found\n" );
                log_warn( "Default rules will be loaded. It is recommended you fix the config and reload dwm\n" );
                return 1;
        }

        log_debug( "%d rules failed to be parsed\n", failed_rules_count );
        log_debug( "Of those rules, %d rule elements failed to be parsed\n", failed_rules_elements_count );

        return failed_rules_count + failed_rules_elements_count;
}

/**
 * @brief Parse a list of tags from a libconfig @ref config_t.
 *
 * TODO
 *
 * @param[in] libconfig_config Pointer to the libconfig @ref config_t containing the tags to be parsed.
 *
 * @return 0 on success, >0 equals the number of tags that failed to be parsed.
 *
 * @note All successfully parsed tags are dynamically allocated into @p tags and will need to be freed.
 */
static int _parse_tags_config( const config_t *libconfig_config ) {

        int tags_failed_count = 0;

        const config_setting_t *tag_names_libconfig_setting = config_lookup( libconfig_config, "tag-names" );
        if ( tag_names_libconfig_setting != NULL ) {
                const char *tag_name = NULL;
                int tags_count = config_setting_length( tag_names_libconfig_setting );

                if ( tags_count == 0 ) {
                        log_warn( "No tag names detected while parsing config, default tag names will be used\n" );
                        return 0;
                }

                log_debug( "Tags detected: %d\n", tags_count );

                if ( tags_count > LENGTH( tags ) ) {
                        log_warn( "More than %lu tag names detected (%d were detected) while parsing config, only the first %lu will be used\n", LENGTH( tags ), tags_count, LENGTH( tags ) );
                        tags_count = LENGTH( tags );
                } else if ( tags_count < LENGTH( tags ) ) {
                        log_warn( "Less than %lu tag names detected while parsing config, filler tags will be used for the remainder\n", LENGTH( tags ) );
                }

                for ( int i = 0; i < tags_count; i++ ) {
                        tag_name = config_setting_get_string_elem( tag_names_libconfig_setting, i );

                        if ( tag_name == NULL ) {
                                log_error( "Problem reading tag array element %d: Value doesn't exist or isn't a string\n", i + 1 );
                                tags_failed_count++;
                                continue;
                        }

                        char fallback_tag_name[ 32 ];
                        snprintf( fallback_tag_name, sizeof( fallback_tag_name ), "%d", i + 1 );

                        tags[ i ] = _estrdup( tag_name );
                        if ( tags[ i ] == NULL ) {
                                log_error( "Failed while copying parsed tag %d\n", i );
                                tags[ i ] = _estrdup( fallback_tag_name );
                                tags_failed_count++;
                                continue;
                        }
                }
        } else {
                log_error( "Problem reading config value \"tag-names\": Not found\n" );
                log_warn( "Default tag names will be loaded. It is recommended you fix the config and reload dwm\n" );
                return 1;
        }

        log_debug( "%d tags failed to be parsed\n", tags_failed_count );

        return tags_failed_count;
}

/**
 * @brief Parse a theme from a libconfig @ref config_setting_t.
 *
 * TODO
 *
 * @param[in] theme_libconfig_setting Pointer to the libconfig setting containing the theme to be parsed.
 *
 * @return 0 on success, >0 equals the number of theme elements that failed to be parsed.
 *
 * @note All successfully parsed theme values are dynamically allocated and will need to be freed.
 */
static int _parse_theme( const config_setting_t *theme_libconfig_setting ) {

        const struct {
                const char *path;
                const char **value;
        } theme_map[ ] = {
                { "font", &fonts[ 0 ] },
                { "normal-foreground", &colors[ SchemeNorm ][ ColFg ] },
                { "normal-background", &colors[ SchemeNorm ][ ColBg ] },
                { "normal-border", &colors[ SchemeNorm ][ ColBorder ] },
                { "selected-foreground", &colors[ SchemeSel ][ ColFg ] },
                { "selected-background", &colors[ SchemeSel ][ ColBg ] },
                { "selected-border", &colors[ SchemeSel ][ ColBorder ] },
        };

        const char *tmp_string = NULL;
        int theme_elements_failed_count = 0;
        for ( int i = 0; i < LENGTH( theme_map ); i++ ) {
                if ( !_libconfig_setting_lookup_string( theme_libconfig_setting, theme_map[ i ].path, false, &tmp_string ) ) {
                        SAFE_FREE( *theme_map[ i ].value );
                        *theme_map[ i ].value = _estrdup( tmp_string );
                } else {
                        theme_elements_failed_count++;
                }
        }

        return theme_elements_failed_count;
}

/**
 * @brief Parse a list of themes from a libconfig @ref config_t.
 *
 * TODO
 *
 * @param[in] libconfig_config Pointer to the libconfig @ref config_t containing the theme to be parsed.
 *
 * @return 0 on success, >0 on failure.
 *
 * @todo Overhaul the error return value. It sucks.
 */
static int _parse_theme_config( const config_t *libconfig_config ) {

        int failed_themes_count = 0;
        int failed_theme_elements_count = 0;

        const config_setting_t *themes_libconfig_setting = config_lookup( libconfig_config, "themes" );
        if ( themes_libconfig_setting != NULL ) {

                int detected_theme_count = config_setting_length( themes_libconfig_setting );

                if ( detected_theme_count == 0 ) {
                        log_warn( "No themes detected while parsing config, the default theme will be used\n" );
                        return 0;
                }

                log_debug( "Themes detected: %d\n", detected_theme_count );

                if ( detected_theme_count > 1 ) {
                        log_warn( "More than 1 theme detected. dwm can only use the first theme in list \"themes\"\n" );
                        detected_theme_count = 1;
                }

                const config_setting_t *theme_libconfig_setting = NULL;

                for ( int i = 0; i < detected_theme_count; i++ ) {

                        theme_libconfig_setting = config_setting_get_elem( themes_libconfig_setting, i );

                        if ( theme_libconfig_setting == NULL ) {
                                log_error( "Theme %d returned NULL, unable to parse\n", i + 1 );
                                failed_themes_count++;
                                continue;
                        }

                        const unsigned int tmp = _parse_theme( theme_libconfig_setting );
                        failed_theme_elements_count += tmp;
                        log_debug( "%d elements failed to be parsed in theme number %d\n", tmp, i + 1 );
                }

                log_debug( "%d themes failed to be parsed\n", failed_themes_count );

        } else {
                log_error( "Problem reading config value \"themes\": Not found\n" );
                log_warn( "Default theme will be loaded. It is recommended you fix the config and reload dwm\n" );
                return 1;
        }

        return failed_themes_count + failed_theme_elements_count;
}

/// Utility function definitions ///

/**
 * @brief Extend one string with another.
 *
 * TODO
 *
 * @param[in,out] source_string_pointer Pointer to where the extended string will be stored. Must be NULL or heap allocated.
 * @param[in] addition Additional string to be appended to the string pointed to by @p source_string_pointer.
 *
 * @warning @p source_string_pointer must be NULL or heap allocated. If it points to a string
 * literal, @ref realloc() will crash the program. See @ref _join_strings() if you wish to append a string literal.
 *
 * @note @p source_string_pointer is or will be dynamically allocated on the heap and must be freed.
 *
 * @note This function is derived from [picom's](https://github.com/yshui/picom/) mstrextend().
 * Credit more or less goes to [Yuxuan Shui](yshuiv7@gmail.com), I just made some minor adjustments.
 *
 * @author Yuxuan Shui - <yshuiv7@gmail.com>
 *
 * @see https://github.com/yshui/picom
 * @see https://github.com/yshui/picom/blob/69539edc0638019e3dd88c67007e90ce7f51174e/src/utils/str.c#L54
 */
static void _extend_string( char **source_string_pointer, const char *addition ) {

        if ( !*source_string_pointer ) {
                *source_string_pointer = _estrdup( addition );
                return;
        }

        const size_t length_1 = strlen( *source_string_pointer );
        const size_t length_2 = strlen( addition );
        const size_t total_length = length_1 + length_2 + 1;

        *source_string_pointer = realloc( *source_string_pointer, total_length );

        strncpy( *source_string_pointer + length_1, addition, length_2 );
        ( *source_string_pointer )[ total_length - 1 ] = '\0';
}

/**
 * @brief Find a layout in @ref layouts that uses a specific arrange function.
 *
 * TODO
 *
 * @param[in] arrange Arrange function used by the layout to be found and returned.
 *
 * @return @ref Arg containing a pointer to the @ref Layout struct containing the
 * first instance of @p arrange.
 */
static Arg _find_layout( void ( *arrange )( Monitor * ) ) {

        for ( int i = 0; i < LENGTH( layouts ); i++ ) {
                if ( layouts[ i ].arrange == arrange ) {
                        return (Arg){ .v = &layouts[ i ] };
                }
        }

        return (Arg){ .i = 0 };
}

/**
 * @brief Get the user's XDG configuration directory path.
 *
 * TODO
 *
 * @return Pointer of a dynamically allocated string containing the complete path to the user's XDG
 * configuration directory, or NULL if no directory was able to be found.
 *
 * @note If the function is successful, the returned string will have been dynamically allocated
 * and will need to be freed.
 *
 * @note This function is derived from [picom's](https://github.com/yshui/picom/) xdg_config_home().
 * Credit more or less goes to [Yuxuan Shui](yshuiv7@gmail.com), I just made some minor adjustments.
 *
 * @author Yuxuan Shui - <yshuiv7@gmail.com>
 *
 * @see https://github.com/yshui/picom
 * @see https://github.com/yshui/picom/blob/69539edc0638019e3dd88c67007e90ce7f51174e/src/config.c#L33
 */
static char *_get_xdg_config_home( void ) {

        char *xdg_config_home = getenv( "XDG_CONFIG_HOME" );
        char *user_home = getenv( "HOME" );

        log_trace( "XDG_CONFIG_HOME: \"%s\", $HOME: \"%s\"\n", xdg_config_home, user_home );

        if ( !xdg_config_home ) {
                const char *default_config_directory = "/.config";

                if ( !user_home ) {
                        log_warn( "XDG_CONFIG_HOME and $HOME are not set\n" );
                        return NULL;
                }

                xdg_config_home = _join_strings( user_home, default_config_directory );
        } else {
                xdg_config_home = _estrdup( xdg_config_home );
        }

        return xdg_config_home;
}

/**
 * @brief Get the user's XDG data directory path.
 *
 * TODO
 *
 * @return Pointer of a dynamically allocated string containing the complete path to the user's XDG
 * data directory, or NULL if no directory was able to be found.
 *
 * @note TODO mention dynamic allocation with _join_strings() and _estrdup()
 *
 * @note This function is derived from [picom's](https://github.com/yshui/picom/) xdg_config_home().
 * Credit more or less goes to [Yuxuan Shui](yshuiv7@gmail.com), I just made some minor adjustments.
 *
 * @author Yuxuan Shui - <yshuiv7@gmail.com>
 *
 * @see https://github.com/yshui/picom
 * @see https://github.com/yshui/picom/blob/69539edc0638019e3dd88c67007e90ce7f51174e/src/config.c#L33
 */
static char *_get_xdg_data_home( void ) {

        char *xdg_data_home = getenv( "XDG_DATA_HOME" );
        char *user_home = getenv( "HOME" );

        log_trace( "$XDG_DATA_HOME: \"%s\", $HOME: \"%s\"\n", xdg_data_home, user_home );

        if ( !xdg_data_home ) {
                const char *default_data_directory = "/.local/share";

                if ( !user_home ) {
                        log_warn( "$XDG_DATA_HOME and $HOME are not set\n" );
                        return NULL;
                }

                xdg_data_home = _join_strings( user_home, default_data_directory );
        } else {
                xdg_data_home = _estrdup( xdg_data_home );
        }

        return xdg_data_home;
}

/**
 * @brief Join two strings into one.
 *
 * TODO
 *
 * @param[in] string_1 String for @p string_2 to be appended to.
 * @param[in] string_2 String to be appended to @p string_1.
 *
 * @return Pointer of a dynamically allocated string containing the now joined strings,
 * or NULL on failure.
 *
 * @note Returned string is dynamically allocated and will need to be freed.
 *
 * @note This function is derived from [picom's](https://github.com/yshui/picom/) mstrjoin().
 * Credit more or less goes to [Yuxuan Shui](yshuiv7@gmail.com), I just made some minor adjustments.
 *
 * @note gcc warns about legitimate truncation worries in @ref strncpy() in @ref _join_strings().
 * `strncpy( joined_string, string_1, length_1 )` intentionally truncates the null byte
 * from @p string_1, however. `strncpy( joined_string + length_1, string_2, length_2 )`
 * uses bounds depending on the source argument, but `joined_string` is allocated with
 * `length_1 + length_2 + 1`, so this @ref strncpy() can't overflow.
 *
 * @author Yuxuan Shui - <yshuiv7@gmail.com>
 *
 * @see https://github.com/yshui/picom
 * @see https://github.com/yshui/picom/blob/69961987e1238f9bc3af53fa0774fc19fdec44a4/src/utils/str.c#L24
 */
static char *_join_strings( const char *string_1, const char *string_2 ) {

        const size_t length_1 = strlen( string_1 );
        const size_t length_2 = strlen( string_2 );
        const size_t total_length = length_1 + length_2 + 1;

        char *joined_string = calloc( total_length, sizeof( char ) );

        if ( !joined_string ) {
                log_error( "Calloc failed: %s\n", strerror(errno) );
                log_error( "Failed to join \"%s\" and \"%s\"\n", string_1, string_2 );
                return NULL;
        }

        #ifndef __clang__
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wpragmas"
        #pragma GCC diagnostic ignored "-Wstringop-truncation"
        #pragma GCC diagnostic ignored "-Wstringop-overflow"
        #endif

        strncpy( joined_string, string_1, length_1 );
        strncpy( joined_string + length_1, string_2, length_2 );

        #ifndef __clang__
        #pragma GCC diagnostic pop
        #endif

        joined_string[ total_length - 1 ] = '\0';

        return joined_string;
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
static int _libconfig_lookup_bool( const config_t *config, const char *path, const bool optional, bool *parsed_value ) {
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
static int _libconfig_lookup_int( const config_t *config, const char *path, const bool optional, const int range_min, const int range_max, int *parsed_value ) {
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
static int _libconfig_setting_lookup_int( const config_setting_t *setting, const char *path, const bool optional, const int range_min, const int range_max, int *parsed_value ) {
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
static int _libconfig_lookup_uint( const config_t *config, const char *path, const bool optional, const unsigned int range_min, const unsigned int range_max, unsigned int *parsed_value ) {
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
static int _libconfig_setting_lookup_uint( const config_setting_t *setting, const char *path, const bool optional, const unsigned int range_min, const unsigned int range_max,
                                           unsigned int *parsed_value ) {
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
static int _libconfig_lookup_float( const config_t *config, const char *path, const bool optional, const float range_min, const float range_max, float *parsed_value ) {
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
static int _libconfig_lookup_string( const config_t *config, const char *path, const bool optional, const char **parsed_value ) {
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
static int _libconfig_setting_lookup_string( const config_setting_t *setting, const char *path, const bool optional, const char **parsed_value ) {
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
 * @brief Creates all directories in a given path if they don't exist.
 *
 * TODO
 *
 * @param[in] path String containing the directory path to attempt to create.
 *
 * @return 0 on success, -1 on failure.
 *
 * @note This function is derived from [dwm-ipc's](https://github.com/mihirlad55/dwm-ipc) mkdirp().
 * Credit more or less goes to [Mihir Lad](mihirlad55@gmail.com), I just made some minor adjustments.
 *
 * @author Mihir Lad - <mihirlad55@gmail.com>
 *
 * @see https://github.com/mihirlad55/dwm-ipc
 * @see https://github.com/mihirlad55/dwm-ipc/blob/b3eebba7c043482d454afc5c882f513fc1b157ad/util.c#L103
 */
static int _make_directory_path( const char *path ) {

        char *normalized_path;
        _normalize_path( path, &normalized_path );

        const char *walk = normalized_path;
        const size_t normalized_path_length = strlen( normalized_path );

        while ( walk < normalized_path + normalized_path_length + 1 ) {

                // Get length from walk to next '/'
                const size_t distance_to_slash = strcspn( walk, "/" );

                // Skip path /
                if ( distance_to_slash == 0 ) {
                        walk++;
                        continue;
                }

                // Length of current path segment
                const size_t current_path_length = walk - normalized_path + distance_to_slash;
                char current_path[ current_path_length + 1 ];

                struct stat stat_variable;

                // Copy path segment to stat
                strncpy( current_path, normalized_path, current_path_length );
                strcpy( current_path + current_path_length, "" );

                errno = 0;
                if ( stat( current_path, &stat_variable ) < 0 ) {
                        if ( errno == ENOENT ) {
                                log_debug( "Making directory %s\n", current_path );
                                if ( mkdir( current_path, 0700 ) < 0 ) {
                                        log_error( "Failed to make directory \"%s\": %s\n", current_path, strerror( errno ) );
                                        free( normalized_path );
                                        return -1;
                                }
                        } else {
                                log_error( "Error stat-ing directory \"%s\": %s\n", current_path, strerror( errno ) );
                                free( normalized_path );
                                return -1;
                        }
                }

                // Continue to next path segment
                walk += distance_to_slash;
        }

        free( normalized_path );
        return 0;
}

/**
 * @brief Normalize a file or folder path to remove repeat forward slash characters.
 *
 * TODO
 *
 * @param[in] original_path String containing the path to be normalized.
 * @param[out] normalized_path Pointer to where to store the normalized path. It is dynamically
 * allocated and will need to be freed.
 *
 * @return TODO check for errors in realloc() and whatever replaces ecalloc() for allocating normalized_path
 *
 * @note @p normalized_path is dynamically allocated and will need to be freed.
 *
 * @note This function is derived from [dwm-ipc's](https://github.com/mihirlad55/dwm-ipc) normalizepath().
 * Credit more or less goes to [Mihir Lad](mihirlad55@gmail.com), I just made some minor adjustments.
 *
 * @author Mihir Lad - <mihirlad55@gmail.com>
 *
 * @see https://github.com/mihirlad55/dwm-ipc
 * @see https://github.com/mihirlad55/dwm-ipc/blob/b3eebba7c043482d454afc5c882f513fc1b157ad/util.c#L40
 */
static int _normalize_path( const char *original_path, char **normalized_path ) {

        const size_t original_length = strlen( original_path );

        // TODO: This probably shouldn't use ecalloc()
        *normalized_path = (char *) ecalloc( ( original_length + 1 ), sizeof( char ) );

        size_t new_length = 0;
        const char *match, *walk = original_path;
        while ( ( match = strchr( walk, '/' ) ) ) {

                // Copy everything between match and walk
                strncpy( *normalized_path + new_length, walk, match - walk );
                new_length += match - walk;
                walk += match - walk;

                // Skip all repeating slashes
                while ( *walk == '/' ) walk++;

                // If not last character in path
                if ( walk != original_path + original_length ) ( *normalized_path )[ new_length++ ] = '/';
        }

        ( *normalized_path )[ new_length++ ] = '\0';

        // Copy remaining path
        strcat( *normalized_path, walk );
        new_length += strlen( walk );

        *normalized_path = (char *) realloc( *normalized_path, new_length * sizeof( char ) );

        return 0;
}

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
static char *_estrdup( const char *string ) {
        if ( !string ) return NULL;
        char *return_string = strdup( string );
        if ( !return_string ) {
                log_error( "strdup failed: %s\n", strerror(errno) );
        }
        return return_string;
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
static char *_trim_whitespace( char *input_string ) {
        if ( !input_string ) return NULL;
        while ( isspace( (unsigned char) *input_string ) ) input_string++;
        if ( *input_string == '\0' ) return input_string;
        char *end = input_string + strlen( input_string ) - 1;
        while ( end > input_string && isspace( (unsigned char) *end ) ) end--;
        *( end + 1 ) = '\0';
        return input_string;
}

/// Warning macros ///
#define buttons buttons_ARRAY_REPLACED_WITH_dwm_config_buttons_array_BY_LIBCONFIG_PATCH
#define keys keys_ARRAY_REPLACED_WITH_dwm_config_keybind_array_BY_LIBCONFIG_PATCH
#define rules rules_ARRAY_REPLACED_WITH_dwm_config_rules_array_BY_LIBCONFIG_PATCH
