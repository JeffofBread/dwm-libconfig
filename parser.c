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
 * @authors Yuxuan Shui - <yshuiv7@gmail.com>, author of [picom](https://github.com/yshui/picom),
 * which I (JeffOfBread) copied code from. Code from picom is credited as such.
 * @authors Mihir Lad - <mihirlad55@gmail.com>, author of [dwm-ipc](https://github.com/mihirlad55/dwm-ipc),
 * which I (JeffOfBread) copied code from. Code from dwm-ipc is credited as such.
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
 * @todo Overhaul printing / logging to match the new error handling.
 * @todo Make sure function arguments are noted for being dynamically allocated in that function or its sub functions.
 * @todo It may be worth going back to having a header, this file is very heavy.
 */

#include <ctype.h>
#include <errno.h>
#include <libconfig.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

// Preprocessor string manipulation used in the logging macros
#define _TOSTRING( X ) #X
#define TOSTRING( X ) _TOSTRING( X )

// Uncomment as necessary to enable log printing for debugging.
// This is just a crude compatability macro between my own
// logging system, which I didn't want to bring over just
// for the config parser.
#define log_trace( ... ) //_log( "TRACE", __VA_ARGS__ )
#define log_debug( ... ) //_log( "DEBUG", __VA_ARGS__ )
#define log_info( ... ) _log( "INFO", __VA_ARGS__ )
#define log_warn( ... ) _log( "WARN", __VA_ARGS__ )
#define log_error( ... ) _log( "ERROR", __VA_ARGS__ )
#define log_fatal( ... ) _log( "FATAL", __VA_ARGS__ )
#define _log( LEVEL, ... ) fprintf( stdout, LEVEL ": [" __FILE__ "::" TOSTRING(__LINE__) "]: "  __VA_ARGS__ )

/**
 * TODO
 */
typedef enum Data_Type {
        TYPE_NONE = 0,
        TYPE_BOOLEAN,
        TYPE_INT,
        TYPE_UINT,
        TYPE_FLOAT,
        TYPE_STRING,
} Data_Type_t;

// String name pairs to the Data_Type enum
const char *DATA_TYPE_ENUM_STRINGS[ ] = { "None", "Boolean", "Int", "Unsigned Int", "Float", "String" };

// Enum to categorize the types of errors that
// can occur during parsing.
typedef enum Error {
        ERROR_NONE = 0,
        ERROR_NOT_FOUND,
        ERROR_TYPE,
        ERROR_RANGE,
        ERROR_NULL_VALUE,
        ERROR_ALLOCATION,
        ERROR_IO,
        ERROR_ENUM_LENGTH // Always must be last
} Error_t;

// String name pairs to the Error_t enum
const char *ERROR_ENUM_STRINGS[ ] = { "None", "Not found", "Invalid type", "Out of range", "Null value", "Failed to allocate memory", "I/O exception" };

// I would rather replace this with a more thorough
// error/exception handling system, but like the
// logging system, it just seems a bit outside
// the scope of the patch.
typedef struct Errors {
        unsigned int errors_count[ ERROR_ENUM_LENGTH - 1 ]; // -1 for ERROR_NONE
} Errors_t;

// Alias libconfig structs for better name
// separation from the parser's configuration struct
typedef config_t Libconfig_Config_t;
typedef config_setting_t Libconfig_Setting_t;

// Typedef used to abstract config array parsing functions.
// Allows for more generic parsing of multiple types.
typedef Errors_t ( *Array_Element_Parser_Function_t )( Libconfig_Setting_t *element_setting, unsigned int element_index, void *parsed_element );

// Struct to hold some parser internal data and
// some of the configuration data that can't
// be written to variables in `config.(def.).h`
typedef struct Parser_Config {
        bool rules_dynamically_allocated;
        bool keybinds_dynamically_allocated;
        bool buttonbinds_dynamically_allocated;
        bool fonts_dynamically_allocated;
        unsigned int rule_array_size;
        unsigned int buttonbind_array_size;
        unsigned int keybind_array_size;
        unsigned int fonts_array_size;
        char *config_filepath;
        Libconfig_Config_t libconfig_config;
} Parser_Config_t;

Parser_Config_t dwm_config = { 0 };

Key *keys = default_keys;
Button *buttons = default_buttons;
Rule *rules = default_rules;
const char **fonts = default_fonts;

// TODO: I should find a way to smoothen / harden the length checking of keys, buttons, and rules

/// Public parser functions ///
void config_cleanup( Parser_Config_t *config );
Errors_t parse_config( Parser_Config_t *config );

/// Public utility functions ///
// TODO: Some of these should be converted to use Error_t returns
void add_error( Errors_t *errors, Error_t error );
unsigned int count_errors( Errors_t errors );
char *estrdup( const char *string );
void extend_string( char **source_string_pointer, const char *addition );
Arg find_layout( void ( *arrange )( Monitor * ) );
char *get_xdg_config_home( void );
char *get_xdg_data_home( void );
char *join_strings( const char *string_1, const char *string_2 );
int make_directory_path( const char *path );
void merge_errors( Errors_t *destination, Errors_t source );
int normalize_path( const char *original_path, char **normalized_path );
void setlayout_floating( const Arg *arg );
void setlayout_monocle( const Arg *arg );
void setlayout_tiled( const Arg *arg );
void spawn_simple( const Arg *arg );

/// Parser internal functions ///
static Error_t _parser_backup_config( Libconfig_Config_t *libconfig_config );
static Error_t _parse_bind_argument( Libconfig_Setting_t *bind_setting, Data_Type_t argument_type, long double range_min, long double range_max, Arg *parsed_argument );
static Errors_t _parse_bind_core( Libconfig_Setting_t *bind_setting, unsigned int bind_index, unsigned int *parsed_modifier, void ( **parsed_function )( const Arg * ), Arg *parsed_argument,
                                  Data_Type_t *parsed_argument_type, const char *bind_array_path );
static Error_t _parse_bind_function( Libconfig_Setting_t *bind_setting, void ( **parsed_function )( const Arg * ), Data_Type_t *parsed_arg_type, long double *parsed_range_min, long double *parsed_range_max );
static Error_t _parse_bind_modifier( Libconfig_Setting_t *bind_setting, unsigned int *parsed_modifier );
static Errors_t _parse_buttonbind( Libconfig_Setting_t *buttonbind_setting, unsigned int buttonbind_index, Button *parsed_buttonbind );
static Errors_t _parse_buttonbind_adapter( Libconfig_Setting_t *buttonbind_setting, unsigned int buttonbind_index, void *parsed_keybind );
static Error_t _parse_buttonbind_button( Libconfig_Setting_t *buttonbind_setting, unsigned int *parsed_button );
static Error_t _parse_buttonbind_click( Libconfig_Setting_t *buttonbind_setting, unsigned int *parsed_click );
static Errors_t _parse_buttonbinds_config( const Libconfig_Config_t *libconfig_config, Button **buttonbind_config, unsigned int *buttonbinds_count, bool *buttonbinds_dynamically_allocated );
static Errors_t _parse_config_array( const Libconfig_Config_t *libconfig_config, Libconfig_Setting_t *libconfig_setting, const char *config_array_name, size_t element_struct_size,
                                     Array_Element_Parser_Function_t array_element_parser_function, bool *dynamically_allocated, void **parsed_config, unsigned int *parsed_config_length );
static Errors_t _parse_font( Libconfig_Setting_t *fonts_setting, unsigned int fonts_index, const char *parsed_font );
static Errors_t _parse_font_adapter( Libconfig_Setting_t *fonts_setting, unsigned int fonts_index, void *parsed_font );
static Errors_t _parse_generic_settings( const Libconfig_Config_t *libconfig_config );
static Errors_t _parse_keybind( Libconfig_Setting_t *keybind_setting, unsigned int keybind_index, Key *parsed_keybind );
static Errors_t _parse_keybind_adapter( Libconfig_Setting_t *keybind_setting, unsigned int keybind_index, void *parsed_keybind );
static Error_t _parse_keybind_keysym( Libconfig_Setting_t *keybind_setting, KeySym *parsed_keysym );
static Errors_t _parse_keybinds_config( const Libconfig_Config_t *libconfig_config, Key **keybind_config, unsigned int *keybinds_count, bool *keybinds_dynamically_allocated );
static void _parser_load_default_config( Parser_Config_t *config );
static Errors_t _parser_open_config( Parser_Config_t *config, bool *fallback_config_loaded );
static Error_t _parser_resolve_include_directory( Parser_Config_t *config );
static Errors_t _parse_rule( Libconfig_Setting_t *rule_libconfig_setting, int rule_index, Rule *parsed_rule );
static Errors_t _parse_rule_adapter( Libconfig_Setting_t *rule_setting, unsigned int rule_index, void *parsed_rule );
static Error_t _parse_rule_string( Libconfig_Setting_t *rule_libconfig_setting, const char *path, int rule_index, const char **parsed_value );
static Errors_t _parse_rules_config( const Libconfig_Config_t *libconfig_config, Rule **rules_config, unsigned int *rules_count, bool *rules_dynamically_allocated );
static Errors_t _parse_tag( Libconfig_Setting_t *tags_setting, unsigned int tags_index );
static Errors_t _parse_tags_adapter( Libconfig_Setting_t *tags_setting, unsigned int tags_index, void *unused );
static Errors_t _parse_tags_config( const Libconfig_Config_t *libconfig_config );
static Errors_t _parse_theme( Libconfig_Setting_t *theme_libconfig_setting, unsigned int theme_index );
static Errors_t _parse_theme_adapter( Libconfig_Setting_t *theme_setting, unsigned int theme_index, void *unused );
static Errors_t _parse_theme_config( const Libconfig_Config_t *libconfig_config );

/// Parser internal utility functions ///
static Error_t _libconfig_lookup_bool( const Libconfig_Config_t *config, const char *path, bool *parsed_value );
static Error_t _libconfig_lookup_float( const Libconfig_Config_t *config, const char *path, float range_min, float range_max, float *parsed_value );
static Error_t _libconfig_lookup_int( const Libconfig_Config_t *config, const char *path, int range_min, int range_max, int *parsed_value );
static Error_t _libconfig_lookup_string( const Libconfig_Config_t *config, const char *path, const char **parsed_value );
static Error_t _libconfig_lookup_uint( const Libconfig_Config_t *config, const char *path, unsigned int range_min, unsigned int range_max, unsigned int *parsed_value );
static Error_t _libconfig_setting_lookup_bool( Libconfig_Setting_t *setting, const char *path, bool *parsed_value );
static Error_t _libconfig_setting_lookup_float( Libconfig_Setting_t *setting, const char *path, float range_min, float range_max, float *parsed_value );
static Error_t _libconfig_setting_lookup_int( Libconfig_Setting_t *setting, const char *path, int range_min, int range_max, int *parsed_value );
static Error_t _libconfig_setting_lookup_string( Libconfig_Setting_t *setting, const char *path, const char **parsed_value );
static Error_t _libconfig_setting_lookup_uint( Libconfig_Setting_t *setting, const char *path, unsigned int range_min, unsigned int range_max, unsigned int *parsed_value );

/// Parser alias maps ///
const struct Function_Alias_Map {
        const char *name;
        void ( *func )( const Arg * );
        const Data_Type_t arg_type;
        const long double range_min, range_max;
} FUNCTION_ALIAS_MAP[ ] = {
        { "focusmon", focusmon, TYPE_INT, -99, 99 },
        { "focusstack", focusstack, TYPE_INT, -99, 99 },
        { "incnmaster", incnmaster, TYPE_INT, -99, 99 },
        { "killclient", killclient, TYPE_NONE },
        { "movemouse", movemouse, TYPE_NONE },
        { "quit", quit, TYPE_NONE },
        { "resizemouse", resizemouse, TYPE_NONE },
        { "setlayout-tiled", setlayout_tiled, TYPE_NONE },
        { "setlayout-floating", setlayout_floating, TYPE_NONE },
        { "setlayout-monocle", setlayout_monocle, TYPE_NONE },
        { "setlayout-toggle", setlayout, TYPE_NONE },
        { "setmfact", setmfact, TYPE_FLOAT, -0.95f, 1.95f },
        { "spawn", spawn_simple, TYPE_STRING },
        { "tag", tag, TYPE_INT, -1, TAGMASK },
        { "tagmon", tagmon, TYPE_INT, -99, 99 },
        { "togglebar", togglebar, TYPE_NONE },
        { "togglefloating", togglefloating, TYPE_NONE },
        { "toggletag", toggletag, TYPE_INT, -1, TAGMASK },
        { "toggleview", toggleview, TYPE_INT, -1, TAGMASK },
        { "view", view, TYPE_INT, -1, TAGMASK },
        { "zoom", zoom, TYPE_NONE },
};

const struct Modifier_Alias_Map {
        const char *name;
        const unsigned int mask;
} MODIFIER_ALIAS_MAP[ ] = {
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

// @formatter:off
const struct Click_Alias_Map{
        const char *name;
        const int click;
} CLICK_ALIAS_MAP[ ] = {
        { "tag", ClkTagBar },
        { "layout", ClkLtSymbol },
        { "status", ClkStatusText },
        { "title", ClkWinTitle },
        { "client", ClkClientWin },
        { "desktop", ClkRootWin },
};
// @formatter:on

const struct Button_Alias_Map {
        const char *name;
        const int button;
} BUTTON_ALIAS_MAP[ ] = {
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

const struct Setting_Alias_Map {
        const char *name;
        void *value;
        const Data_Type_t type;
        const bool optional;
        const long double range_min, range_max;
} SETTING_ALIAS_MAP[ ] = {

        // General
        { "showbar", &showbar, TYPE_BOOLEAN, true },
        { "topbar", &topbar, TYPE_BOOLEAN, true },
        { "resizehints", &resizehints, TYPE_BOOLEAN, true },
        { "lockfullscreen", &lockfullscreen, TYPE_BOOLEAN, true },
        { "borderpx", &borderpx, TYPE_UINT, true, 0, 9999 },
        { "snap", &snap, TYPE_UINT, true, 0, 9999 },
        { "nmaster", &nmaster, TYPE_UINT, true, 0, 99 },
        { "refreshrate", &refreshrate, TYPE_UINT, true, 0, 999 },
        { "mfact", &mfact, TYPE_FLOAT, true, 0.05f, 0.95f },
};

const struct Theme_Alias_Map {
        const char *path;
        const char **value;
} THEME_ALIAS_MAP[ ] = {
        { "normal-foreground", &colors[ SchemeNorm ][ ColFg ] },
        { "normal-background", &colors[ SchemeNorm ][ ColBg ] },
        { "normal-border", &colors[ SchemeNorm ][ ColBorder ] },
        { "selected-foreground", &colors[ SchemeSel ][ ColFg ] },
        { "selected-background", &colors[ SchemeSel ][ ColBg ] },
        { "selected-border", &colors[ SchemeSel ][ ColBorder ] },
};

/// Public parser functions ///

/**
 * @brief Frees all members of a @ref Parser_Config_t struct.
 *
 * Frees all members of a @ref Parser_Config_t struct. Intended
 * for use after @ref parse_config().
 *
 * @param[in,out] config Pointer to the @ref Parser_Config_t
 * struct to be cleaned.
 */
void config_cleanup( Parser_Config_t *config ) {

        if ( config->config_filepath != NULL ) {
                free( config->config_filepath );
        }

        if ( config->rules_dynamically_allocated == false ) {
                free( rules );
        }

        if ( config->keybinds_dynamically_allocated == false ) {
                free( keys );
        }

        if ( config->buttonbinds_dynamically_allocated == false ) {
                free( buttons );
        }

        if ( config->fonts_dynamically_allocated == false ) {
                free( fonts );
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
 * @param[in,out] config Pointer to the @ref Parser_Config_t struct. It is expected to be
 * a valid and mutable pointer to an already allocated struct.
 *
 * @return TODO
 *
 * @note This function is an exit point in the program. If @p config
 * is NULL, the program will be unable to continue. Later attempts to access
 * values in @p config would just cause the program to either enter
 * undefined behavior or crash. Instead, the program will log the fatal error
 * and return a failure exit code.
 *
 * @authors JeffOfBread <jeffofbreadcoding@gmail.com>
 *
 * @see https://github.com/JeffofBread/dwm-libconfig
 */
Errors_t parse_config( Parser_Config_t *config ) {

        Errors_t errors = { 0 };

        if ( config == NULL ) {
                log_fatal( "Unable to begin configuration parsing. Pointer to config is NULL\n" );
                exit( EXIT_FAILURE );
        }

        _parser_load_default_config( config );

        bool fallback_config_loaded = false;
        merge_errors( &errors, _parser_open_config( config, &fallback_config_loaded ) );

        // Exit the parser if we haven't acquired a configuration file.
        // Without a configuration file, there isn't a reason to continue parsing.
        // The program will have to rely on the hardcoded default values instead.
        if ( config->config_filepath == NULL ) return errors;

        log_info( "Path to config file: \"%s\"\n", config->config_filepath );

        add_error( &errors, _parser_resolve_include_directory( config ) );

        config_set_options( &config->libconfig_config, CONFIG_OPTION_AUTOCONVERT | CONFIG_OPTION_SEMICOLON_SEPARATORS );
        config_set_tab_width( &config->libconfig_config, 4 );

        merge_errors( &errors, _parse_generic_settings( &config->libconfig_config ) );
        merge_errors( &errors, _parse_keybinds_config( &config->libconfig_config, &keys, &config->keybind_array_size, &config->keybinds_dynamically_allocated ) );
        merge_errors( &errors, _parse_buttonbinds_config( &config->libconfig_config, &buttons, &config->buttonbind_array_size, &config->buttonbinds_dynamically_allocated ) );
        merge_errors( &errors, _parse_rules_config( &config->libconfig_config, &rules, &config->rule_array_size, &config->rules_dynamically_allocated ) );
        merge_errors( &errors, _parse_tags_config( &config->libconfig_config ) );
        merge_errors( &errors, _parse_theme_config( &config->libconfig_config ) );

        // The error requirement being 0 may be a bit strict, I am not sure. May need
        // some relaxing or possibly come up with a better way of calculating if a config
        // passes, or is valid enough to warrant backing up.

        // TODO: This logic is clumsily structured, it should be improved. It also probably should include config->rules_dynamically_allocated
        if ( count_errors( errors ) == 0 && config->keybinds_dynamically_allocated && config->buttonbinds_dynamically_allocated && !fallback_config_loaded ) {
                const Error_t backup_error = _parser_backup_config( &config->libconfig_config );
                add_error( &errors, backup_error );
        } else {
                if ( !config->keybinds_dynamically_allocated || !config->buttonbinds_dynamically_allocated ) {
                        log_warn( "Not saving config as backup, as hardcoded default bind values were used, not the user's\n" );
                }
                if ( fallback_config_loaded ) {
                        log_warn( "Not saving config as backup, as the parsed configuration file is a system fallback configuration\n" );
                }
                if ( count_errors( errors ) != 0 ) {
                        log_warn( "Not saving config as backup, as the parsed config had too many (%d) errors\n", count_errors( errors ) );
                }
        }

        return errors;
}

/// Public utility functions ///

/**
 * @brief TODO
 *
 * TODO
 *
 * @param errors TODO
 * @param error TODO
 */
void add_error( Errors_t *errors, const Error_t error ) {
        if ( error > ERROR_NONE && error < ERROR_ENUM_LENGTH ) {
                errors->errors_count[ error - 1 ]++; // -1 for ERROR_NONE. We don't track them.
        }
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param errors TODO
 *
 * @return TODO
 */
unsigned int count_errors( const Errors_t errors ) {

        unsigned int count = 0;

        for ( int i = 0; i < LENGTH( errors.errors_count ); i++ ) {
                count += errors.errors_count[ i ];
        }

        return count;
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
char *estrdup( const char *string ) {
        if ( !string ) return NULL;
        char *return_string = strdup( string );
        if ( !return_string ) {
                log_error( "strdup failed to copy \"%s\": %s\n", string, strerror(errno) );
                errno = 0;
        }
        return return_string;
}

/**
 * @brief Extend one string with another.
 *
 * TODO
 *
 * @param[in,out] source_string_pointer Pointer to where the extended string will be stored. Must be NULL or heap allocated.
 * @param[in] addition Additional string to be appended to the string pointed to by @p source_string_pointer.
 *
 * @warning @p source_string_pointer must be NULL or heap allocated. If it points to a string
 * literal, @ref realloc() will crash the program. See @ref join_strings() if you wish to append a string literal.
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
void extend_string( char **source_string_pointer, const char *addition ) {

        if ( !*source_string_pointer ) {
                *source_string_pointer = estrdup( addition );
                return;
        }

        const size_t length_1 = strlen( *source_string_pointer );
        const size_t length_2 = strlen( addition );
        const size_t total_length = length_1 + length_2 + 1;

        // TODO: Check for realloc errors
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
Arg find_layout( void ( *arrange )( Monitor * ) ) {

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
char *get_xdg_config_home( void ) {

        char *xdg_config_home = getenv( "XDG_CONFIG_HOME" );
        char *user_home = getenv( "HOME" );

        log_debug( "XDG_CONFIG_HOME: \"%s\", HOME: \"%s\"\n", xdg_config_home, user_home );

        if ( !xdg_config_home ) {
                const char *default_config_directory = "/.config";

                if ( !user_home ) {
                        log_warn( "XDG_CONFIG_HOME and HOME are not set\n" );
                        return NULL;
                }

                xdg_config_home = join_strings( user_home, default_config_directory );
        } else {
                xdg_config_home = estrdup( xdg_config_home );
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
 * @note TODO mention dynamic allocation with join_strings() and estrdup()
 *
 * @note This function is derived from [picom's](https://github.com/yshui/picom/) xdg_config_home().
 * Credit more or less goes to [Yuxuan Shui](yshuiv7@gmail.com), I just made some minor adjustments.
 *
 * @author Yuxuan Shui - <yshuiv7@gmail.com>
 *
 * @see https://github.com/yshui/picom
 * @see https://github.com/yshui/picom/blob/69539edc0638019e3dd88c67007e90ce7f51174e/src/config.c#L33
 */
char *get_xdg_data_home( void ) {

        char *xdg_data_home = getenv( "XDG_DATA_HOME" );
        char *user_home = getenv( "HOME" );

        log_debug( "XDG_DATA_HOME: \"%s\", HOME: \"%s\"\n", xdg_data_home, user_home );

        if ( !xdg_data_home ) {
                const char *default_data_directory = "/.local/share";

                if ( !user_home ) {
                        log_warn( "XDG_DATA_HOME and HOME are not set\n" );
                        return NULL;
                }

                xdg_data_home = join_strings( user_home, default_data_directory );
        } else {
                xdg_data_home = estrdup( xdg_data_home );
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
 * @note gcc warns about legitimate truncation worries in @ref strncpy() in @ref join_strings().
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
char *join_strings( const char *string_1, const char *string_2 ) {

        const size_t length_1 = strlen( string_1 );
        const size_t length_2 = strlen( string_2 );
        const size_t total_length = length_1 + length_2 + 1;

        char *joined_string = calloc( total_length, sizeof( char ) );

        if ( !joined_string ) {
                log_error( "Calloc failed trying to join \"%s\" and \"%s\": %s\n", string_1, string_2, strerror(errno) );
                errno = 0;
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
int make_directory_path( const char *path ) {

        char *normalized_path = NULL;

        if ( normalize_path( path, &normalized_path ) == -1 ) {
                log_error( "Unable to make directory path because path normalization failed" );
                return -1;
        }

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
 * @brief TODO
 *
 * TODO
 *
 * @param destination TODO
 * @param source TODO
 *
 * @return TODO
 */
void merge_errors( Errors_t *destination, const Errors_t source ) {
        for ( int i = 0; i < LENGTH( destination->errors_count ); i++ ) {
                destination->errors_count[ i ] += source.errors_count[ i ];
        }
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
 * @return TODO
 *
 * @note @p normalized_path can be dynamically allocated and will need to be freed.
 *
 * @note This function is derived from [dwm-ipc's](https://github.com/mihirlad55/dwm-ipc) normalizepath().
 * Credit more or less goes to [Mihir Lad](mihirlad55@gmail.com), I just made some minor adjustments.
 *
 * @author Mihir Lad - <mihirlad55@gmail.com>
 *
 * @see https://github.com/mihirlad55/dwm-ipc
 * @see https://github.com/mihirlad55/dwm-ipc/blob/b3eebba7c043482d454afc5c882f513fc1b157ad/util.c#L40
 */
int normalize_path( const char *original_path, char **normalized_path ) {

        const size_t original_length = strlen( original_path );

        *normalized_path = calloc( ( original_length + 1 ), sizeof( char ) );

        if ( *normalized_path == NULL ) {
                log_error( "Calloc failed trying to allocate %lu bytes: %s\n", ( original_length + 1 ) * sizeof( char ), strerror(errno) );
                return -1;
        }

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
 * @brief Set current layout to floating.
 *
 * This is a wrapper for the @ref setlayout() function. It sets the
 * currently focused monitor's layout to the floating layout found
 * in the @ref layouts array.
 *
 * @param arg Unused.
 */
void setlayout_floating( const Arg *arg ) {

        const Arg tmp = find_layout( NULL );
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

        const Arg tmp = find_layout( monocle );
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

        const Arg tmp = find_layout( tile );
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

/// Parser internal functions ///

/**
 * @brief Backs up a libconfig configuration to disk.
 *
 * This function backs up the given libconfig @ref Libconfig_Config_t
 * @p libconfig_config following the XDG specification. If
 * @ref get_xdg_data_home() fails to find the XDG data directory
 * (which is likely), we will default to using "~/.local/share/"
 * instead. Meaning that, in the latter case, the filepath to
 * the backup file will be "~/.local/share/dwm/dwm_last.conf".
 * "/dwm/dwm_last.conf" will always be appended to the end of
 * whatever path is returned by @ref get_xdg_data_home().
 * The backup file is then created and written to by libconfig's
 * config_write_file().
 *
 * @param[in] libconfig_config Pointer to the libconfig configuration
 * to be backed up.
 *
 * @return TODO
 *
 * @todo This function may need a little TLC when it comes to logging
 */
static Error_t _parser_backup_config( Libconfig_Config_t *libconfig_config ) {

        // Save xdg data directory to buffer (~/.local/share)
        char *buffer = get_xdg_data_home();

        if ( buffer == NULL ) {
                log_error( "Unable to get necessary directory to backup config\n" );
                return ERROR_NOT_FOUND;
        }

        // Append buffer (already has "~/.local/share" or other xdg data directory)
        // with the directory we want to backup the config to, create the directory
        // if it doesn't exist, and then append with the filename we want to backup
        // to config in.
        extend_string( &buffer, "/dwm/" );
        if ( buffer == NULL ) return ERROR_NULL_VALUE;

        if ( make_directory_path( buffer ) != 0 ) return ERROR_IO;

        extend_string( &buffer, "dwm_last.conf" );
        if ( buffer == NULL ) return ERROR_NULL_VALUE;

        if ( config_write_file( libconfig_config, buffer ) == CONFIG_FALSE ) {
                log_error( "Problem backing up current config to \"%s\"\n", buffer );
                free( buffer );
                return ERROR_IO;
        }

        log_info( "Current config backed up to \"%s\"\n", buffer );
        free( buffer );

        return ERROR_NONE;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] bind_setting TODO
 * @param[in] argument_type Enum describing the type of data stored in @p parsed_argument.
 * @param[in] range_min Minimum value @p parsed_argument can have. Only applies to numerical types.
 * @param[in] range_max Maximum value @p parsed_argument can have. Only applies to numerical types.
 * @param[out] parsed_argument Pointer to an @ref Arg struct where the parsed value of type @p argument_type
 * in range @p range_min to @p range_max will be stored.
 *
 * @return TODO
 */
static Error_t _parse_bind_argument( Libconfig_Setting_t *bind_setting, const Data_Type_t argument_type, const long double range_min, const long double range_max, Arg *parsed_argument ) {

        // @formatter:off
        Error_t lookup_error = ERROR_NONE;
        switch ( argument_type ) {
                case TYPE_NONE:
                        return ERROR_NONE;

                case TYPE_BOOLEAN:
                        lookup_error = _libconfig_setting_lookup_bool( bind_setting, "argument", (bool *) &parsed_argument->ui );
                        break;

                case TYPE_INT: {
                        lookup_error = _libconfig_setting_lookup_int( bind_setting, "argument", range_min, range_max, &parsed_argument->i );
                        break;
                }

                case TYPE_UINT: {
                        lookup_error = _libconfig_setting_lookup_uint( bind_setting, "argument", range_min, range_max, &parsed_argument->ui );
                        break;
                }

                case TYPE_FLOAT: {
                        lookup_error = _libconfig_setting_lookup_float( bind_setting, "argument", range_min, range_max, &parsed_argument->f );
                        break;
                }

                case TYPE_STRING: {
                        lookup_error = _libconfig_setting_lookup_string( bind_setting, "argument", (const char **) &parsed_argument->v );
                        break;
                }

                default:
                        log_error( "Unknown argument type during bind parsing: %d. Please reprogram to a valid type\n", argument_type );
                        return ERROR_TYPE;
        }
        // @formatter:on

        return lookup_error;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param bind_setting TODO
 * @param bind_index TODO
 * @param parsed_modifier TODO
 * @param parsed_function TODO
 * @param parsed_argument TODO
 * @param parsed_argument_type TODO
 * @param bind_array_path
 *
 * @return TODO
 */
static Errors_t _parse_bind_core( Libconfig_Setting_t *bind_setting, const unsigned int bind_index, unsigned int *parsed_modifier, void ( **parsed_function )( const Arg * ), Arg *parsed_argument,
                                  Data_Type_t *parsed_argument_type, const char *bind_array_path ) {

        Errors_t returned_errors = { 0 };

        // TODO: It would be nice to find a clean way to allow for no modifier at all in a bind, not just an empty string
        const Error_t modifier_error = _parse_bind_modifier( bind_setting, parsed_modifier );
        add_error( &returned_errors, modifier_error );

        if ( modifier_error != ERROR_NONE ) {
                log_error( "%s %d invalid, unable to parse bind's modifier: %s\n", bind_array_path, bind_index + 1, ERROR_ENUM_STRINGS[ modifier_error ] );
                return returned_errors;
        }

        long double range_min = 0, range_max = 0;
        const Error_t bind_error = _parse_bind_function( bind_setting, parsed_function, parsed_argument_type, &range_min, &range_max );
        add_error( &returned_errors, bind_error );

        if ( bind_error != ERROR_NONE ) {
                log_error( "%s %d invalid, unable to parse bind's function: %s\n", bind_array_path, bind_index + 1, ERROR_ENUM_STRINGS[ bind_error ] );
                return returned_errors;
        }

        const Error_t argument_error = _parse_bind_argument( bind_setting, *parsed_argument_type, range_min, range_max, parsed_argument );
        add_error( &returned_errors, argument_error );

        if ( argument_error != ERROR_NONE ) {
                log_error( "%s %d invalid, unable to parse bind's arguments: %s\n", bind_array_path, bind_index + 1, ERROR_ENUM_STRINGS[ argument_error ] );
                return returned_errors;
        }

        return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] bind_setting TODO
 * @param[out] parsed_function TODO
 * @param[out] parsed_arg_type Pointer to where to store the data type of the @ref Arg that can be passed to @p parsed_function.
 * @param[out] parsed_range_min Pointer to where to store the minimum value of the @ref Arg that can be passed to @p parsed_function.
 * @param[out] parsed_range_max Pointer to where to store the maximum value of the @ref Arg that can be passed to @p parsed_function.
 *
 * @return TODO
 */

static Error_t _parse_bind_function( Libconfig_Setting_t *bind_setting, void ( **parsed_function )( const Arg * ), Data_Type_t *parsed_arg_type, long double *parsed_range_min, long double *parsed_range_max ) {

        const char *function_string = NULL;
        const Error_t lookup_error = _libconfig_setting_lookup_string( bind_setting, "function", &function_string );

        if ( lookup_error != ERROR_NONE ) return lookup_error;

        for ( int i = 0; i < LENGTH( FUNCTION_ALIAS_MAP ); i++ ) {
                if ( strcasecmp( function_string, FUNCTION_ALIAS_MAP[ i ].name ) == 0 ) {
                        *parsed_function = FUNCTION_ALIAS_MAP[ i ].func;
                        *parsed_arg_type = FUNCTION_ALIAS_MAP[ i ].arg_type;
                        *parsed_range_min = FUNCTION_ALIAS_MAP[ i ].range_min;
                        *parsed_range_max = FUNCTION_ALIAS_MAP[ i ].range_max;
                        return ERROR_NONE;
                }
        }

        return ERROR_NOT_FOUND;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] bind_setting TODO
 * @param[out] parsed_modifier TODO
 *
 * @return TODO
 *
 * @see https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/master/include/X11/X.h
 */
static Error_t _parse_bind_modifier( Libconfig_Setting_t *bind_setting, unsigned int *parsed_modifier ) {

        const char *modifier_string = NULL;
        const Error_t lookup_error = _libconfig_setting_lookup_string( bind_setting, "modifier", &modifier_string );

        if ( lookup_error != ERROR_NONE ) return lookup_error;

        char *buffer = estrdup( modifier_string );

        if ( !buffer ) return ERROR_ALLOCATION;

        char *modifier_token = strtok( buffer, "+" );
        while ( modifier_token != NULL ) {

                while ( *modifier_token == ' ' || *modifier_token == '\t' ) modifier_token++;

                char *end = modifier_token + strlen( modifier_token ) - 1;
                while ( end > modifier_token && ( *end == ' ' || *end == '\t' ) ) {
                        *end = '\0';
                        end--;
                }

                bool found = false;
                for ( int i = 0; i < LENGTH( MODIFIER_ALIAS_MAP ); i++ ) {
                        if ( strcasecmp( modifier_token, MODIFIER_ALIAS_MAP[ i ].name ) == 0 ) {
                                *parsed_modifier |= MODIFIER_ALIAS_MAP[ i ].mask;
                                found = true;
                                break;
                        }
                }

                if ( !found ) {
                        log_error( "Invalid modifier: \"%s\"\n", modifier_token );
                        free( buffer );
                        return ERROR_NOT_FOUND;
                }

                modifier_token = strtok( NULL, "+" );
        }

        free( buffer );

        return ERROR_NONE;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] buttonbind_setting TODO
 * @param[in] buttonbind_index TODO
 * @param[out] parsed_buttonbind TODO
 *
 * @return TODO
 */
static Errors_t _parse_buttonbind( Libconfig_Setting_t *buttonbind_setting, const unsigned int buttonbind_index, Button *parsed_buttonbind ) {

        Errors_t returned_errors = { 0 };

        merge_errors( &returned_errors, _parse_bind_core( buttonbind_setting, buttonbind_index, &parsed_buttonbind->mask, &parsed_buttonbind->func, &parsed_buttonbind->arg,
                                                          (Data_Type_t *) &parsed_buttonbind->argument_type, "Buttonbind" ) );

        const Error_t button_error = _parse_buttonbind_button( buttonbind_setting, &parsed_buttonbind->button );
        add_error( &returned_errors, button_error );

        if ( button_error != ERROR_NONE ) {
                log_error( "Buttonbind %d invalid, unable to parse bind's button: %s\n", buttonbind_index + 1, ERROR_ENUM_STRINGS[ button_error ] );
                return returned_errors;
        }

        const Error_t click_error = _parse_buttonbind_click( buttonbind_setting, &parsed_buttonbind->click );
        add_error( &returned_errors, click_error );

        if ( click_error != ERROR_NONE ) {
                log_error( "Buttonbind %d invalid, unable to parse bind's click: %s\n", buttonbind_index + 1, ERROR_ENUM_STRINGS[ click_error ] );
                return returned_errors;
        }

        return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param buttonbind_setting TODO
 * @param buttonbind_index TODO
 * @param parsed_keybind TODO
 *
 * @return TODO
 */
static Errors_t _parse_buttonbind_adapter( Libconfig_Setting_t *buttonbind_setting, const unsigned int buttonbind_index, void *parsed_keybind ) {
        return _parse_buttonbind( buttonbind_setting, buttonbind_index, (Button *) parsed_keybind );
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param buttonbind_setting
 * @param[out] parsed_button TODO
 *
 * @return TODO
 *
 * @see https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/master/include/X11/X.h
 */
static Error_t _parse_buttonbind_button( Libconfig_Setting_t *buttonbind_setting, unsigned int *parsed_button ) {

        const char *button_string = NULL;
        const Error_t lookup_error = _libconfig_setting_lookup_string( buttonbind_setting, "button", &button_string );

        if ( lookup_error != ERROR_NONE ) return lookup_error;

        for ( int i = 0; i < LENGTH( BUTTON_ALIAS_MAP ); i++ ) {
                if ( strcasecmp( button_string, BUTTON_ALIAS_MAP[ i ].name ) == 0 ) {
                        *parsed_button = BUTTON_ALIAS_MAP[ i ].button;
                        return ERROR_NONE;
                }
        }

        errno = 0;
        char *end_pointer = NULL;
        const unsigned long parsed_value = strtoul( button_string, &end_pointer, 10 );

        if ( errno != 0 || end_pointer == button_string || *end_pointer != '\0' ) return ERROR_NOT_FOUND;

        // X11 button mask is only 8 bits
        if ( parsed_value < 1 || parsed_value > 255 ) return ERROR_RANGE;

        *parsed_button = (unsigned int) parsed_value;

        return ERROR_NONE;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] buttonbind_setting TODO
 * @param[out] parsed_click TODO
 *
 * @return TODO
 */
static Error_t _parse_buttonbind_click( Libconfig_Setting_t *buttonbind_setting, unsigned int *parsed_click ) {

        const char *click_string = NULL;
        const Error_t lookup_error = _libconfig_setting_lookup_string( buttonbind_setting, "click", &click_string );

        if ( lookup_error != ERROR_NONE ) return lookup_error;

        for ( int i = 0; i < LENGTH( CLICK_ALIAS_MAP ); i++ ) {
                if ( strcasecmp( click_string, CLICK_ALIAS_MAP[ i ].name ) == 0 ) {
                        *parsed_click = CLICK_ALIAS_MAP[ i ].click;
                        return ERROR_NONE;
                }
        }

        return ERROR_NOT_FOUND;
}

/**
 * @brief Parse a list of buttonbinds from a libconfig configuration into a list of @ref Button structs.
 *
 * TODO
 *
 * @param[in] libconfig_config Pointer to the libconfig configuration containing the buttonbinds to
 * be parsed into @p buttonbind_config.
 * @param[out] buttonbind_config Pointer to where to dynamically allocate and store all the parsed buttonbinds.
 * @param[out] buttonbinds_count Pointer to where to store the number of buttonbinds to be parsed.
 * This value does not take into account any failures during parsing the buttonbinds.
 * @param[out] buttonbinds_dynamically_allocated Boolean to track if @p buttonbind_config has been dynamically
 * allocated yet or not, analogous to if the default buttonbinds are being used or not. Value is set
 * to `false` after allocation.
 *
 * @return TODO
 *
 * @note TODO Maybe mention dynamic allocation in _parse_binds_config()?
 */
static Errors_t _parse_buttonbinds_config( const Libconfig_Config_t *libconfig_config, Button **buttonbind_config, unsigned int *buttonbinds_count, bool *buttonbinds_dynamically_allocated ) {
        return _parse_config_array( libconfig_config, NULL, "buttonbinds", sizeof( Button ), _parse_buttonbind_adapter, buttonbinds_dynamically_allocated, (void **) buttonbind_config, buttonbinds_count );
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param libconfig_config TODO
 * @param libconfig_setting TODO
 * @param config_array_name TODO
 * @param element_struct_size TODO
 * @param array_element_parser_function TODO
 * @param dynamically_allocated TODO
 * @param parsed_config TODO
 * @param parsed_config_length TODO
 *
 * @return TODO
 *
 * @todo It may be worth trying to add some kind of safeguard to fall back on default config if enough
 * elements fail to be parsed. For example, if the keybinds all (or many) fail, it could soft-lock the
 * user in the program. Not sure best way to do that, or if it is even the best idea to add.
 */
static Errors_t _parse_config_array( const Libconfig_Config_t *libconfig_config, Libconfig_Setting_t *libconfig_setting, const char *config_array_name, const size_t element_struct_size,
                                     const Array_Element_Parser_Function_t array_element_parser_function, bool *dynamically_allocated, void **parsed_config, unsigned int *parsed_config_length ) {

        Errors_t returned_errors = { 0 };

        if ( libconfig_config == NULL && libconfig_setting == NULL ) {
                log_error( "libconfig_config and libconfig_setting were both NULL. Cannot parse without a context, please fix programming error\n" );
                add_error( &returned_errors, ERROR_NULL_VALUE );
                return returned_errors;
        } else if ( libconfig_config != NULL && libconfig_setting != NULL ) {
                log_error( "libconfig_config and libconfig_setting were both given. It is unable to tell which context was intended to be searched, please fix programming error\n" );
                add_error( &returned_errors, ERROR_NULL_VALUE );
                return returned_errors;
        }

        const Libconfig_Setting_t *parent_setting = NULL;

        if ( libconfig_config != NULL ) {
                parent_setting = config_lookup( libconfig_config, config_array_name );
        } else if ( libconfig_setting != NULL ) {
                parent_setting = config_setting_lookup( libconfig_setting, config_array_name );
        }

        if ( parent_setting == NULL ) {
                log_error( "Problem reading config value \"%s\": Not found. Default %s will be loaded\n", config_array_name, config_array_name );
                add_error( &returned_errors, ERROR_NOT_FOUND );
                return returned_errors;
        }

        *parsed_config_length = config_setting_length( parent_setting );
        if ( *parsed_config_length == 0 ) {
                log_warn( "No %s listed. Default %s will be used\n", config_array_name, config_array_name );
                add_error( &returned_errors, ERROR_NOT_FOUND );
                return returned_errors;
        }

        log_debug( "%u %s detected\n", *parsed_config_length, config_array_name );

        // dynamically_allocated is also used to determine if we
        // should dynamically allocate the array or if it is already
        // allocated some other way, usually on the stack, as well
        // as report that it has been dynamically allocated here.
        if ( dynamically_allocated != NULL && parsed_config != NULL ) {
                log_debug( "Dynamically allocating %s\n", config_array_name );
                *parsed_config = calloc( *parsed_config_length, element_struct_size );
                if ( *parsed_config == NULL ) {
                        add_error( &returned_errors, ERROR_ALLOCATION );
                        return returned_errors;
                } else {
                        *dynamically_allocated = true;
                }
        }

        for ( unsigned int i = 0; i < *parsed_config_length; i++ ) {

                Libconfig_Setting_t *child_setting = config_setting_get_elem( parent_setting, i );

                if ( child_setting == NULL ) {
                        log_warn( "\"%s\" element number %u returned NULL\n", config_array_name, i + 1 );
                        add_error( &returned_errors, ERROR_NULL_VALUE );
                        continue;
                }

                // Yes, parsed_config can be NULL, that is intended. It is used by things like
                // theme or tag parsing that rely on global values.
                void *element = parsed_config ? (char *) ( *parsed_config ) + ( i * element_struct_size ) : NULL;

                const Errors_t parsing_error = array_element_parser_function( child_setting, i, element );

                if ( count_errors( parsing_error ) ) {
                        log_warn( "\"%s\" element number %d failed to be parsed. It had %d errors\n", config_array_name, i + 1, count_errors( parsing_error ) );
                        merge_errors( &returned_errors, parsing_error );
                        continue;
                }
        }

        return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param fonts_setting TODO
 * @param fonts_index TODO
 * @param parsed_font TODO
 *
 * @return TODO
 */
static Errors_t _parse_font( Libconfig_Setting_t *fonts_setting, const unsigned int fonts_index, const char *parsed_font ) {

        Errors_t returned_errors = { 0 };

        // TODO: Is there a better way that will give a better error return?
        parsed_font = config_setting_get_string( fonts_setting );

        if ( parsed_font == NULL ) {
                log_error( "Problem reading font element %d: Value doesn't exist or isn't a string\n", fonts_index + 1 );
                add_error( &returned_errors, ERROR_NULL_VALUE );
        }

        return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param fonts_setting TODO
 * @param fonts_index TODO
 * @param parsed_font TODO
 *
 * @return TODO
 */
static Errors_t _parse_font_adapter( Libconfig_Setting_t *fonts_setting, const unsigned int fonts_index, void *parsed_font ) {
        return _parse_font( fonts_setting, fonts_index, (const char *) parsed_font );
}

/**
 * @brief Parse generic configuration settings from a libconfig configuration.
 *
 * TODO
 *
 * @param[in] libconfig_config Pointer to the libconfig configuration containing the generic settings to be parsed.
 *
 * @return TODO
 */
static Errors_t _parse_generic_settings( const Libconfig_Config_t *libconfig_config ) {

        Errors_t returned_errors = { 0 };
        Error_t returned_error = { 0 };

        log_debug( "Generic settings available: %lu\n", LENGTH( SETTING_ALIAS_MAP ) );

        for ( int i = 0; i < LENGTH( SETTING_ALIAS_MAP ); ++i ) {
                switch ( SETTING_ALIAS_MAP[ i ].type ) {
                        case TYPE_BOOLEAN:
                                returned_error = _libconfig_lookup_bool( libconfig_config, SETTING_ALIAS_MAP[ i ].name, SETTING_ALIAS_MAP[ i ].value );
                                break;

                        case TYPE_INT:
                                returned_error = _libconfig_lookup_int( libconfig_config, SETTING_ALIAS_MAP[ i ].name, (int) SETTING_ALIAS_MAP[ i ].range_min, (int) SETTING_ALIAS_MAP[ i ].range_max,
                                                                        SETTING_ALIAS_MAP[ i ].value );
                                break;

                        case TYPE_UINT:
                                returned_error = _libconfig_lookup_uint( libconfig_config, SETTING_ALIAS_MAP[ i ].name, (unsigned int) SETTING_ALIAS_MAP[ i ].range_min,
                                                                         (unsigned int) SETTING_ALIAS_MAP[ i ].range_max, SETTING_ALIAS_MAP[ i ].value );
                                break;

                        case TYPE_FLOAT:
                                returned_error = _libconfig_lookup_float( libconfig_config, SETTING_ALIAS_MAP[ i ].name, (float) SETTING_ALIAS_MAP[ i ].range_min, (float) SETTING_ALIAS_MAP[ i ].range_max,
                                                                          SETTING_ALIAS_MAP[ i ].value );
                                break;

                        case TYPE_STRING:
                                returned_error = _libconfig_lookup_string( libconfig_config, SETTING_ALIAS_MAP[ i ].name, SETTING_ALIAS_MAP[ i ].value );
                                break;

                        default:
                                returned_error = ERROR_TYPE;
                                log_error( "Setting \"%s\" is programmed with an invalid type: \"%s\"\n", SETTING_ALIAS_MAP[ i ].name, DATA_TYPE_ENUM_STRINGS[ SETTING_ALIAS_MAP[ i ].type ] );
                                break;
                }

                add_error( &returned_errors, returned_error );

                if ( returned_error != ERROR_NONE && !SETTING_ALIAS_MAP[ i ].optional ) {
                        log_error( "Issue while parsing \"%s\": %s\n", SETTING_ALIAS_MAP[ i ].name, ERROR_ENUM_STRINGS[ returned_error ] );
                }
        }

        return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] keybind_setting TODO
 * @param[in] keybind_index TODO
 * @param[out] parsed_keybind TODO
 *
 * @return TODO
 */
static Errors_t _parse_keybind( Libconfig_Setting_t *keybind_setting, const unsigned int keybind_index, Key *parsed_keybind ) {

        Errors_t returned_errors = { 0 };

        merge_errors( &returned_errors, _parse_bind_core( keybind_setting, keybind_index, &parsed_keybind->mod, &parsed_keybind->func, &parsed_keybind->arg, (Data_Type_t *) &parsed_keybind->argument_type,
                                                          "Keybind" ) );

        const Error_t keysym_error = _parse_keybind_keysym( keybind_setting, &parsed_keybind->keysym );
        add_error( &returned_errors, keysym_error );

        if ( keysym_error != ERROR_NONE ) {
                log_error( "Keybind %d invalid, unable to parse bind's key: %s\n", keybind_index + 1, ERROR_ENUM_STRINGS[ keysym_error ] );
                return returned_errors;
        }

        return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param keybind_setting TODO
 * @param keybind_index TODO
 * @param parsed_keybind TODO
 *
 * @return TODO
 */
static Errors_t _parse_keybind_adapter( Libconfig_Setting_t *keybind_setting, const unsigned int keybind_index, void *parsed_keybind ) {
        return _parse_keybind( keybind_setting, keybind_index, (Key *) parsed_keybind );
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param keybind_setting TODO
 * @param[out] parsed_keysym Pointer to where to store the keysym value parsed from @p keysym_string.
 *
 * @return TODO
 *
 * @note `xev` is likely your best bet at finding the keysym values that will work with @ref XStringToKeysym().
 * If someone knows a better way, please reach out and let me know.
 *
 * @see https://gitlab.freedesktop.org/xorg/app/xev
 * @see https://gitlab.freedesktop.org/xorg/lib/libx11/-/blob/master/src/StrKeysym.c?ref_type=heads#L74
 */
static Error_t _parse_keybind_keysym( Libconfig_Setting_t *keybind_setting, KeySym *parsed_keysym ) {

        const char *keybind_string = NULL;
        const Error_t lookup_error = _libconfig_setting_lookup_string( keybind_setting, "key", &keybind_string );

        if ( lookup_error != ERROR_NONE ) return lookup_error;

        *parsed_keysym = XStringToKeysym( keybind_string );

        if ( *parsed_keysym == NoSymbol ) return ERROR_NOT_FOUND;

        // The upper case return of XConvertCase(), which we don't use.
        KeySym unused = 0;
        XConvertCase( *parsed_keysym, parsed_keysym, &unused );

        return ERROR_NONE;
}

/**
 * @brief Parse a list of keybinds from a libconfig configuration into a list of @ref Key structs.
 *
 * TODO
 *
 * @param[in] libconfig_config Pointer to the libconfig configuration containing the keybinds to
 * be parsed into @p keybind_config.
 * @param[out] keybind_config Pointer to where to dynamically allocate memory and store all the parsed keybinds.
 * @param[out] keybinds_count Pointer to where to store the number of keybinds to be parsed.
 * This value does not take into account any failures during parsing the keybinds.
 * @param[out] keybinds_dynamically_allocated Boolean to track if @p keybind_config has been dynamically
 * allocated yet or not, analogous to if the default keybinds are being used or not. Value is set
 * to `false` after allocation.
 *
 * @return TODO
 *
 * @note TODO Maybe mention dynamic allocation in _parse_binds_config()?
 */
static Errors_t _parse_keybinds_config( const Libconfig_Config_t *libconfig_config, Key **keybind_config, unsigned int *keybinds_count, bool *keybinds_dynamically_allocated ) {
        return _parse_config_array( libconfig_config, NULL, "keybinds", sizeof( Key ), _parse_keybind_adapter, keybinds_dynamically_allocated, (void **) keybind_config, keybinds_count );
}

/**
 * @brief Loads default values from @ref config.h into a @ref Parser_Config_t struct.
 *
 * Initializes the given @ref Parser_Config_t struct with default values from
 * @ref config.h and other misc hardcoded default values. This is intended to
 * proceed any use of the configuration to ensure consistent behavior.
 *
 * @param[in,out] config Pointer to the @ref Parser_Config_t struct to load default values
 * into. If the pointer is NULL, the program will exit with a failure exit code.
 *
 * @note This function is an exit point in the program. If @p config
 * is NULL, the program will be unable to continue. Later attempts to access
 * values in @p config would just cause the program to either enter
 * undefined behavior or crash. Instead, the program will log the fatal error
 * and return a failure exit code.
 */
static void _parser_load_default_config( Parser_Config_t *config ) {

        if ( config == NULL ) {
                log_fatal( "Unable to begin configuration parsing. Pointer to config is NULL\n" );
                exit( EXIT_FAILURE );
        }

        config->rules_dynamically_allocated = false;
        config->rule_array_size = LENGTH( default_rules );

        config->keybinds_dynamically_allocated = false;
        config->keybind_array_size = LENGTH( default_keys );

        config->buttonbinds_dynamically_allocated = false;
        config->buttonbind_array_size = LENGTH( default_buttons );

        config->fonts_dynamically_allocated = false;
        config->fonts_array_size = LENGTH( default_fonts );

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
 * loaded during @ref _parser_load_default_config().
 *
 * @param[in,out] config Pointer to the @ref Parser_Config_t. From @p config, we read
 * the value of @ref Parser_Config_t::config_filepath to see if the user passed in a
 * custom configuration filepath from the cli. In @p config, we also store the filepath
 * and libconfig configuration if we find and parse a valid configuration file.
 * @param fallback_config_loaded
 *
 * @return TODO
 *
 * @todo These error returns may not be the most accurate, not sure exactly the best fits.
 * @todo This function is a bit of a mess, it could be cut down.
 */
static Errors_t _parser_open_config( Parser_Config_t *config, bool *fallback_config_loaded ) {

        Errors_t returned_errors = { 0 };
        int i, config_filepaths_length = 0;

        // Yes this uses a "magic number", but I didn't see a way that was less
        // cumbersome than just hardcoding this value. 5 is just the 1 CLI custom
        // filepath + 4 fallback config locations created a few lines later.
        char *config_filepaths[ 5 ];

        // Check if a custom user config was passed through the CLI.
        // If a path was given, copy it to the first index of the filepaths array.
        if ( config->config_filepath != NULL ) {
                config_filepaths[ config_filepaths_length ] = estrdup( config->config_filepath );
                if ( config_filepaths[ config_filepaths_length ] == NULL ) add_error( &returned_errors, ERROR_NULL_VALUE );
                config_filepaths_length++;
                free( config->config_filepath );
                config->config_filepath = NULL;
        }

        // $XDG_CONFIG_HOME/.config/dwm.conf or $HOME/.config/dwm.conf
        char *config_top_directory = get_xdg_config_home();
        if ( !config_top_directory ) {
                log_warn( "Unable to acquire top level configuration directory\n" );
                add_error( &returned_errors, ERROR_NULL_VALUE );
        } else {
                extend_string( &config_top_directory, "/dwm.conf" );
                config_filepaths[ config_filepaths_length++ ] = config_top_directory;
        }

        // $XDG_CONFIG_HOME/.config/dwm/dwm.conf or $HOME/.config/dwm/dwm.conf
        char *config_sub_directory = get_xdg_config_home();
        if ( !config_sub_directory ) {
                log_warn( "Unable to acquire dwm configuration directory\n" );
                add_error( &returned_errors, ERROR_NULL_VALUE );
        } else {
                extend_string( &config_sub_directory, "/dwm/dwm.conf" );
                config_filepaths[ config_filepaths_length++ ] = config_sub_directory;
        }

        // $XDG_DATA_HOME/.local/share/dwm/dwm_last.conf or $HOME/.local/share/dwm/dwm_last.conf
        char *config_backup = get_xdg_data_home();
        if ( !config_backup ) {
                log_warn( "Unable to acquire dwm configuration backup directory\n" );
                add_error( &returned_errors, ERROR_NULL_VALUE );
        } else {
                extend_string( &config_backup, "/dwm/dwm_last.conf" );
                config_filepaths[ config_filepaths_length++ ] = config_backup;
        }

        // /etc/dwm/dwm.conf
        char *config_fallback = estrdup( "/etc/dwm/dwm.conf" );
        if ( !config_fallback ) {
                log_warn( "Unable to acquire dwm system configuration fallback directory\n" );
                add_error( &returned_errors, ERROR_ALLOCATION );
        } else {
                config_filepaths[ config_filepaths_length++ ] = config_fallback;
        }

        FILE *tmp_file = NULL;
        for ( i = 0; i < config_filepaths_length; i++ ) {
                log_debug( "Attempting to open config file \"%s\"\n", config_filepaths[ i ] );

                if ( config_filepaths[ i ] == NULL ) {
                        log_warn( "config_filepaths[%d] was NULL, unable to lookup intended config. Likely a memory allocation error\n", i );
                        add_error( &returned_errors, ERROR_NULL_VALUE );
                        continue;
                }

                tmp_file = fopen( config_filepaths[ i ], "r" );

                if ( tmp_file == NULL ) {
                        log_warn( "Unable to open config file \"%s\"\n", config_filepaths[ i ] );
                        add_error( &returned_errors, ERROR_NOT_FOUND );
                        continue;
                }

                if ( config_read( &config->libconfig_config, tmp_file ) == CONFIG_FALSE ) {
                        log_warn( "Problem parsing config file \"%s\", line %d: %s\n", config_filepaths[ i ], config_error_line( &config->libconfig_config ), config_error_text( &config->libconfig_config ) );
                        add_error( &returned_errors, ERROR_NULL_VALUE );
                        fclose( tmp_file );
                        continue;
                }

                // Save found config filepath
                config->config_filepath = estrdup( config_filepaths[ i ] );
                if ( config->config_filepath == NULL ) add_error( &returned_errors, ERROR_NULL_VALUE );

                // Check if it's a user's custom configuration
                if ( strcmp( config_filepaths[ i ], config_backup ) == 0 || strcmp( config_filepaths[ i ], config_fallback ) == 0 ) {
                        *fallback_config_loaded = true;
                }

                for ( i = 0; i < config_filepaths_length; i++ ) {
                        if ( config_filepaths[ i ] != NULL ) {
                                free( config_filepaths[ i ] );
                        }
                }

                fclose( tmp_file );

                return returned_errors;
        }

        log_error( "Unable to load any configs. Hardcoded default config values will be used. Exiting parsing\n" );

        for ( i = 0; i < config_filepaths_length; i++ ) {
                if ( config_filepaths[ i ] != NULL ) {
                        free( config_filepaths[ i ] );
                }
        }

        config_destroy( &config->libconfig_config );

        return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in,out] config TODO
 *
 * @return TODO
 */
static Error_t _parser_resolve_include_directory( Parser_Config_t *config ) {

        char *config_include_directory = realpath( config->config_filepath, NULL );

        if ( config_include_directory == NULL ) {
                log_warn( "Failed to allocate memory for the configuration file's include path\n" );
                return ERROR_ALLOCATION;
        }

        config_include_directory = dirname( config_include_directory );

        if ( config_include_directory[ 0 ] == '.' ) {
                log_warn( "Unable to resolve configuration file's include directory\n" );
                free( config_include_directory );
                return ERROR_NOT_FOUND;
        }

        config_set_include_dir( &config->libconfig_config, config_include_directory );

        free( config_include_directory );

        return ERROR_NONE;
}

/**
 * @brief Parse a rule from a libconfig configuration setting.
 *
 * TODO
 *
 * @param[in] rule_libconfig_setting Pointer to the libconfig setting containing the rule to be parsed into @p parsed_rule.
 * @param[in] rule_index Index of the current rule in the larger array. Used purely for debug printing.
 * @param[out] parsed_rule Pointer to the @ref Rule struct where the values parsed from @p rule_libconfig_setting are stored.
 *
 * @return TODO
 */
static Errors_t _parse_rule( Libconfig_Setting_t *rule_libconfig_setting, const int rule_index, Rule *parsed_rule ) {

        Errors_t returned_errors = { 0 };

        add_error( &returned_errors, _parse_rule_string( rule_libconfig_setting, "class", rule_index, &parsed_rule->class ) );
        add_error( &returned_errors, _parse_rule_string( rule_libconfig_setting, "instance", rule_index, &parsed_rule->instance ) );
        add_error( &returned_errors, _parse_rule_string( rule_libconfig_setting, "title", rule_index, &parsed_rule->title ) );
        add_error( &returned_errors, _libconfig_setting_lookup_uint( rule_libconfig_setting, "tag-mask", 0, TAGMASK, &parsed_rule->tags ) );
        add_error( &returned_errors, _libconfig_setting_lookup_int( rule_libconfig_setting, "monitor", -1, 99, &parsed_rule->monitor ) );
        add_error( &returned_errors, _libconfig_setting_lookup_int( rule_libconfig_setting, "floating", 0, 1, &parsed_rule->isfloating ) );

        log_debug( "Rule %d: class: \"%s\", instance: \"%s\", title: \"%s\", tag-mask: %d, monitor: %d, floating: %d\n", rule_index, parsed_rule->class, parsed_rule->instance, parsed_rule->title,
                   parsed_rule->tags, parsed_rule->monitor, parsed_rule->isfloating );

        return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param rule_setting TODO
 * @param rule_index TODO
 * @param parsed_rule TODO
 *
 * @return TODO
 */
static Errors_t _parse_rule_adapter( Libconfig_Setting_t *rule_setting, const unsigned int rule_index, void *parsed_rule ) {
        return _parse_rule( rule_setting, rule_index, (Rule *) parsed_rule );
}

/**
 * @brief Parse a rule's string field value.
 *
 * TODO
 *
 * @param[in] rule_libconfig_setting Pointer to the libconfig setting containing the string to be parsed into @p parsed_value.
 * @param[in] path Path to the string value to be parsed from @p rule_libconfig_setting into @p parsed_value.
 * @param[in] rule_index Index of the current rule in the larger array. Used purely for debug printing.
 * @param[out] parsed_value Pointer to the string where the values parsed from @p rule_libconfig_setting is stored.
 *
 * @return TODO
 */
static Error_t _parse_rule_string( Libconfig_Setting_t *rule_libconfig_setting, const char *path, const int rule_index, const char **parsed_value ) {

        const Error_t error = _libconfig_setting_lookup_string( rule_libconfig_setting, path, parsed_value );

        if ( *parsed_value == NULL ) {
                log_error( "Problem parsing \"%s\" value of rule %d: %s\n", path, rule_index + 1, ERROR_ENUM_STRINGS[ error ] );
                return error;
        }

        if ( strcasecmp( *parsed_value, "NULL" ) == 0 ) *parsed_value = NULL;

        return ERROR_NONE;
}

/**
 * @brief Parse a list of rules from a libconfig configuration into a list of @ref Rule structs.
 *
 * TODO
 *
 * @param[in] libconfig_config Pointer to the libconfig configuration containing the rules to be parsed into @p rules_config.
 * @param[out] rules_config Pointer to where to dynamically allocate memory and store all the parsed rules.
 * @param[out] rules_count Pointer to where to store the number of rules to be parsed. This value does not take into account
 * any failures during parsing the rules.
 * @param[out] rules_dynamically_allocated Boolean to track if @p rules_config has been dynamically allocated yet or not, analogous
 * to if the default rules are being used or not. Value is set to `false` after allocation.
 *
 * @return TODO
 */
static Errors_t _parse_rules_config( const Libconfig_Config_t *libconfig_config, Rule **rules_config, unsigned int *rules_count, bool *rules_dynamically_allocated ) {
        return _parse_config_array( libconfig_config, NULL, "rules", sizeof( Rule ), _parse_rule_adapter, rules_dynamically_allocated, (void **) rules_config, rules_count );
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param tags_setting TODO
 * @param tags_index TODO
 *
 * @return TODO
 */
static Errors_t _parse_tag( Libconfig_Setting_t *tags_setting, const unsigned int tags_index ) {

        Errors_t returned_errors = { 0 };

        if ( tags_index >= LENGTH( tags ) ) {
                add_error( &returned_errors, ERROR_RANGE );
                return returned_errors;
        }

        const char *original_tag_name = tags[ tags_index ];

        // TODO: Is there a better way that will give a better error return?
        tags[ tags_index ] = config_setting_get_string( tags_setting );

        // TODO: Ensure it fits into a 32 bit unsigned int like NumTags does
        if ( tags[ tags_index ] == NULL ) {
                log_error( "Problem reading tag element %d: Value doesn't exist or isn't a string\n", tags_index + 1 );
                add_error( &returned_errors, ERROR_NULL_VALUE );
                tags[ tags_index ] = original_tag_name;
        }

        return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param tags_setting TODO
 * @param tags_index TODO
 * @param unused Unused.
 *
 * @return TODO
 */
static Errors_t _parse_tags_adapter( Libconfig_Setting_t *tags_setting, const unsigned int tags_index, void *unused ) {
        return _parse_tag( tags_setting, tags_index );
}

/**
 * @brief Parse a list of tags from a libconfig configuration.
 *
 * TODO
 *
 * @param[in] libconfig_config Pointer to the libconfig configuration containing the tags to be parsed.
 *
 * @return TODO
 */
static Errors_t _parse_tags_config( const Libconfig_Config_t *libconfig_config ) {
        unsigned int tags_count = 0;
        const Errors_t returned_errors = _parse_config_array( libconfig_config, NULL, "tags", 0, _parse_tags_adapter, NULL, NULL, &tags_count );

        if ( tags_count > LENGTH( tags ) ) {
                log_warn( "More than %lu tag names detected (%d were detected) while parsing config, only the first %lu will be used\n", LENGTH( tags ), tags_count, LENGTH( tags ) );
        } else if ( tags_count < LENGTH( tags ) ) {
                log_warn( "Less than %lu tag names detected while parsing config, filler tags will be used for the remainder\n", LENGTH( tags ) );
        }

        return returned_errors;
}

/**
 * @brief Parse a theme from a libconfig configuration setting.
 *
 * TODO
 *
 * @param[in] theme_libconfig_setting Pointer to the libconfig setting containing the theme to be parsed.
 * @param[in] theme_index Index of the current theme being parsed.
 *
 * @return TODO
 */
static Errors_t _parse_theme( Libconfig_Setting_t *theme_libconfig_setting, const unsigned int theme_index ) {

        Errors_t returned_errors = { 0 };

        // dwm does not support more than 1 theme
        if ( theme_index > 0 ) {
                log_warn( "%d themes detected. dwm can only use the first theme in list \"themes\"\n", theme_index + 1 );
                add_error( &returned_errors, ERROR_RANGE );
                return returned_errors;
        }

        const Errors_t font_errors = _parse_config_array( NULL, theme_libconfig_setting, "fonts", sizeof( char * ), _parse_font_adapter, &dwm_config.fonts_dynamically_allocated, (void **) fonts,
                                                          &dwm_config.fonts_array_size );

        merge_errors( &returned_errors, font_errors );

        for ( int i = 0; i < LENGTH( THEME_ALIAS_MAP ); i++ ) {
                const Error_t error = _libconfig_setting_lookup_string( theme_libconfig_setting, THEME_ALIAS_MAP[ i ].path, THEME_ALIAS_MAP[ i ].value );
                add_error( &returned_errors, error );
                if ( error != ERROR_NONE ) {
                        log_error( "Failed to parse theme %d's element \"%s\": %s\n", theme_index, THEME_ALIAS_MAP[ i ].path, ERROR_ENUM_STRINGS[ error ] );
                }
        }

        return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param theme_setting TODO
 * @param theme_index TODO
 * @param unused TODO
 *
 * @return TODO
 */
static Errors_t _parse_theme_adapter( Libconfig_Setting_t *theme_setting, const unsigned int theme_index, void *unused ) {
        return _parse_theme( theme_setting, theme_index );
}

/**
 * @brief Parse a list of themes from a libconfig configuration.
 *
 * TODO
 *
 * @param[in] libconfig_config Pointer to the libconfig configuration containing the theme to be parsed.
 *
 * @return TODO
 *
 * @note dwm only supports a single theme, so only the first theme in the list is parsed.
 */
static Errors_t _parse_theme_config( const Libconfig_Config_t *libconfig_config ) {
        unsigned int unused = 0;
        return _parse_config_array( libconfig_config, NULL, "themes", 0, _parse_theme_adapter, NULL, NULL, &unused );
}

/// Parser internal utility functions ///

/**
 * @brief Look up a boolean value in a libconfig configuration.
 *
 * TODO
 *
 * @param[in] config Pointer to the libconfig configuration.
 * @param[in] path Path expression to search within @p config.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
* @return TODO
 */
static Error_t _libconfig_lookup_bool( const Libconfig_Config_t *config, const char *path, bool *parsed_value ) {

        if ( !config || !path || !parsed_value ) return ERROR_NULL_VALUE;

        config_setting_t *setting = config_lookup( config, path );

        if ( !setting ) return ERROR_NOT_FOUND;
        if ( config_setting_type( setting ) != CONFIG_TYPE_BOOL ) return ERROR_TYPE;

        *parsed_value = config_setting_get_bool( setting );

        return ERROR_NONE;
}

/**
 * @brief Look up a float value in a libconfig configuration.
 *
 * TODO
 *
 * @param[in] config Pointer to the libconfig config.
 * @param[in] path Path expression to search within @p config.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return TODO
 */
static Error_t _libconfig_lookup_float( const Libconfig_Config_t *config, const char *path, const float range_min, const float range_max, float *parsed_value ) {

        if ( !config || !path || !parsed_value ) return ERROR_NULL_VALUE;

        config_setting_t *setting = config_lookup( config, path );

        if ( !setting ) return ERROR_NOT_FOUND;
        if ( config_setting_type( setting ) != CONFIG_TYPE_FLOAT ) return ERROR_TYPE;

        const float tmp = config_setting_get_float( setting );

        if ( tmp < range_min || tmp > range_max ) return ERROR_RANGE;

        *parsed_value = tmp;

        return ERROR_NONE;
}

/**
 * @brief Look up an integer value in a libconfig configuration.
 *
 * TODO
 *
 * @param[in] config Pointer to the libconfig config.
 * @param[in] path Path expression to search within @p config.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return TODO
 */
static Error_t _libconfig_lookup_int( const Libconfig_Config_t *config, const char *path, const int range_min, const int range_max, int *parsed_value ) {

        if ( !config || !path || !parsed_value ) return ERROR_NULL_VALUE;

        config_setting_t *setting = config_lookup( config, path );

        if ( !setting ) return ERROR_NOT_FOUND;
        if ( config_setting_type( setting ) != CONFIG_TYPE_INT ) return ERROR_TYPE;

        const int tmp = config_setting_get_int( setting );

        if ( tmp < range_min || tmp > range_max ) return ERROR_RANGE;

        *parsed_value = tmp;

        return ERROR_NONE;
}

/**
 * @brief Look up a string value in a libconfig configuration.
 *
 * TODO
 *
 * @param[in] config Pointer to the libconfig configuration.
 * @param[in] path Path expression to search within @p config.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return TODO
 */
static Error_t _libconfig_lookup_string( const Libconfig_Config_t *config, const char *path, const char **parsed_value ) {

        if ( !config || !path || !parsed_value ) return ERROR_NULL_VALUE;

        config_setting_t *setting = config_lookup( config, path );

        if ( !setting ) return ERROR_NOT_FOUND;
        if ( config_setting_type( setting ) != CONFIG_TYPE_STRING ) return ERROR_TYPE;

        *parsed_value = config_setting_get_string( setting );

        return ERROR_NONE;
}

/**
 * @brief Look up an unsigned integer value in a libconfig configuration.
 *
 * TODO
 *
 * @param[in] config Pointer to the libconfig config.
 * @param[in] path Path expression to search within @p config.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return TODO
 */
static Error_t _libconfig_lookup_uint( const Libconfig_Config_t *config, const char *path, const unsigned int range_min, const unsigned int range_max, unsigned int *parsed_value ) {

        if ( !config || !path || !parsed_value ) return ERROR_NULL_VALUE;

        config_setting_t *setting = config_lookup( config, path );

        if ( !setting ) return ERROR_NOT_FOUND;
        if ( config_setting_type( setting ) != CONFIG_TYPE_INT ) return ERROR_TYPE;

        const int tmp_int = config_setting_get_int( setting );

        if ( tmp_int < 0 ) return ERROR_RANGE;

        const unsigned int tmp_uint = (unsigned int) tmp_int;

        if ( tmp_uint < range_min || tmp_uint > range_max ) return ERROR_NOT_FOUND;

        *parsed_value = tmp_uint;

        return ERROR_NONE;
}

/**
 * @brief Look up a boolean value in a libconfig setting.
 *
 * TODO
 *
 * @param[in] setting Pointer to the libconfig setting.
 * @param[in] path Path expression to search within @p setting.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
* @return TODO
 */
static Error_t _libconfig_setting_lookup_bool( Libconfig_Setting_t *setting, const char *path, bool *parsed_value ) {

        if ( !setting || !path || !parsed_value ) return ERROR_NULL_VALUE;

        config_setting_t *child_setting = config_setting_lookup( setting, path );

        if ( !child_setting ) return ERROR_NOT_FOUND;
        if ( config_setting_type( child_setting ) != CONFIG_TYPE_BOOL ) return ERROR_TYPE;

        *parsed_value = config_setting_get_bool( child_setting );

        return ERROR_NONE;
}

/**
 * @brief Look up a float value in a libconfig setting.
 *
 * TODO
 *
 * @param[in] setting Pointer to the libconfig setting.
 * @param[in] path Path expression to search within @p setting.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return TODO
 */
static Error_t _libconfig_setting_lookup_float( Libconfig_Setting_t *setting, const char *path, const float range_min, const float range_max, float *parsed_value ) {

        if ( !setting || !path || !parsed_value ) return ERROR_NULL_VALUE;

        config_setting_t *child_setting = config_setting_lookup( setting, path );

        if ( !child_setting ) return ERROR_NOT_FOUND;
        if ( config_setting_type( child_setting ) != CONFIG_TYPE_FLOAT ) return ERROR_TYPE;

        const float tmp = config_setting_get_float( child_setting );

        if ( tmp < range_min || tmp > range_max ) return ERROR_RANGE;

        *parsed_value = tmp;

        return ERROR_NONE;
}

/**
 * @brief Look up an integer value in a libconfig setting.
 *
 * TODO
 *
 * @param[in] setting Pointer to the libconfig setting.
 * @param[in] path Path expression to search within @p setting.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return TODO
 */
static Error_t _libconfig_setting_lookup_int( Libconfig_Setting_t *setting, const char *path, const int range_min, const int range_max, int *parsed_value ) {

        if ( !setting || !path || !parsed_value ) return ERROR_NULL_VALUE;

        config_setting_t *child_setting = config_setting_lookup( setting, path );

        if ( !child_setting ) return ERROR_NOT_FOUND;
        if ( config_setting_type( child_setting ) != CONFIG_TYPE_INT ) return ERROR_TYPE;

        const int tmp = config_setting_get_int( child_setting );

        if ( tmp < range_min || tmp > range_max ) return ERROR_RANGE;

        *parsed_value = tmp;

        return ERROR_NONE;
}

/**
 * @brief Look up a string value in a libconfig setting.
 *
 * TODO
 *
 * @param[in] setting Pointer to the libconfig setting.
 * @param[in] path Path expression to search within @p setting.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return TODO
 */
static Error_t _libconfig_setting_lookup_string( Libconfig_Setting_t *setting, const char *path, const char **parsed_value ) {

        if ( !setting || !path || !parsed_value ) return ERROR_NULL_VALUE;

        config_setting_t *child_setting = config_setting_lookup( setting, path );

        if ( !child_setting ) return ERROR_NOT_FOUND;
        if ( config_setting_type( child_setting ) != CONFIG_TYPE_STRING ) return ERROR_TYPE;

        *parsed_value = config_setting_get_string( child_setting );

        return ERROR_NONE;
}

/**
 * @brief Look up an unsigned integer value in a libconfig setting.
 *
 * TODO
 *
 * @param[in] setting Pointer to the libconfig setting.
 * @param[in] path Path expression to search within @p setting.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return TODO
 */
static Error_t _libconfig_setting_lookup_uint( Libconfig_Setting_t *setting, const char *path, const unsigned int range_min, const unsigned int range_max, unsigned int *parsed_value ) {

        if ( !setting || !path || !parsed_value ) return ERROR_NULL_VALUE;

        config_setting_t *child_setting = config_setting_lookup( setting, path );

        if ( !child_setting ) return ERROR_NOT_FOUND;
        if ( config_setting_type( child_setting ) != CONFIG_TYPE_INT ) return ERROR_TYPE;

        const int tmp_int = config_setting_get_int( child_setting );

        if ( tmp_int < 0 ) return ERROR_RANGE;

        const unsigned int tmp_uint = (unsigned int) tmp_int;

        if ( tmp_uint < range_min || tmp_uint > range_max ) return ERROR_RANGE;

        *parsed_value = tmp_uint;

        return ERROR_NONE;
}
