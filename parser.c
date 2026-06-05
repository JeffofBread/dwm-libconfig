/**
 * @file parser.c
 * @brief Runtime configuration parser using [libconfig](https://github.com/hyperrealm/libconfig).
 *
 * This file replaces the need to edit config.h or config.def.h and recompile to make changes to
 * dwm's configuration by parsing a configuration file (see example dwm.conf) at runtime. That configuration
 * file contains all the configuration values traditionally found in config.h or config.def.h, and can
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
 * @warning This file must be included in dwm.c after these:
 * 	-# The inclusion of config.h
 * 	-# The structs Arg, Rule, Button, Key
 * 	-# The enums for clicks and color schemes
 *
 * @note I (JeffOfBread) did not write all the code present in this file. Though I have made minor changes,
 * it's still not fair to say I wrote the code. I have listed them above as authors, and all code I used from
 * them (many of the public utility functions) has been credited accordingly.
 *
 * @todo Finish documentation. Make sure function arguments are noted for being dynamically allocated in that function or its sub functions.
 * @todo Overhaul printing / logging to match the new error handling.
 * @todo Try and reduce the number of unique string literals throughout the parser. Tons are used for logging, inflating binary size by 8kb.
 */

#include <errno.h>
#include <libconfig.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

////////////////////////////////
///// Macros & Definitions /////
////////////////////////////////

/**
 * @brief Log a categorized message.
 *
 * Logging is more or less intended to be enabled/disabled by uncommenting/commenting
 * individual logging level's macro bodies. I would prefer a proper logging system, but that
 * seems out the scope of this patch, and this works good enough for the patch's needs.
 *
 * Also, if you would like to include function names in the log, you can use `__func__`:
 * @code{.c}
 * fprintf( stdout, LEVEL " [%s::%s::%d]: ", _parser_filepath, __func__, __LINE__ );
 * @endcode
 *
 * @param[in] LEVEL Log level string literal.
 */
#define _LOG( LEVEL, ... )\
	do {\
		fprintf( stdout, LEVEL " [%s::%d]: ", _parser_filepath, __LINE__ );\
		fprintf( stdout, "" __VA_ARGS__ );\
	} while ( false )

/** @brief Log a trace message. */
#define LOG_TRACE( ... ) //_LOG( "TRACE", __VA_ARGS__ )

/** @brief Log a debug message. */
#define LOG_DEBUG( ... ) //_LOG( "DEBUG", __VA_ARGS__ )

/** @brief Log an informational message. */
#define LOG_INFO( ... )  _LOG( "INFO ", __VA_ARGS__ )

/** @brief Log a warning message. */
#define LOG_WARN( ... )  _LOG( "WARN ", __VA_ARGS__ )

/** @brief Log an error message. */
#define LOG_ERROR( ... ) _LOG( "ERROR", __VA_ARGS__ )

/** @brief Log a fatal issue message. */
#define LOG_FATAL( ... ) //_LOG( "FATAL", __VA_ARGS__ )

/**
 * @brief Return function with @ref Errors_t struct if variable is NULL.
 *
 * @param[in] check Variable to be checked for NULL.
 * @param[in,out] errors @ref Errors_t struct to be returned with an @ref ERROR_NULL_VALUE
 * error if @p check is NULL.
 * @param[in] ... Format and message to be given to @ref LOG_ERROR() to be logged.
 */
#define RETURN_ERRORS_IF_NULL( check, errors, ... )\
	if( check == NULL ){\
		LOG_ERROR( __VA_ARGS__ );\
		add_error( &errors, ERROR_NULL_VALUE );\
		return errors;\
	}

/**
 * @brief Return function with a given value if variable is NULL.
 *
 * @param[in] check Variable to be checked for NULL.
 * @param[in] value Value to be returned if @p check is NULL.
 * @param[in] ... Format and message to be given to @ref LOG_ERROR() to be logged.
 */
#define RETURN_VALUE_IF_NULL( check, value, ... )\
	if( check == NULL ){\
		LOG_ERROR( __VA_ARGS__ );\
		return value;\
	}

/**
 * @brief Return function if variable is NULL.
 *
 * @param[in] check Variable to be checked for NULL.
 * @param[in] ... Format and message to be given to @ref LOG_ERROR() to be logged.
 */
#define RETURN_IF_NULL( check, ... )\
	if( check == NULL ){\
		LOG_ERROR( __VA_ARGS__ );\
		return;\
	}

/**
 * @brief Macro to easily set status text before first bar draw.
 * @param[in] ... Format and variables to be printed to the status text.
 */
#define SET_STATUS_TEXT( ... )\
	do{\
		snprintf( stext, sizeof( stext ), "" __VA_ARGS__ );\
		XStoreName(dpy, root, stext);\
		XSync(dpy, False);\
	} while ( false )

///////////////////////////
///// Structs & Enums /////
///////////////////////////

/** @brief Struct to map a string alias to a matching X11 button. */
typedef struct {
	const char *alias; ///< String alias to search for in configuration.
	const int button;  ///< X11 button relating to @p alias.
} Button_Alias_Map_t;

/** @brief Struct to map a string alias to a matching click enum. */
typedef struct {
	const char *alias; ///< String alias to search for in configuration.
	const int click;   ///< Click enum relating to @p alias.
} Click_Alias_Map_t;

/** @brief Enum used to keep track of what kind of data is to be stored in an Arg struct */
typedef enum {
	TYPE_NONE = 0, ///< No data stored/required.
	TYPE_BOOLEAN,  ///< Boolean data.
	TYPE_INT,      ///< Integer data.
	TYPE_UINT,     ///< Unsigned Integer data.
	TYPE_FLOAT,    ///< Float data.
	TYPE_STRING,   ///< String data.
} Data_Type_t;

/**
 * @brief Enum to categorize the types of errors that can occur during parsing.
 * @todo Maybe look at adding ERROR_ARGUMENT, as we check function arguments a ton.
 */
typedef enum {
	ERROR_NONE = 0,   ///< No issue. Always must be first.
	ERROR_NOT_FOUND,  ///< Value was unable to be acquired.
	ERROR_TYPE,       ///< Value was not of required type.
	ERROR_RANGE,      ///< Value evaluated out of required range.
	ERROR_NULL_VALUE, ///< Value evaluated NULL.
	ERROR_ALLOCATION, ///< Failed to dynamically allocating memory.
	ERROR_IO,         ///< Failure processing or collecting input/output.
	ERROR_ENUM_LENGTH ///< Count of possible enum values. Always must be last.
} Error_t;

/** @brief Struct containing a count of accumulated @ref Error_t. */
typedef struct {
	unsigned int errors_count[ ERROR_ENUM_LENGTH ]; ///< Array of error counts for every enum of @ref Error_t.
} Errors_t;

/** @brief Typedef used to abstract config array parsing functions. Allows for
 * more generic parsing of multiple types. */
typedef Errors_t ( *Array_Element_Parser_Function_t )( config_setting_t *element_setting, unsigned int element_index, void *parsed_element );

/** @brief Struct to map a string alias to a matching function pointer, its argument data type, and that argument's acceptable range. */
typedef struct {
	const char *alias;                 ///< String alias to search for in configuration.
	void ( *function )( const Arg * ); ///< Function relating to @ref alias
	const Data_Type_t argument_type;   ///< Argument data type required by @ref function.
	const long double range_min;       ///< If @ref argument_type is numeric, minimum permissible value.
	const long double range_max;       ///< If @ref argument_type is numeric, maximum permissible value.
} Function_Alias_Map_t;

/** @brief Struct to map a string alias to a matching X11 modifier. */
typedef struct {
	const char *alias;           ///< String alias to search for in configuration.
	const unsigned int modifier; ///< X11 modifier mask relating to @ref alias.
} Modifier_Alias_Map_t;

/** @brief Struct to map a string alias to a matching setting pointer, its optional status, argument data type, and that argument's acceptable range. */
typedef struct {
	const char *alias;           ///< String alias to search for in configuration.
	void *setting;               ///< Global variable (setting) relating to @ref alias.
	const Data_Type_t type;      ///< Data type of @ref setting.
	const bool optional;         ///< Boolean controlling whether a warning is logged if this setting fails to parse.
	const long double range_min; ///< If @ref type is numeric, minimum permissible value.
	const long double range_max; ///< If @ref type is numeric, maximum permissible value.
} Setting_Alias_Map_t;

/** @brief Struct to map a string alias to a matching color string pointer. */
typedef struct {
	const char *alias;  ///< String alias to search for in configuration.
	const char **color; ///< Global color/theme variable relating to @ref alias.
} Theme_Alias_Map_t;

////////////////////////////
///// Global Variables /////
////////////////////////////

config_t libconfig_config;    ///< Master libconfig configuration context used for parsing.
char *config_filepath = NULL; ///< Path to the currently loaded configuration's file.

Key *keys = default_keys;           ///< Array of current keybinds.
Button *buttons = default_buttons;  ///< Array of current buttons.
Rule *rules = default_rules;        ///< Array of current rules.
const char **fonts = default_fonts; ///< Array of current fonts.

unsigned int keys_count = LENGTH( default_keys );       ///< Number of elements in @ref keys.
unsigned int buttons_count = LENGTH( default_buttons ); ///< Number of elements in @ref buttons.
unsigned int rules_count = LENGTH( default_rules );     ///< Number of elements in @ref rules.
unsigned int fonts_count = LENGTH( default_fonts );     ///< Number of elements in @ref fonts_count.

bool keys_malloced = false;    ///< Boolean tracking whether @ref keys has been dynamically allocated.
bool buttons_malloced = false; ///< Boolean tracking whether @ref buttons has been dynamically allocated.
bool rules_malloced = false;   ///< Boolean tracking whether @ref rules has been dynamically allocated.
bool fonts_malloced = false;   ///< Boolean tracking whether @ref fonts has been dynamically allocated.

/**
 * @brief Parser filepath string.
 *
 * This variable is to save on binary size. By instead referencing
 * this string when logging, we don't add hundreds of repeat string
 * literals to the binary, saving roughly 4kb on the final binary.
 */
static const char *_parser_filepath = __FILE__;

/** @brief String name pairs to @ref Error_t. */
const char *ERROR_ENUM_STRINGS[ ] = { "None", "Not found", "Invalid type", "Out of range", "Null value", "Failed to allocate memory", "I/O exception" };

/** @brief Common string used for logging memory allocation issues. */
const char *FAILED_ALLOC_PRINT_STRING = "Failed to allocate memory";

/** @brief Common string used for logging NULL pointers. */
const char *POINTER_NULL_PRINT_STRING = "Cannot continue, pointer NULL";

///////////////////////////////////
///// Public parser functions /////
///////////////////////////////////

void config_cleanup( void );
Errors_t parse_config( void );

////////////////////////////////////
///// Public utility functions /////
////////////////////////////////////

void add_error( Errors_t *errors, Error_t error );
void copy_errors( Errors_t *destination, Errors_t source );
int errors_failure_count( const Errors_t *errors );
char *estrdup( const char *string );
void extend_string( char **source_string, const char *addition );
const Layout *get_layout( void ( *arrange )( Monitor * ) );
char *get_xdg_config_home( void );
char *get_xdg_data_home( void );
char *join_strings( const char *string_1, const char *string_2 );
int make_directory_path( const char *path );
int normalize_path( const char *original_path, char **normalized_path );
void setlayout_floating( const Arg *arg );
void setlayout_monocle( const Arg *arg );
void setlayout_tiled( const Arg *arg );
void spawn_string( const Arg *arg );

/////////////////////////////////////
///// Parser internal functions /////
/////////////////////////////////////

static Error_t _parser_backup_config( config_t *config );
static Error_t _parse_bind_argument( config_setting_t *setting, Data_Type_t argument_type, long double range_min, long double range_max, Arg *argument );
static Errors_t _parse_bind_core( config_setting_t *setting, unsigned int bind_index, unsigned int *modifier, void ( **function )( const Arg * ), Arg *argument );
static Error_t _parse_bind_function( config_setting_t *setting, void ( **function )( const Arg * ), Data_Type_t *argument_type, long double *range_min, long double *range_max );
static Error_t _parse_bind_modifier( config_setting_t *setting, unsigned int *modifier );
static Errors_t _parse_buttonbind( config_setting_t *setting, unsigned int index, Button *buttonbind );
static Errors_t _parse_buttonbind_adapter( config_setting_t *setting, unsigned int index, void *buttonbind );
static Error_t _parse_buttonbind_button( config_setting_t *setting, unsigned int *button );
static Error_t _parse_buttonbind_click( config_setting_t *setting, unsigned int *click );
static Errors_t _parse_buttonbinds_config( const config_t *config, Button **array, unsigned int *count, bool *malloced );
static Errors_t _parse_font( config_setting_t *setting, unsigned int index, const char **font );
static Errors_t _parse_font_adapter( config_setting_t *setting, unsigned int index, void *font );
static Errors_t _parse_generic_settings( const config_t *config );
static Errors_t _parse_keybind( config_setting_t *setting, unsigned int index, Key *keybind );
static Errors_t _parse_keybind_adapter( config_setting_t *setting, unsigned int index, void *keybind );
static Error_t _parse_keybind_keysym( config_setting_t *setting, KeySym *keysym );
static Errors_t _parse_keybinds_config( const config_t *config, Key **array, unsigned int *count, bool *malloced );
static Errors_t _parser_open_config_file( config_t *config, const char *custom_config_filepath, char **found_config_filepath, bool *fallback_config_loaded );
static Errors_t _parse_rule( config_setting_t *setting, unsigned int index, Rule *rule );
static Errors_t _parse_rule_adapter( config_setting_t *setting, unsigned int index, void *rule );
static Errors_t _parse_rules_config( const config_t *config, Rule **array, unsigned int *count, bool *malloced );
static Errors_t _parse_setting_array( const config_setting_t *setting, size_t element_size, Array_Element_Parser_Function_t array_element_parser_function, bool *malloced, void **parsed_config,
                                      unsigned int *parsed_config_length );
static Errors_t _parse_tag( config_setting_t *setting, unsigned int index );
static Errors_t _parse_tags_adapter( config_setting_t *setting, unsigned int index, void *unused );
static Errors_t _parse_tags_config( const config_t *config );
static Errors_t _parse_theme( config_setting_t *setting, unsigned int index );
static Errors_t _parse_theme_adapter( config_setting_t *setting, unsigned int index, void *unused );
static Errors_t _parse_theme_config( const config_t *config );

/////////////////////////////////////////////
///// Parser internal utility functions /////
/////////////////////////////////////////////

static Error_t _libconfig_generic_lookup( config_setting_t *parent_setting, const char *path, int expected_type, void *parsed_value );
static Error_t _libconfig_get_setting_name( const config_setting_t *setting, const char **found_name );
static Error_t _libconfig_lookup_float( config_setting_t *parent_setting, const char *path, float range_min, float range_max, float *parsed_value );
static Error_t _libconfig_lookup_int( config_setting_t *parent_setting, const char *path, int range_min, int range_max, int *parsed_value );
static Error_t _libconfig_lookup_string( config_setting_t *parent_setting, const char *path, const char **parsed_value );
static Error_t _libconfig_lookup_uint( config_setting_t *parent_setting, const char *path, unsigned int range_min, unsigned int range_max, unsigned int *parsed_value );

/////////////////////////////
///// Parser alias maps /////
/////////////////////////////

/** @brief Default alias map for dwm's click enum. */
static const Click_Alias_Map_t CLICK_ALIAS_MAP[ ] = {
	{ "tag", ClkTagBar },
	{ "layout", ClkLtSymbol },
	{ "status", ClkStatusText },
	{ "title", ClkWinTitle },
	{ "client", ClkClientWin },
	{ "desktop", ClkRootWin },
};

/** @brief Default alias map for common X11 buttons. */
static const Button_Alias_Map_t BUTTON_ALIAS_MAP[ ] = {
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

/** @brief Default alias map for dwm's arg functions. */
static const Function_Alias_Map_t FUNCTION_ALIAS_MAP[ ] = {
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
	{ "spawn", spawn_string, TYPE_STRING },
	{ "tag", tag, TYPE_INT, -1, TAGMASK },
	{ "tagmon", tagmon, TYPE_INT, -99, 99 },
	{ "togglebar", togglebar, TYPE_NONE },
	{ "togglefloating", togglefloating, TYPE_NONE },
	{ "toggletag", toggletag, TYPE_INT, -1, TAGMASK },
	{ "toggleview", toggleview, TYPE_INT, -1, TAGMASK },
	{ "view", view, TYPE_INT, -1, TAGMASK },
	{ "zoom", zoom, TYPE_NONE },
};

/** @brief Default alias map for common X11 modifiers. */
static const Modifier_Alias_Map_t MODIFIER_ALIAS_MAP[ ] = {
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

/** @brief Default alias map for dwm's global config settings. */
static const Setting_Alias_Map_t SETTING_ALIAS_MAP[ ] = {
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

/** @brief Default alias map for dwm's scheme indexes. */
static const Theme_Alias_Map_t THEME_ALIAS_MAP[ ] = {
	{ "normal-foreground", &colors[ SchemeNorm ][ ColFg ] },
	{ "normal-background", &colors[ SchemeNorm ][ ColBg ] },
	{ "normal-border", &colors[ SchemeNorm ][ ColBorder ] },
	{ "selected-foreground", &colors[ SchemeSel ][ ColFg ] },
	{ "selected-background", &colors[ SchemeSel ][ ColBg ] },
	{ "selected-border", &colors[ SchemeSel ][ ColBorder ] },
};

///////////////////////////////////
///// Public parser functions /////
///////////////////////////////////

/**
 * @brief Frees dynamically allocated parser data.
 *
 * Frees all dynamically allocated data allocated
 * during the parsing process. Intended for use
 * before program exit and after @ref parse_config().
 */
void config_cleanup( void ) {

	if ( config_filepath != NULL ) free( config_filepath );

	if ( rules_malloced == false ) free( rules );
	if ( keys_malloced == false ) free( keys );
	if ( buttons_malloced == false ) free( buttons );
	if ( fonts_malloced == false ) free( fonts );

	config_destroy( &libconfig_config );
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
 * @return An @ref Errors_t struct containing all collected errors (including @ref ERROR_NONE)
 * while parsing the configuration. See @ref Error_t for a list of possible error types.
 *
 * @todo Polish the status texts a little. I like the idea but could be refined.
 * @todo Backup config file logic is clumsily structured, it should be improved. It also probably should include rules_malloced
 */
Errors_t parse_config( void ) {

	Errors_t returned_errors = { 0 };

	config_init( &libconfig_config );

	bool fallback_config_loaded = false;
	const char *custom_config_filepath = config_filepath;
	config_filepath = NULL;
	copy_errors( &returned_errors, _parser_open_config_file( &libconfig_config, custom_config_filepath, &config_filepath, &fallback_config_loaded ) );

	// Exit the parser if we haven't acquired a configuration file.
	// Without a configuration file, there isn't a reason to continue parsing.
	// The program will have to rely on the hardcoded default values instead.
	if ( config_filepath == NULL ) {
		LOG_ERROR( "Unable to load any configs. Hardcoded default config values will be used. Exiting parsing\n" );
		SET_STATUS_TEXT( "Failed to load config file" );
		config_destroy( &libconfig_config );
		return returned_errors;
	}

	LOG_INFO( "Path to config file: \"%s\"\n", config_filepath );

	config_set_options( &libconfig_config, CONFIG_OPTION_AUTOCONVERT | CONFIG_OPTION_SEMICOLON_SEPARATORS );

	Errors_t parsing_errors = { 0 };

	copy_errors( &parsing_errors, _parse_generic_settings( &libconfig_config ) );
	copy_errors( &parsing_errors, _parse_keybinds_config( &libconfig_config, &keys, &keys_count, &keys_malloced ) );
	copy_errors( &parsing_errors, _parse_buttonbinds_config( &libconfig_config, &buttons, &buttons_count, &buttons_malloced ) );
	copy_errors( &parsing_errors, _parse_rules_config( &libconfig_config, &rules, &rules_count, &rules_malloced ) );
	copy_errors( &parsing_errors, _parse_tags_config( &libconfig_config ) );
	copy_errors( &parsing_errors, _parse_theme_config( &libconfig_config ) );

	// The error requirement being 0 may be a bit strict, I am not sure. May need
	// some relaxing or possibly come up with a better way of calculating if a config
	// passes, or is valid enough to warrant backing up.
	if ( errors_failure_count( &parsing_errors ) == 0 && keys_malloced && buttons_malloced && !fallback_config_loaded ) {
		const Error_t backup_error = _parser_backup_config( &libconfig_config );
		add_error( &parsing_errors, backup_error );
	} else {
		if ( keys_malloced == false || buttons_malloced == false ) {
			LOG_WARN( "Not saving config as backup, as hardcoded default bind values were used, not the user's\n" );
		}
		if ( fallback_config_loaded == true ) {
			LOG_WARN( "Not saving config as backup, as the parsed configuration file is a system fallback configuration\n" );
		}
		if ( errors_failure_count( &parsing_errors ) != 0 ) {
			LOG_WARN( "Not saving config as backup, as the parsed config had too many (%d) errors\n", errors_failure_count( &parsing_errors ) );
		}
	}

	copy_errors( &returned_errors, parsing_errors );
	LOG_DEBUG( "Parsing errors: %d, Total errors: %d\n", errors_failure_count( &parsing_errors ), errors_failure_count( &returned_errors ) );

	SET_STATUS_TEXT( "%s | Errors: %u", config_filepath, errors_failure_count( &returned_errors ) );

	return returned_errors;
}

////////////////////////////////////
///// Public utility functions /////
////////////////////////////////////

/**
 * @brief Adds an error to an @ref Errors_t struct.
 *
 * This function acts as a simple way to increment the error
 * count of an @ref Errors_t struct at the correct error index.
 *
 * @param[out] errors Pointer to an @ref Errors_t struct to add @p error to.
 * @param[in] error Error to be added to the error count in @p errors.
 */
void add_error( Errors_t *errors, const Error_t error ) {

	RETURN_IF_NULL( errors, "%s:\"errors\"\n", POINTER_NULL_PRINT_STRING );

	if ( error < ERROR_ENUM_LENGTH ) {
		errors->errors_count[ error ]++;
	}
}

/**
 * @brief Copy errors from @p source to @p destination.
 *
 * This function copies the errors in @p source to the @ref Errors_t
 * struct pointed to by @p destination. It does not alter the errors
 * in @p source. If the pointer to @p destination is NULL, an error
 * will be logged and the function will return.
 *
 * @param[out] destination Pointer to where to copy errors to.
 * @param[in] source Errors to copy.
 */
void copy_errors( Errors_t *destination, const Errors_t source ) {

	RETURN_IF_NULL( destination, "%s:\"destination\"\n", POINTER_NULL_PRINT_STRING );

	for ( unsigned int i = 0; i < ERROR_ENUM_LENGTH; i++ ) {
		destination->errors_count[ i ] += source.errors_count[ i ];
	}
}

/**
 * @brief Counts the number of failure errors present in an @ref Errors_t struct.
 *
 * This function acts as a simple way to count the number of failure errors
 * present in an @ref Errors_t struct.
 *
 * @param[in] errors Pointer to an @ref Errors_t struct to count errors from.
 *
 * @return -1 if @p errors is NULL, else the total of the number of failure
 * errors present in @p errors.
 */
int errors_failure_count( const Errors_t *errors ) {

	RETURN_VALUE_IF_NULL( errors, -1, "%s:\"errors\"\n", POINTER_NULL_PRINT_STRING );

	int count = 0;

	for ( unsigned int i = ERROR_NONE + 1; i < ERROR_ENUM_LENGTH; i++ ) {
		count += (int) errors->errors_count[ i ];
	}

	return count;
}

/**
 * @brief Simple wrapper around `strdup()` to provide error logging.
 *
 * This function acts as a simple wrapper around `strdup()` that provides
 * some NULL safety as well as error logging around `strdup()`. It can
 * be used as a drop in replacement for `strdup()` safely.
 *
 * @param[in] string String to be copied.
 *
 * @return NULL if allocation failed or @p string was NULL, or the pointer
 * to a dynamically allocated duplicate of @p string.
 *
 * @note Returned string is dynamically allocated and will need to be manually freed.
 */
char *estrdup( const char *string ) {

	RETURN_VALUE_IF_NULL( string, NULL, "%s:\"string\"\n", POINTER_NULL_PRINT_STRING );

	errno = 0;
	char *return_string = strdup( string );

	RETURN_VALUE_IF_NULL( return_string, NULL, "%s using strdup(): %s\n", FAILED_ALLOC_PRINT_STRING, strerror(errno) );

	return return_string;
}

/**
 * @brief Extend one string with another.
 *
 * This function reallocates and extends the string pointed to by @p source_string with the string @p addition.
 * If either the pointer to @p source_string or @p addition are NULL, the function will log an error and return.
 * If the string at @p source_string is NULL, @p addition will be copied into it's place using dynamic allocation.
 * If all succeeds, then the string pointed to by @p source_string will be dynamically reallocated to contain both
 * the source string and @p addition, and will need to be freed manually.
 *
 * @param[in,out] source_string Pointer to the string to be extended with @p addition. Must point to a NULL or heap allocated string.
 * @param[in] addition Additional string to be appended to the string pointed to by @p source_string.
 *
 * @warning String at @p source_string must be NULL or heap allocated. If it points to a string literal,
 * `realloc()` will crash the program. See @ref join_strings() if you wish to append a string literal.
 *
 * @note
 * 	-# @p source_string is or will be dynamically allocated and must be manually freed.
 * 	-# This function is derived from [picom's](https://github.com/yshui/picom/) `mstrextend()`.
 * Credit more or less goes to [Yuxuan Shui](yshuiv7@gmail.com), I just made some minor adjustments.
 *
 * @author Yuxuan Shui - <yshuiv7@gmail.com>
 *
 * @see https://github.com/yshui/picom
 * @see https://github.com/yshui/picom/blob/69539edc0638019e3dd88c67007e90ce7f51174e/src/utils/str.c#L54
 */
void extend_string( char **source_string, const char *addition ) {

	RETURN_IF_NULL( source_string, "%s:\"source_string\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_IF_NULL( addition, "%s:\"addition\"\n", POINTER_NULL_PRINT_STRING );

	if ( *source_string == NULL ) {
		*source_string = estrdup( addition );
		LOG_DEBUG( "Source string was NULL, addition was copied into its place\n" );
		return;
	}

	const size_t source_length = strlen( *source_string );
	const size_t addition_length = strlen( addition );
	const size_t total_length = source_length + addition_length + 1; // +1 for NULL termination

	errno = 0;
	*source_string = realloc( *source_string, total_length * sizeof( char ) );

	RETURN_IF_NULL( *source_string, "%s (%lu bytes) using realloc(): %s\n", FAILED_ALLOC_PRINT_STRING, total_length * sizeof( char ), strerror(errno) );

	strncpy( *source_string + source_length, addition, addition_length );
	( *source_string )[ total_length - 1 ] = '\0';
}

/**
 * @brief Find and return a layout that uses a specific arrange function.
 *
 * This function searches the global array `layouts` for a layout struct that contains
 * @p arrange. If it is found, a pointer to it is returned, else NULL is returned.
 *
 * @param[in] arrange Arrange function used by the layout to be found and returned.
 *
 * @return Pointer to the found layout or NULL if no layout containing @p arrange was found.
 *
 * @note This function assumes that only one layout uses the given arrange
 * function. If multiple layouts use the same arrange function, then this
 * function will simply return the first one in the array using that arrange
 * function.
 */
const Layout *get_layout( void ( *arrange )( Monitor * ) ) {

	for ( unsigned int i = 0; i < LENGTH( layouts ); i++ ) {
		if ( layouts[ i ].arrange == arrange ) {
			return &layouts[ i ];
		}
	}

	return NULL;
}

/**
 * @brief Get the user's XDG configuration directory path.
 *
 * TODO
 *
 * @return Pointer of a dynamically allocated string containing the complete path to the user's XDG
 * configuration directory, or NULL if no directory was able to be found.
 *
 * @note
 * 	-# If the function is successful, the returned string will have been dynamically allocated
 * and will need to be manually freed.
 * 	-# This function is derived from [picom's](https://github.com/yshui/picom/) `xdg_config_home()`.
 * Credit more or less goes to [Yuxuan Shui](yshuiv7@gmail.com), I just made some minor adjustments.
 *
 * @author Yuxuan Shui - <yshuiv7@gmail.com>
 *
 * @see https://github.com/yshui/picom
 * @see https://github.com/yshui/picom/blob/69539edc0638019e3dd88c67007e90ce7f51174e/src/config.c#L33
 */
char *get_xdg_config_home( void ) {

	char *xdg_config_home = getenv( "XDG_CONFIG_HOME" );
	const char *user_home = getenv( "HOME" );

	LOG_DEBUG( "XDG_CONFIG_HOME: \"%s\", HOME: \"%s\"\n", xdg_config_home, user_home );

	if ( xdg_config_home == NULL ) {
		const char *default_config_directory = "/.config";

		RETURN_VALUE_IF_NULL( user_home, NULL, "XDG_CONFIG_HOME and HOME are not set\n" );

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
 * @note This function is derived from [picom's](https://github.com/yshui/picom/) `xdg_config_home()`.
 * Credit more or less goes to [Yuxuan Shui](yshuiv7@gmail.com), I just made some minor adjustments.
 *
 * @author Yuxuan Shui - <yshuiv7@gmail.com>
 *
 * @see https://github.com/yshui/picom
 * @see https://github.com/yshui/picom/blob/69539edc0638019e3dd88c67007e90ce7f51174e/src/config.c#L33
 */
char *get_xdg_data_home( void ) {

	char *xdg_data_home = getenv( "XDG_DATA_HOME" );
	const char *user_home = getenv( "HOME" );

	LOG_DEBUG( "XDG_DATA_HOME: \"%s\", HOME: \"%s\"\n", xdg_data_home, user_home );

	if ( xdg_data_home == NULL ) {
		const char *default_data_directory = "/.local/share";

		RETURN_VALUE_IF_NULL( user_home, NULL, "XDG_DATA_HOME and HOME are not set\n" );

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
 * @return Pointer of a dynamically allocated string containing the now joined
 * strings, or NULL on failure.
 *
 * @note
 * 	-# Returned string is dynamically allocated and will need to be manually freed.
 * 	-# This function is derived from [picom's](https://github.com/yshui/picom/) `mstrjoin()`.
 * Credit more or less goes to [Yuxuan Shui](yshuiv7@gmail.com), I just made some minor adjustments.
 * 	-# gcc warns about legitimate truncation worries in `strncpy()`.
 * `strncpy( joined_string, string_1, length_1 )` intentionally truncates the null byte
 * from @p string_1, however. `strncpy( joined_string + length_1, string_2, length_2 )`
 * uses bounds depending on the source argument, but `joined_string` is allocated with
 * `length_1 + length_2 + 1`, so this `strncpy()` can't overflow.
 *
 * @author Yuxuan Shui - <yshuiv7@gmail.com>
 *
 * @see https://github.com/yshui/picom
 * @see https://github.com/yshui/picom/blob/69961987e1238f9bc3af53fa0774fc19fdec44a4/src/utils/str.c#L24
 */
char *join_strings( const char *string_1, const char *string_2 ) {

	RETURN_VALUE_IF_NULL( string_1, NULL, "%s:\"string_1\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_VALUE_IF_NULL( string_2, NULL, "%s:\"string_2\"\n", POINTER_NULL_PRINT_STRING );

	const size_t length_1 = strlen( string_1 );
	const size_t length_2 = strlen( string_2 );
	const size_t total_length = length_1 + length_2 + 1;

	errno = 0;
	char *joined_string = calloc( total_length, sizeof( char ) );

	RETURN_VALUE_IF_NULL( joined_string, NULL, "%s (%lu bytes) using calloc(): %s\n", FAILED_ALLOC_PRINT_STRING, total_length * sizeof( char ), strerror(errno) )

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
 * @note This function is derived from [dwm-ipc's](https://github.com/mihirlad55/dwm-ipc) `mkdirp()`.
 * Credit more or less goes to [Mihir Lad](mihirlad55@gmail.com), I just made some minor adjustments.
 *
 * @author Mihir Lad - <mihirlad55@gmail.com>
 *
 * @see https://github.com/mihirlad55/dwm-ipc
 * @see https://github.com/mihirlad55/dwm-ipc/blob/b3eebba7c043482d454afc5c882f513fc1b157ad/util.c#L103
 */
int make_directory_path( const char *path ) {

	RETURN_VALUE_IF_NULL( path, -1, "%s:\"path\"\n", POINTER_NULL_PRINT_STRING );

	char *normalized_path = NULL;

	if ( normalize_path( path, &normalized_path ) != 0 ) {
		LOG_ERROR( "Unable to make directory path because path normalization failed" );
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
				LOG_DEBUG( "Making directory %s\n", current_path );
				if ( mkdir( current_path, 0700 ) < 0 ) {
					LOG_ERROR( "Failed to make directory \"%s\": %s\n", current_path, strerror( errno ) );
					free( normalized_path );
					return -1;
				}
			} else {
				LOG_ERROR( "Error stat-ing directory \"%s\": %s\n", current_path, strerror( errno ) );
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
 * allocated and will need to be manually freed.
 *
 * @return 0 on success, -1 on failure.
 *
 * @note
 * 	-# @p normalized_path can be dynamically allocated and will need to be manually freed.
 * 	-# This function is derived from [dwm-ipc's](https://github.com/mihirlad55/dwm-ipc) `normalizepath()`.
 * Credit more or less goes to [Mihir Lad](mihirlad55@gmail.com), I just made some minor adjustments.
 *
 * @author Mihir Lad - <mihirlad55@gmail.com>
 *
 * @see https://github.com/mihirlad55/dwm-ipc
 * @see https://github.com/mihirlad55/dwm-ipc/blob/b3eebba7c043482d454afc5c882f513fc1b157ad/util.c#L40
 */
int normalize_path( const char *original_path, char **normalized_path ) {

	RETURN_VALUE_IF_NULL( original_path, -1, "%s:\"original_path\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_VALUE_IF_NULL( normalized_path, -1, "%s:\"normalized_path\"\n", POINTER_NULL_PRINT_STRING );

	const size_t original_length = strlen( original_path );

	errno = 0;
	*normalized_path = calloc( ( original_length + 1 ), sizeof( char ) );

	RETURN_VALUE_IF_NULL( *normalized_path, -1, "%s (%lu bytes) using calloc(): %s\n", FAILED_ALLOC_PRINT_STRING, ( original_length + 1 ) * sizeof( char ), strerror(errno) );

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

	errno = 0;
	*normalized_path = realloc( *normalized_path, new_length * sizeof( char ) );

	RETURN_VALUE_IF_NULL( *normalized_path, -1, "%s (%lu bytes) using realloc(): %s\n", FAILED_ALLOC_PRINT_STRING, new_length * sizeof( char ), strerror(errno) );

	return 0;
}

/**
 * @brief Set current layout to floating.
 *
 * This is a wrapper for the `setlayout()` function. It sets the
 * currently focused monitor's layout to the floating layout found
 * in the layouts array.
 *
 * @param arg Unused.
 */
void setlayout_floating( const Arg *arg ) {

	const Layout *layout = get_layout( NULL ); // NULL is floating layout's arrange function

	RETURN_IF_NULL( layout, "Failed to get floating layout" );

	setlayout( &(Arg){ .v = layout } );
}

/**
 * @brief Set current layout to monocle.
 *
 * This is a wrapper for the `setlayout()` function. It sets the
 * currently focused monitor's layout to the monocle layout found in
 * the layouts array.
 *
 * @param arg Unused.
 */
void setlayout_monocle( const Arg *arg ) {

	const Layout *layout = get_layout( monocle );

	RETURN_IF_NULL( layout, "Failed to get monocle layout" );

	setlayout( &(Arg){ .v = layout } );
}

/**
 * @brief Set current layout to tile.
 *
 * This is a wrapper for the `setlayout()` function. It sets the
 * currently focused monitor's layout to the tile layout found in the
 * layouts array.
 *
 * @param arg Unused.
 */
void setlayout_tiled( const Arg *arg ) {

	const Layout *layout = get_layout( tile );

	RETURN_IF_NULL( layout, "Failed to get tile layout" );

	setlayout( &(Arg){ .v = layout } );
}

/**
 * @brief Wrapper around `spawn()` for simpler program spawning.
 *
 * This wrapper is to simplify the parsers interaction with the
 * `spawn()` function. It takes in the program name and arguments
 * as a string from Arg, prepares them with the necessary shell
 * executable and flag, and then passes it all to `spawn()`.
 *
 * @param[in] arg Pointer to the Arg struct containing a null
 * terminated string containing the name and arguments of the program
 * to spawn.
 */
void spawn_string( const Arg *arg ) {

	RETURN_IF_NULL( arg, "%s:\"arg\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_IF_NULL( arg->v, "%s:\"arg->v\"\n", POINTER_NULL_PRINT_STRING );

	const char *cmd = arg->v;
	char *argv[ ] = { "/bin/sh", "-c", (char *) cmd, NULL };
	const Arg tmp = { .v = argv };

	LOG_DEBUG( "Attempting to spawn \"%s\"\n", (char *) cmd );
	spawn( &tmp );
}

/////////////////////////////////////
///// Parser internal functions /////
/////////////////////////////////////

/**
 * @brief Backs up a libconfig configuration to disk.
 *
 * This function backs up the given libconfig config_t,
 * @p config, following the XDG specification. If the function
 * @ref get_xdg_data_home() fails to find the XDG data directory
 * (which is likely), we will default to using "~/.local/share/"
 * instead. Meaning that, in the latter case, the filepath to
 * the backup file will be "~/.local/share/dwm/dwm_last.conf".
 * "/dwm/dwm_last.conf" will always be appended to the end of
 * whatever path is returned by @ref get_xdg_data_home().
 * The backup file is then created and written to by libconfig's
 * `config_write_file()`.
 *
 * @param[in] config Pointer to the libconfig configuration
 * to be backed up.
 *
 * @return @ref ERROR_NONE on success.
 * @return @ref ERROR_NULL_VALUE if a NULL argument is provided.
 * @return @ref ERROR_NOT_FOUND if the call to @ref get_xdg_data_home() failed
 * to provide necessary directory.
 * @return @ref ERROR_ALLOCATION if any calls to @ref extend_string() were unable
 * to allocate necessary memory to construct the complete backup filepath.
 * @return @ref ERROR_IO if libconfig failed to write @p config to a file.
 */
static Error_t _parser_backup_config( config_t *config ) {

	RETURN_VALUE_IF_NULL( config, ERROR_NULL_VALUE, "%s:\"config\"\n", POINTER_NULL_PRINT_STRING );

	// Save xdg data directory to buffer (~/.local/share)
	char *buffer = get_xdg_data_home();

	RETURN_VALUE_IF_NULL( buffer, ERROR_NOT_FOUND, "Failed to get parent backup directory path\n" );

	// Append buffer (already has "~/.local/share" or other xdg data directory)
	// with the directory we want to backup the config to, create the directory
	// if it doesn't exist, and then extend with the filename we want to backup
	// to config in.
	extend_string( &buffer, "/dwm/" );
	RETURN_VALUE_IF_NULL( buffer, ERROR_ALLOCATION, "%s for backup directory path\n", FAILED_ALLOC_PRINT_STRING );

	if ( make_directory_path( buffer ) != 0 ) return ERROR_IO;

	extend_string( &buffer, "dwm_last.conf" );
	RETURN_VALUE_IF_NULL( buffer, ERROR_ALLOCATION, "%s for backup file path\n", FAILED_ALLOC_PRINT_STRING );

	if ( config_write_file( config, buffer ) == CONFIG_FALSE ) {
		LOG_ERROR( "Failed to write configuration to \"%s\"\n", buffer );
		free( buffer );
		return ERROR_IO;
	}

	LOG_INFO( "Current config backed up to \"%s\"\n", buffer );
	free( buffer );

	return ERROR_NONE;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] setting TODO
 * @param[in] argument_type Enum describing the type of data stored in @p parsed_argument.
 * @param[in] range_min Minimum value @p parsed_argument can have. Only applies to numerical types.
 * @param[in] range_max Maximum value @p parsed_argument can have. Only applies to numerical types.
 * @param[out] argument Pointer to an Arg struct where the parsed value of type @p argument_type
 * in range @p range_min to @p range_max will be stored.
 *
 * @return @ref ERROR_NONE on success.
 * @return @ref ERROR_NULL_VALUE if a NULL argument is provided.
 * @return @ref ERROR_TYPE if @p argument_type does not match any case in the switch statement.
 * @return Any error returned from any of the `_libconfig_setting_lookup_TYPE()` functions.
 */
static Error_t _parse_bind_argument( config_setting_t *setting, const Data_Type_t argument_type, const long double range_min, const long double range_max, Arg *argument ) {

	RETURN_VALUE_IF_NULL( setting, ERROR_NULL_VALUE, "%s:\"setting\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_VALUE_IF_NULL( argument, ERROR_NULL_VALUE, "%s:\"argument\"\n", POINTER_NULL_PRINT_STRING );

	const char *argument_path = "argument";
	Error_t lookup_error = ERROR_NONE;
	switch ( argument_type ) {
		case TYPE_NONE: {
			return ERROR_NONE;
		}

		case TYPE_BOOLEAN: {
			lookup_error = _libconfig_generic_lookup( setting, argument_path, CONFIG_TYPE_BOOL, (bool *) &argument->ui );
			break;
		}

		case TYPE_INT: {
			lookup_error = _libconfig_lookup_int( setting, argument_path, range_min, range_max, &argument->i );
			break;
		}

		case TYPE_UINT: {
			lookup_error = _libconfig_lookup_uint( setting, argument_path, range_min, range_max, &argument->ui );
			break;
		}

		case TYPE_FLOAT: {
			lookup_error = _libconfig_lookup_float( setting, argument_path, range_min, range_max, &argument->f );
			break;
		}

		case TYPE_STRING: {
			lookup_error = _libconfig_lookup_string( setting, argument_path, (const char **) &argument->v );
			break;
		}

		default: {
			LOG_WARN( "Unknown argument type during bind parsing: %d. Please reprogram to a valid type\n", argument_type );
			return ERROR_TYPE;
		}
	}

	return lookup_error;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] setting TODO
 * @param[in] bind_index TODO
 * @param[out] modifier TODO
 * @param[out] function TODO
 * @param[out] argument TODO
 *
 * @return TODO
 */
static Errors_t _parse_bind_core( config_setting_t *setting, const unsigned int bind_index, unsigned int *modifier, void ( **function )( const Arg * ), Arg *argument ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( setting, returned_errors, "%s:\"setting\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_ERRORS_IF_NULL( modifier, returned_errors, "%s:\"modifier\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_ERRORS_IF_NULL( function, returned_errors, "%s:\"function\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_ERRORS_IF_NULL( argument, returned_errors, "%s:\"argument\"\n", POINTER_NULL_PRINT_STRING );

	const char *setting_name = NULL;
	const Error_t setting_name_error = _libconfig_get_setting_name( setting, &setting_name );
	add_error( &returned_errors, setting_name_error );

	if ( setting_name_error != ERROR_NONE ) {
		LOG_WARN( "Unable to acquire setting name, logs in this function may not print correctly: %s\n", ERROR_ENUM_STRINGS[ setting_name_error ] );
	}

	const Error_t modifier_error = _parse_bind_modifier( setting, modifier );
	add_error( &returned_errors, modifier_error );

	if ( modifier_error != ERROR_NONE ) {
		LOG_WARN( "\"%s\" index %d invalid, unable to parse the bind's modifier: %s\n", setting_name, bind_index + 1, ERROR_ENUM_STRINGS[ modifier_error ] );
		return returned_errors;
	}

	Data_Type_t argument_type = TYPE_NONE;
	long double range_min = 0, range_max = 0;
	const Error_t bind_error = _parse_bind_function( setting, function, &argument_type, &range_min, &range_max );
	add_error( &returned_errors, bind_error );

	if ( bind_error != ERROR_NONE ) {
		LOG_WARN( "\"%s\" index %d invalid, unable to parse the bind's function: %s\n", setting_name, bind_index + 1, ERROR_ENUM_STRINGS[ bind_error ] );
		return returned_errors;
	}

	const Error_t argument_error = _parse_bind_argument( setting, argument_type, range_min, range_max, argument );
	add_error( &returned_errors, argument_error );

	if ( argument_error != ERROR_NONE ) {
		LOG_WARN( "\"%s\" index %d invalid, unable to parse the bind's arguments: %s\n", setting_name, bind_index + 1, ERROR_ENUM_STRINGS[ argument_error ] );
		return returned_errors;
	}

	return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] setting TODO
 * @param[out] function TODO
 * @param[out] argument_type Pointer to where to store the data type of the Arg that can be passed to @p function.
 * @param[out] range_min Pointer to where to store the minimum value of the Arg that can be passed to @p function.
 * @param[out] range_max Pointer to where to store the maximum value of the Arg that can be passed to @p function.
 *
 * @return @ref ERROR_NONE on success.
 * @return @ref ERROR_NULL_VALUE if a NULL argument is provided.
 * @return @ref ERROR_NOT_FOUND if parsed intermediate function string does not match any alias found in @ref FUNCTION_ALIAS_MAP.
 * @return Any error returned from @ref _libconfig_setting_lookup_string().
 */
static Error_t _parse_bind_function( config_setting_t *setting, void ( **function )( const Arg * ), Data_Type_t *argument_type, long double *range_min, long double *range_max ) {

	RETURN_VALUE_IF_NULL( setting, ERROR_NULL_VALUE, "%s:\"setting\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_VALUE_IF_NULL( function, ERROR_NULL_VALUE, "%s:\"function\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_VALUE_IF_NULL( argument_type, ERROR_NULL_VALUE, "%s:\"argument_type\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_VALUE_IF_NULL( range_min, ERROR_NULL_VALUE, "%s:\"range_min\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_VALUE_IF_NULL( range_max, ERROR_NULL_VALUE, "%s:\"range_max\"\n", POINTER_NULL_PRINT_STRING );

	const char *function_string = NULL;
	const Error_t lookup_error = _libconfig_lookup_string( setting, "function", &function_string );

	if ( lookup_error != ERROR_NONE ) return lookup_error;

	for ( unsigned int i = 0; i < LENGTH( FUNCTION_ALIAS_MAP ); i++ ) {
		if ( strcasecmp( function_string, FUNCTION_ALIAS_MAP[ i ].alias ) == 0 ) {
			*function = FUNCTION_ALIAS_MAP[ i ].function;
			*argument_type = FUNCTION_ALIAS_MAP[ i ].argument_type;
			*range_min = FUNCTION_ALIAS_MAP[ i ].range_min;
			*range_max = FUNCTION_ALIAS_MAP[ i ].range_max;
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
 * @param[in] setting TODO
 * @param[out] modifier TODO
 *
 * @return @ref ERROR_NONE on success.
 * @return @ref ERROR_NULL_VALUE if a NULL argument is provided.
 * @return @ref ERROR_ALLOCATION if @ref estrdup() fails to allocate memory.
 * @return @ref ERROR_NOT_FOUND if parsed intermediate modifier string does not
 * match any alias found in @ref MODIFIER_ALIAS_MAP.
 * @return Any error returned from @ref _libconfig_setting_lookup_string().
 *
 * @see https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/master/include/X11/X.h
 */
static Error_t _parse_bind_modifier( config_setting_t *setting, unsigned int *modifier ) {

	RETURN_VALUE_IF_NULL( setting, ERROR_NULL_VALUE, "%s:\"setting\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_VALUE_IF_NULL( modifier, ERROR_NULL_VALUE, "%s:\"modifier\"\n", POINTER_NULL_PRINT_STRING );

	const char *modifier_string = NULL;
	const Error_t lookup_error = _libconfig_lookup_string( setting, "modifier", &modifier_string );

	if ( lookup_error != ERROR_NONE ) return lookup_error;

	char *buffer = estrdup( modifier_string );

	if ( buffer == NULL ) return ERROR_ALLOCATION;

	char *modifier_token = strtok( buffer, "+" );
	while ( modifier_token != NULL ) {

		while ( *modifier_token == ' ' || *modifier_token == '\t' ) modifier_token++;

		char *end = modifier_token + strlen( modifier_token ) - 1;
		while ( end > modifier_token && ( *end == ' ' || *end == '\t' ) ) {
			*end = '\0';
			end--;
		}

		bool found = false;
		for ( unsigned int i = 0; i < LENGTH( MODIFIER_ALIAS_MAP ); i++ ) {
			if ( strcasecmp( modifier_token, MODIFIER_ALIAS_MAP[ i ].alias ) == 0 ) {
				*modifier |= MODIFIER_ALIAS_MAP[ i ].modifier;
				found = true;
				break;
			}
		}

		if ( found == false ) {
			LOG_WARN( "Invalid modifier: \"%s\"\n", modifier_token );
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
 * @param[in] setting TODO
 * @param[in] index TODO
 * @param[out] buttonbind TODO
 *
 * @return TODO
 */
static Errors_t _parse_buttonbind( config_setting_t *setting, const unsigned int index, Button *buttonbind ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( setting, returned_errors, "%s:\"setting\" at index %d\n", POINTER_NULL_PRINT_STRING, index );
	RETURN_ERRORS_IF_NULL( buttonbind, returned_errors, "%s:\"buttonbind\" at index %d\n", POINTER_NULL_PRINT_STRING, index );

	copy_errors( &returned_errors, _parse_bind_core( setting, index, &buttonbind->mask, &buttonbind->func, &buttonbind->arg ) );

	const Error_t button_error = _parse_buttonbind_button( setting, &buttonbind->button );
	add_error( &returned_errors, button_error );

	if ( button_error != ERROR_NONE ) {
		LOG_WARN( "Buttonbind %d invalid, unable to parse the bind's button: %s\n", index + 1, ERROR_ENUM_STRINGS[ button_error ] );
		return returned_errors;
	}

	const Error_t click_error = _parse_buttonbind_click( setting, &buttonbind->click );
	add_error( &returned_errors, click_error );

	if ( click_error != ERROR_NONE ) {
		LOG_WARN( "Buttonbind %d invalid, unable to parse the bind's click: %s\n", index + 1, ERROR_ENUM_STRINGS[ click_error ] );
	}

	// Ensure button is zeroed and doesn't accidentally have any
	// data left over from parsing that could cause unusual behavior
	if ( errors_failure_count( &returned_errors ) != 0 ) {
		buttonbind->click = 0;
		buttonbind->mask = 0;
		buttonbind->button = 0;
		buttonbind->func = NULL;
		buttonbind->arg.i = 0;
	}

	return returned_errors;
}

/**
 * @brief Thin adapter function to allow for buttonbind parsing inside a generic array parsing function.
 *
 * See @ref _parse_buttonbind() for function documentation.
 */
static Errors_t _parse_buttonbind_adapter( config_setting_t *setting, const unsigned int index, void *buttonbind ) {
	return _parse_buttonbind( setting, index, (Button *) buttonbind );
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] setting TODO
 * @param[out] button TODO
 *
 * @return @ref ERROR_NONE on success.
 * @return @ref ERROR_NULL_VALUE if a NULL argument is provided or if `strtoul()`
 * failed to properly convert parsed intermediate button string to a number.
 * @return @ref ERROR_RANGE If parsed button value exceeds 8 bit mask value limit (255,
 * 8 bit maximum value minus 1) or is 0, an empty/no button.
 *
 * @see https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/master/include/X11/X.h
 */
static Error_t _parse_buttonbind_button( config_setting_t *setting, unsigned int *button ) {

	RETURN_VALUE_IF_NULL( setting, ERROR_NULL_VALUE, "%s:\"setting\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_VALUE_IF_NULL( button, ERROR_NULL_VALUE, "%s:\"button\"\n", POINTER_NULL_PRINT_STRING );

	const char *button_string = NULL;
	const Error_t lookup_error = _libconfig_lookup_string( setting, "button", &button_string );

	if ( lookup_error != ERROR_NONE ) return lookup_error;

	for ( unsigned int i = 0; i < LENGTH( BUTTON_ALIAS_MAP ); i++ ) {
		if ( strcasecmp( button_string, BUTTON_ALIAS_MAP[ i ].alias ) == 0 ) {
			*button = BUTTON_ALIAS_MAP[ i ].button;
			return ERROR_NONE;
		}
	}

	errno = 0;
	char *end_pointer = NULL;
	const unsigned long parsed_value = strtoul( button_string, &end_pointer, 10 );

	if ( errno != 0 || end_pointer == button_string || *end_pointer != '\0' ) return ERROR_NULL_VALUE;

	// X11 button mask is only 8 bits, ensure it fits
	if ( parsed_value == 0 || parsed_value >= Button1Mask ) return ERROR_RANGE;

	*button = (unsigned int) parsed_value;

	return ERROR_NONE;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] setting TODO
 * @param[out] click TODO
 *
 * @return @ref ERROR_NONE on success.
 * @return @ref ERROR_NULL_VALUE if a NULL argument is provided.
 * @return @ref ERROR_NOT_FOUND if parsed intermediate click string does not
 * match any alias found in @ref CLICK_ALIAS_MAP.
 * @return Any error returned from @ref _libconfig_setting_lookup_string().
 */
static Error_t _parse_buttonbind_click( config_setting_t *setting, unsigned int *click ) {

	RETURN_VALUE_IF_NULL( setting, ERROR_NULL_VALUE, "%s:\"setting\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_VALUE_IF_NULL( click, ERROR_NULL_VALUE, "%s:\"click\"\n", POINTER_NULL_PRINT_STRING );

	const char *click_string = NULL;
	const Error_t lookup_error = _libconfig_lookup_string( setting, "click", &click_string );

	if ( lookup_error != ERROR_NONE ) return lookup_error;

	for ( unsigned int i = 0; i < LENGTH( CLICK_ALIAS_MAP ); i++ ) {
		if ( strcasecmp( click_string, CLICK_ALIAS_MAP[ i ].alias ) == 0 ) {
			*click = CLICK_ALIAS_MAP[ i ].click;
			return ERROR_NONE;
		}
	}

	return ERROR_NOT_FOUND;
}

/**
 * @brief Parse a list of buttonbinds from a libconfig configuration into a list of Button structs.
 *
 * TODO
 *
 * @param[in] config Pointer to the libconfig configuration containing the buttonbinds to be parsed into @p array.
 * @param[out] array Pointer to where to dynamically allocate and store all the parsed buttonbinds.
 * @param[out] count Pointer to where to store the number of buttonbinds to be parsed. This value does not take into
 * account any failures during parsing the buttonbinds.
 * @param[out] malloced Boolean to track if @p array has been dynamically allocated yet or not, analogous to if the
 * default buttonbinds are being used or not. Value is set to `false` after allocation.
 *
 * @return TODO
 *
 * @note TODO Maybe mention dynamic allocation in _parse_binds_config()?
 */
static Errors_t _parse_buttonbinds_config( const config_t *config, Button **array, unsigned int *count, bool *malloced ) {

	Errors_t returned_errors = { 0 };
	const char *lookup_path = "buttonbinds";

	RETURN_ERRORS_IF_NULL( config, returned_errors, "%s:\"config\"\n", POINTER_NULL_PRINT_STRING );

	config_setting_t *array_setting = NULL;
	const Error_t lookup_error = _libconfig_generic_lookup( config_root_setting( config ), lookup_path, CONFIG_TYPE_LIST, &array_setting );
	add_error( &returned_errors, lookup_error );

	if ( lookup_error != ERROR_NONE ) {
		LOG_ERROR( "Lookup of \"%s\" failed: %s\n", lookup_path, ERROR_ENUM_STRINGS[ lookup_error ] );
		return returned_errors;
	}

	const Errors_t array_errors = _parse_setting_array( array_setting, sizeof( Button ), _parse_buttonbind_adapter, malloced, (void **) array, count );
	copy_errors( &returned_errors, array_errors );

	return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] setting TODO
 * @param[in] index TODO
 * @param[out] font TODO
 *
 * @return TODO
 */
static Errors_t _parse_font( config_setting_t *setting, const unsigned int index, const char **font ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( setting, returned_errors, "%s:\"setting\" at index %d\n", POINTER_NULL_PRINT_STRING, index );

	if ( config_setting_type( setting ) != CONFIG_TYPE_STRING ) {
		LOG_WARN( "Font element at index %u is not a string\n", index );
		add_error( &returned_errors, ERROR_TYPE );
		return returned_errors;
	}

	*font = config_setting_get_string( setting );

	if ( *font == NULL ) {
		LOG_WARN( "Failed to parse theme font at index %u for unknown an reason\n", index );
		add_error( &returned_errors, ERROR_NULL_VALUE );
		return returned_errors;
	}

	return returned_errors;
}

/**
 * @brief Thin adapter function to allow for font parsing inside a generic array parsing function.
 *
 * See @ref _parse_font() for function documentation.
 */
static Errors_t _parse_font_adapter( config_setting_t *setting, const unsigned int index, void *font ) {
	return _parse_font( setting, index, (const char **) font );
}

/**
 * @brief Parse generic configuration settings from a libconfig configuration.
 *
 * TODO
 *
 * @param[in] config Pointer to the libconfig configuration containing the generic settings to be parsed.
 *
 * @return TODO
 */
static Errors_t _parse_generic_settings( const config_t *config ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( config, returned_errors, "%s:\"config\"\n", POINTER_NULL_PRINT_STRING );

	for ( unsigned int i = 0; i < LENGTH( SETTING_ALIAS_MAP ); ++i ) {
		Error_t returned_error = ERROR_NONE;

		switch ( SETTING_ALIAS_MAP[ i ].type ) {
			case TYPE_BOOLEAN:
				returned_error = _libconfig_generic_lookup( config_root_setting( config ), SETTING_ALIAS_MAP[ i ].alias, CONFIG_TYPE_BOOL, SETTING_ALIAS_MAP[ i ].setting );
				break;

			case TYPE_INT:
				returned_error = _libconfig_lookup_int( config_root_setting( config ), SETTING_ALIAS_MAP[ i ].alias, (int) SETTING_ALIAS_MAP[ i ].range_min,
				                                        (int) SETTING_ALIAS_MAP[ i ].range_max, SETTING_ALIAS_MAP[ i ].setting );
				break;

			case TYPE_UINT:
				returned_error = _libconfig_lookup_uint( config_root_setting( config ), SETTING_ALIAS_MAP[ i ].alias, (unsigned int) SETTING_ALIAS_MAP[ i ].range_min,
				                                         (unsigned int) SETTING_ALIAS_MAP[ i ].range_max, SETTING_ALIAS_MAP[ i ].setting );
				break;

			case TYPE_FLOAT:
				returned_error = _libconfig_lookup_float( config_root_setting( config ), SETTING_ALIAS_MAP[ i ].alias, (float) SETTING_ALIAS_MAP[ i ].range_min,
				                                          (float) SETTING_ALIAS_MAP[ i ].range_max, SETTING_ALIAS_MAP[ i ].setting );
				break;

			case TYPE_STRING:
				returned_error = _libconfig_lookup_string( config_root_setting( config ), SETTING_ALIAS_MAP[ i ].alias, SETTING_ALIAS_MAP[ i ].setting );
				break;

			default:
				returned_error = ERROR_TYPE;
				LOG_WARN( "Setting \"%s\" is programmed with an invalid type\n", SETTING_ALIAS_MAP[ i ].alias );
				break;
		}

		if ( returned_error != ERROR_NONE ) {
			if ( returned_error == ERROR_NOT_FOUND && SETTING_ALIAS_MAP[ i ].optional ) {
				LOG_DEBUG( "\"%s\" was not found but is flagged as optional, continuing", SETTING_ALIAS_MAP[ i ].alias );
			} else {
				add_error( &returned_errors, returned_error );
				LOG_WARN( "Issue while parsing \"%s\": %s\n", SETTING_ALIAS_MAP[ i ].alias, ERROR_ENUM_STRINGS[ returned_error ] );
			}
		}
	}

	return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] setting TODO
 * @param[in] index TODO
 * @param[out] keybind TODO
 *
 * @return TODO
 */
static Errors_t _parse_keybind( config_setting_t *setting, const unsigned int index, Key *keybind ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( setting, returned_errors, "%s:\"setting\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_ERRORS_IF_NULL( keybind, returned_errors, "%s:\"keybind\"\n", POINTER_NULL_PRINT_STRING );

	const Errors_t bind_core_errors = _parse_bind_core( setting, index, &keybind->mod, &keybind->func, &keybind->arg );
	copy_errors( &returned_errors, bind_core_errors );

	const Error_t keysym_error = _parse_keybind_keysym( setting, &keybind->keysym );
	add_error( &returned_errors, keysym_error );

	if ( keysym_error != ERROR_NONE ) {
		LOG_WARN( "Keybind %d invalid, unable to parse the bind's key: %s\n", index + 1, ERROR_ENUM_STRINGS[ keysym_error ] );
	}

	// Ensure key is zeroed and doesn't accidentally have any
	// data left over from parsing that could cause unusual behavior
	if ( errors_failure_count( &returned_errors ) != 0 ) {
		keybind->mod = 0;
		keybind->keysym = NoSymbol;
		keybind->func = NULL;
		keybind->arg.i = 0;
	}

	return returned_errors;
}

/**
 * @brief Thin adapter function to allow for keybind parsing inside a generic array parsing function.
 *
 * See @ref _parse_keybind() for function documentation.
 */
static Errors_t _parse_keybind_adapter( config_setting_t *setting, const unsigned int index, void *keybind ) {
	return _parse_keybind( setting, index, (Key *) keybind );
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] setting TODO
 * @param[out] keysym Pointer to where to store the parsed keysym value.
 *
 * @return @ref ERROR_NONE on success.
 * @return @ref ERROR_NULL_VALUE if a NULL argument is provided.
 * @return @ref ERROR_NOT_FOUND if unable to convert parsed string to keysym.
 * @return Any error returned from @ref _libconfig_setting_lookup_string().
 *
 * @note `xev` is likely your best bet at finding the keysym values that will work with `XStringToKeysym()`.
 * If someone knows a better way, please reach out and let me know.
 *
 * @see https://gitlab.freedesktop.org/xorg/app/xev
 * @see https://gitlab.freedesktop.org/xorg/lib/libx11/-/blob/master/src/StrKeysym.c?ref_type=heads#L74
 */
static Error_t _parse_keybind_keysym( config_setting_t *setting, KeySym *keysym ) {

	RETURN_VALUE_IF_NULL( setting, ERROR_NULL_VALUE, "%s:\"setting\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_VALUE_IF_NULL( keysym, ERROR_NULL_VALUE, "%s:\"keysym\"\n", POINTER_NULL_PRINT_STRING );

	const char *keybind_string = NULL;
	const Error_t lookup_error = _libconfig_lookup_string( setting, "key", &keybind_string );

	if ( lookup_error != ERROR_NONE ) return lookup_error;

	*keysym = XStringToKeysym( keybind_string );

	if ( *keysym == NoSymbol ) {
		LOG_WARN( "Failed to convert string (\"%s\") to keysym\n", keybind_string );
		return ERROR_NOT_FOUND;
	}

	// The upper case return of XConvertCase(), which we don't use.
	KeySym unused = 0;
	XConvertCase( *keysym, keysym, &unused );

	return ERROR_NONE;
}

/**
 * @brief Parse a list of keybinds from a libconfig configuration into a list of
 * Key structs.
 *
 * TODO
 *
 * @param[in] config Pointer to the libconfig configuration containing the keybinds to be parsed into @p array.
 * @param[out] array Pointer to where to dynamically allocate memory and store all the parsed keybinds.
 * @param[out] count Pointer to where to store the number of keybinds to be parsed. This value does not take
 * into account any failures during parsing the keybinds.
 * @param[out] malloced Boolean to track if @p array has been dynamically allocated yet or not, analogous to if
 * the default keybinds are being used or not. Value is set to `false` after allocation.
 *
 * @return TODO
 *
 * @note TODO Maybe mention dynamic allocation in _parse_binds_config()?
 */
static Errors_t _parse_keybinds_config( const config_t *config, Key **array, unsigned int *count, bool *malloced ) {

	Errors_t returned_errors = { 0 };
	const char *lookup_path = "keybinds";

	RETURN_ERRORS_IF_NULL( config, returned_errors, "%s:\"config\"\n", POINTER_NULL_PRINT_STRING );

	config_setting_t *array_setting = NULL;
	const Error_t lookup_error = _libconfig_generic_lookup( config_root_setting( config ), lookup_path, CONFIG_TYPE_LIST, &array_setting );
	add_error( &returned_errors, lookup_error );

	if ( lookup_error != ERROR_NONE ) {
		LOG_ERROR( "Lookup of \"%s\" failed: %s\n", lookup_path, ERROR_ENUM_STRINGS[ lookup_error ] );
		return returned_errors;
	}

	const Errors_t array_errors = _parse_setting_array( array_setting, sizeof( Key ), _parse_keybind_adapter, malloced, (void **) array, count );
	copy_errors( &returned_errors, array_errors );

	return returned_errors;
}

/**
 * @brief Attempts to find, open, and store a valid libconfig configuration file.
 *
 * This function handles most of the high level logic regarding finding, opening,
 * validating, and storing a libconfig configuration file. This function will first
 * search for a custom configuration file, passed in using @p config_filepath. If that
 * is not found, a list of default directories are searched. If still no user configuration
 * is found, a user's last configuration backup will be attempted to be loaded. If again
 * unsuccessful, a system default configuration file will be loaded. Finally, if all else
 * fails, the function will return and the program will rely on the hardcoded default values
 * loaded from config.h
 *
 * @param[in] config TODO
 * @param[in] custom_config_filepath TODO
 * @param[out] found_config_filepath TODO
 * @param[out] fallback_config_loaded TODO
 *
 * @return TODO
 *
 * @todo These error returns may not be the most accurate, not sure exactly the best fits.
 * @todo Should the parser even look for another config file if one is passed from the CLI? Could be deceptive behavior.
 */
static Errors_t _parser_open_config_file( config_t *config, const char *custom_config_filepath, char **found_config_filepath, bool *fallback_config_loaded ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( config, returned_errors, "%s:\"config\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_ERRORS_IF_NULL( found_config_filepath, returned_errors, "%s:\"found_config_filepath\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_ERRORS_IF_NULL( fallback_config_loaded, returned_errors, "%s:\"fallback_config_loaded\"\n", POINTER_NULL_PRINT_STRING );

	char *xdg_config_home = get_xdg_config_home();
	char *xdg_data_home = get_xdg_data_home();

	const struct Filepath {
		const char *filepath_1;
		const char *filepath_2;
		const bool is_fallback_config;
	} config_filepaths[ ] = {
		{ custom_config_filepath, "", false },
		{ xdg_config_home, "/dwm.conf", false },
		{ xdg_config_home, "/dwm/dwm.conf", false },
		{ xdg_data_home, "/dwm/dwm_last.conf", true },
		{ "/etc/dwm.conf", "", true }
	};

	for ( unsigned int i = 0; i < LENGTH( config_filepaths ); i++ ) {
		char constructed_path[ PATH_MAX ];

		if ( config_filepaths[ i ].filepath_1 != NULL && config_filepaths[ i ].filepath_2 != NULL ) {
			snprintf( constructed_path, sizeof( constructed_path ), "%s%s", config_filepaths[ i ].filepath_1, config_filepaths[ i ].filepath_2 );
		} else {
			LOG_DEBUG( "Part of a path at index %d was NULL while trying top open a configuration file, skipping invalid path\n", i );
			continue;
		}

		LOG_DEBUG( "Attempting to open config file \"%s\"\n", constructed_path );

		FILE *configuration_file = fopen( constructed_path, "r" );

		if ( configuration_file == NULL ) {
			LOG_DEBUG( "Unable to open config file \"%s\"\n", constructed_path );
			continue;
		}

		if ( config_read( config, configuration_file ) == CONFIG_FALSE ) {
			LOG_WARN( "Problem parsing config file \"%s\", line %d: %s\n", constructed_path, config_error_line( config ), config_error_text( config ) );
			add_error( &returned_errors, ERROR_NULL_VALUE );
			fclose( configuration_file );
			continue;
		}

		*found_config_filepath = estrdup( constructed_path );
		if ( *found_config_filepath == NULL ) add_error( &returned_errors, ERROR_ALLOCATION );

		*fallback_config_loaded = config_filepaths[ i ].is_fallback_config;

		fclose( configuration_file );

		break;
	}

	free( xdg_config_home );
	free( xdg_data_home );

	return returned_errors;
}

/**
 * @brief Parse a rule from a libconfig configuration setting.
 *
 * TODO
 *
 * @param[in] setting Pointer to the libconfig setting containing the rule to be parsed into @p rule.
 * @param[in] index Index of the current rule in the larger array. Used purely for debug printing.
 * @param[out] rule Pointer to the Rule struct where the values parsed from @p setting are stored.
 *
 * @return TODO
 */
static Errors_t _parse_rule( config_setting_t *setting, const unsigned int index, Rule *rule ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( setting, returned_errors, "%s:\"config\" at index %d\n", POINTER_NULL_PRINT_STRING, index );
	RETURN_ERRORS_IF_NULL( rule, returned_errors, "%s:\"rule\" at index %d\n", POINTER_NULL_PRINT_STRING, index );

	add_error( &returned_errors, _libconfig_lookup_string( setting, "class", &rule->class ) );
	add_error( &returned_errors, _libconfig_lookup_string( setting, "instance", &rule->instance ) );
	add_error( &returned_errors, _libconfig_lookup_string( setting, "title", &rule->title ) );
	add_error( &returned_errors, _libconfig_lookup_uint( setting, "tag-mask", 0, TAGMASK, &rule->tags ) );
	add_error( &returned_errors, _libconfig_lookup_int( setting, "monitor", -1, 99, &rule->monitor ) );

	// This logically should be a boolean value, but I didn't want to
	// deviate from the coded type, so I kept it an int and range check it.
	add_error( &returned_errors, _libconfig_lookup_int( setting, "floating", 0, 1, &rule->isfloating ) );

	// Ensure rule is zeroed and doesn't accidentally have any
	// data left over from parsing that could cause unusual behavior
	if ( errors_failure_count( &returned_errors ) != 0 ) {
		rule->class = NULL;
		rule->instance = NULL;
		rule->title = NULL;
		rule->tags = 0;
		rule->isfloating = 0;
		rule->monitor = 0;
	}

	LOG_DEBUG( "Rule %d: class: \"%s\", instance: \"%s\", title: \"%s\", tag-mask: %d, monitor: %d, floating: %d\n", rule_index, parsed_rule->class, parsed_rule->instance, parsed_rule->title,
	           parsed_rule->tags, parsed_rule->monitor, parsed_rule->isfloating );

	return returned_errors;
}

/**
 * @brief Thin adapter function to allow for rule parsing inside a generic array parsing function.
 *
 * See @ref _parse_rule() for function documentation.
 */
static Errors_t _parse_rule_adapter( config_setting_t *setting, const unsigned int index, void *rule ) {
	return _parse_rule( setting, index, (Rule *) rule );
}

/**
 * @brief Parse a list of rules from a libconfig configuration into a list of Rule structs.
 *
 * TODO
 *
 * @param[in] config Pointer to the libconfig configuration containing the rules to be parsed into @p array.
 * @param[out] array Pointer to where to dynamically allocate memory and store all the parsed rules.
 * @param[out] count Pointer to where to store the number of rules to be parsed. This value does not take into account
 * any failures during parsing the rules.
 * @param[out] malloced Boolean to track if @p array has been dynamically allocated yet or not, analogous
 * to if the default rules are being used or not. Value is set to `false` after allocation.
 *
 * @return TODO
 *
 * @todo Check for NULL pointers from function arguments. Possible able to use lookup_path in logs instead of repeating message
 */
static Errors_t _parse_rules_config( const config_t *config, Rule **array, unsigned int *count, bool *malloced ) {

	Errors_t returned_errors = { 0 };
	const char *lookup_path = "rules";

	RETURN_ERRORS_IF_NULL( config, returned_errors, "%s:\"config\"\n", POINTER_NULL_PRINT_STRING );

	config_setting_t *array_setting = NULL;
	const Error_t lookup_error = _libconfig_generic_lookup( config_root_setting( config ), lookup_path, CONFIG_TYPE_LIST, &array_setting );
	add_error( &returned_errors, lookup_error );

	if ( lookup_error != ERROR_NONE ) {
		LOG_ERROR( "Lookup of \"%s\" failed: %s\n", lookup_path, ERROR_ENUM_STRINGS[ lookup_error ] );
		return returned_errors;
	}

	const Errors_t array_errors = _parse_setting_array( array_setting, sizeof( Rule ), _parse_rule_adapter, malloced, (void **) array, count );
	copy_errors( &returned_errors, array_errors );

	return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] setting TODO
 * @param[in] element_size TODO
 * @param[in] array_element_parser_function TODO
 * @param[in,out] malloced TODO
 * @param[in,out] parsed_config TODO
 * @param[out] parsed_config_length TODO
 *
 * @return TODO
 *
 * @todo It may be worth trying to add some kind of safeguard to fall back on default config if enough
 * elements fail to be parsed. For example, if the keybinds all (or many) fail, it could soft-lock the
 * user in the program. Not sure best way to do that, or if it is even the best idea to add.
 *
 * @todo The logic around @p malloced and null checking @p parsed_config before allocation is brittle, needs work.
 */
static Errors_t _parse_setting_array( const config_setting_t *setting, const size_t element_size, const Array_Element_Parser_Function_t array_element_parser_function, bool *malloced,
                                      void **parsed_config, unsigned int *parsed_config_length ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( setting, returned_errors, "%s:\"setting\"\n", POINTER_NULL_PRINT_STRING );
	RETURN_ERRORS_IF_NULL( parsed_config_length, returned_errors, "%s:\"parsed_config_length\"\n", POINTER_NULL_PRINT_STRING );

	const char *setting_name = NULL;
	const Error_t setting_name_error = _libconfig_get_setting_name( setting, &setting_name );

	if ( setting_name_error != ERROR_NONE ) {
		LOG_WARN( "Unable to acquire setting name, logs in this function may not print correctly: %s\n", ERROR_ENUM_STRINGS[ setting_name_error ] );
	}

	*parsed_config_length = config_setting_length( setting );
	if ( *parsed_config_length == 0 ) {
		LOG_ERROR( "No %s listed. Default values will be used\n", setting_name );
		add_error( &returned_errors, ERROR_NOT_FOUND );
		return returned_errors;
	}

	LOG_DEBUG( "%u elements detected in \"%s\"\n", *parsed_config_length, setting_name );

	// malloced is also used to determine if we should dynamically
	// allocate the array or if it is already allocated some other
	// way, usually on the stack, as well as report that it has
	// been dynamically allocated here.
	if ( malloced != NULL && parsed_config != NULL ) {

		if ( element_size == 0 ) {
			LOG_ERROR( "Cannot allocate memory correctly if element size to allocate is 0\n" );
			add_error( &returned_errors, ERROR_RANGE );
			return returned_errors;
		}

		LOG_DEBUG( "Dynamically allocating %lu bytes of memory for \"%s\"\n", *parsed_config_length * element_size, setting_name );

		errno = 0;
		void *calloced_memory = calloc( *parsed_config_length, element_size );

		if ( calloced_memory == NULL ) {
			LOG_ERROR( "Failed to allocate %lu bytes for \"%s\": %s\n", *parsed_config_length * element_size, setting_name, strerror(errno) );
			add_error( &returned_errors, ERROR_ALLOCATION );
			return returned_errors;
		}

		*parsed_config = calloced_memory;
		*malloced = true;
	}

	for ( unsigned int i = 0; i < *parsed_config_length; i++ ) {

		config_setting_t *child_setting = config_setting_get_elem( setting, i );

		RETURN_ERRORS_IF_NULL( child_setting, returned_errors, "Element index %u in \"%s\" returned NULL\n", i, setting_name );

		// Yes, parsed_config can be NULL, that is intended. It is used by things like
		// theme or tag parsing that rely on global values.
		void *element = parsed_config ? (char *) ( *parsed_config ) + ( i * element_size ) : NULL;

		const Errors_t parsing_error = array_element_parser_function( child_setting, i, element );

		if ( errors_failure_count( &parsing_error ) ) {
			LOG_WARN( "\"%s\" element number %d failed to be parsed. It had %d errors\n", setting_name, i + 1, errors_failure_count( &parsing_error ) );
			copy_errors( &returned_errors, parsing_error );
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
 * @param[in] setting TODO
 * @param[out] index TODO
 *
 * @return TODO
 */
static Errors_t _parse_tag( config_setting_t *setting, const unsigned int index ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( setting, returned_errors, "%s:\"setting\" at index %d\n", POINTER_NULL_PRINT_STRING, index );

	if ( index >= LENGTH( tags ) ) {
		LOG_WARN( "Parsed tag's index exceeds limits of the tags array\n" );
		add_error( &returned_errors, ERROR_RANGE );
		return returned_errors;
	}

	const char *original_tag_name = tags[ index ];

	if ( config_setting_type( setting ) != CONFIG_TYPE_STRING ) {
		LOG_WARN( "Tag element at index %u is not a string\n", index );
		add_error( &returned_errors, ERROR_TYPE );
		return returned_errors;
	}

	tags[ index ] = config_setting_get_string( setting );

	if ( tags[ index ] == NULL ) {
		tags[ index ] = original_tag_name;
		LOG_WARN( "Failed to parse tag at index %u for unknown an reason\n", index );
		add_error( &returned_errors, ERROR_NULL_VALUE );
		return returned_errors;
	}

	return returned_errors;
}

/**
 * @brief Thin adapter function to allow for tag parsing inside a generic array parsing function.
 *
 * See @ref _parse_tag() for function documentation.
 */
static Errors_t _parse_tags_adapter( config_setting_t *setting, const unsigned int index, void *unused ) {
	return _parse_tag( setting, index );
}

/**
 * @brief Parse a list of tags from a libconfig configuration.
 *
 * TODO
 *
 * @param[in] config Pointer to the libconfig configuration containing the tags to be parsed.
 *
 * @return TODO
 */
static Errors_t _parse_tags_config( const config_t *config ) {

	const char *lookup_path = "tags";
	unsigned int tags_count = 0;
	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( config, returned_errors, "%s:\"config\"\n", POINTER_NULL_PRINT_STRING );

	config_setting_t *array_setting = NULL;
	const Error_t lookup_error = _libconfig_generic_lookup( config_root_setting( config ), lookup_path, CONFIG_TYPE_LIST, &array_setting );
	add_error( &returned_errors, lookup_error );

	if ( lookup_error != ERROR_NONE ) {
		LOG_ERROR( "Lookup of \"%s\" failed: %s\n", lookup_path, ERROR_ENUM_STRINGS[ lookup_error ] );
		return returned_errors;
	}

	const Errors_t array_errors = _parse_setting_array( array_setting, 0, _parse_tags_adapter, NULL, NULL, &tags_count );
	copy_errors( &returned_errors, array_errors );

	if ( tags_count > LENGTH( tags ) ) {
		LOG_WARN( "More than %lu tag names detected (%d were detected) while parsing config, only the first %lu will be used\n", LENGTH( tags ), tags_count, LENGTH( tags ) );
	} else if ( tags_count < LENGTH( tags ) ) {
		LOG_WARN( "Less than %lu tag names detected while parsing config, default tags will be used for the remainder\n", LENGTH( tags ) );
	}

	return returned_errors;
}

/**
 * @brief Parse a theme from a libconfig configuration setting.
 *
 * TODO
 *
 * @param[in] setting Pointer to the libconfig setting containing the theme to be parsed.
 * @param[in] index Index of the current theme being parsed.
 *
 * @return TODO
 */
static Errors_t _parse_theme( config_setting_t *setting, const unsigned int index ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( setting, returned_errors, "%s:\"setting\" at index %d\n", POINTER_NULL_PRINT_STRING, index );

	// dwm does not support more than 1 theme
	if ( index > 0 ) {
		LOG_WARN( "%d themes detected. dwm can only supports using 1 theme\n", index + 1 );
		add_error( &returned_errors, ERROR_RANGE );
		return returned_errors;
	}

	const char *fonts_lookup_path = "fonts";
	config_setting_t *array_setting = NULL;
	const Error_t lookup_error = _libconfig_generic_lookup( setting, fonts_lookup_path, CONFIG_TYPE_LIST, &array_setting );
	add_error( &returned_errors, lookup_error );

	if ( lookup_error != ERROR_NONE ) {
		LOG_ERROR( "Lookup of \"%s\" failed: %s\n", fonts_lookup_path, ERROR_ENUM_STRINGS[ lookup_error ] );
		return returned_errors;
	}

	const Errors_t font_errors = _parse_setting_array( array_setting, sizeof( char * ), _parse_font_adapter, &fonts_malloced, (void **) &fonts, &fonts_count );
	copy_errors( &returned_errors, font_errors );

	for ( unsigned int i = 0; i < LENGTH( THEME_ALIAS_MAP ); i++ ) {
		const Error_t error = _libconfig_lookup_string( setting, THEME_ALIAS_MAP[ i ].alias, THEME_ALIAS_MAP[ i ].color );
		add_error( &returned_errors, error );
		if ( error != ERROR_NONE ) {
			LOG_WARN( "Failed to parse theme %d's element \"%s\": %s\n", index, THEME_ALIAS_MAP[ i ].alias, ERROR_ENUM_STRINGS[ error ] );
		}
	}

	return returned_errors;
}

/**
 * @brief Thin adapter function to allow for theme parsing inside a generic array parsing function.
 *
 * See @ref _parse_theme() for function documentation.
 */
static Errors_t _parse_theme_adapter( config_setting_t *setting, const unsigned int index, void *unused ) {
	return _parse_theme( setting, index );
}

/**
 * @brief Parse a list of themes from a libconfig configuration.
 *
 * TODO
 *
 * @param[in] config Pointer to the libconfig configuration containing the theme to be parsed.
 *
 * @return TODO
 *
 * @note dwm only supports a single theme, so only the first theme in the list is parsed.
 */
static Errors_t _parse_theme_config( const config_t *config ) {

	Errors_t returned_errors = { 0 };
	const char *lookup_path = "themes";

	RETURN_ERRORS_IF_NULL( config, returned_errors, "%s:\"config\"\n", POINTER_NULL_PRINT_STRING );

	config_setting_t *array_setting = NULL;
	const Error_t lookup_error = _libconfig_generic_lookup( config_root_setting( config ), lookup_path, CONFIG_TYPE_LIST, &array_setting );
	add_error( &returned_errors, lookup_error );

	if ( lookup_error != ERROR_NONE ) {
		LOG_ERROR( "Lookup of \"%s\" failed: %s\n", lookup_path, ERROR_ENUM_STRINGS[ lookup_error ] );
		return returned_errors;
	}

	unsigned int unused = 0;
	const Errors_t array_errors = _parse_setting_array( array_setting, 0, _parse_theme_adapter, NULL, NULL, &unused );
	copy_errors( &returned_errors, array_errors );

	return returned_errors;
}

/////////////////////////////////////////////
///// Parser internal utility functions /////
/////////////////////////////////////////////

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] parent_setting TODO
 * @param[in] path TODO
 * @param[in] expected_type TODO
 * @param[out] parsed_value TODO
 *
 * @return @ref ERROR_NONE on success.
 * @return @ref ERROR_NULL_VALUE if a NULL argument is provided.
 * @return @ref ERROR_NOT_FOUND if lookup failed.
 * @return @ref ERROR_TYPE if type found at @p path does not match
 * @p expected_type or if there is an unexpected type in the switch case.
 */
static Error_t _libconfig_generic_lookup( config_setting_t *parent_setting, const char *path, const int expected_type, void *parsed_value ) {

	if ( parent_setting == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;

	config_setting_t *child_setting = config_setting_lookup( parent_setting, path );

	if ( child_setting == NULL ) return ERROR_NOT_FOUND;

	if ( config_setting_type( child_setting ) != expected_type ) return ERROR_TYPE;

	switch ( expected_type ) {
		case CONFIG_TYPE_STRING:
			*(const char **) parsed_value = config_setting_get_string( child_setting );
			break;

		case CONFIG_TYPE_INT:
			*(int *) parsed_value = config_setting_get_int( child_setting );
			break;

		case CONFIG_TYPE_INT64:
			*(long long *) parsed_value = config_setting_get_int64( child_setting );
			break;

		case CONFIG_TYPE_FLOAT:
			*(float *) parsed_value = config_setting_get_float( child_setting );
			break;

		case CONFIG_TYPE_BOOL:
			*(bool *) parsed_value = config_setting_get_bool( child_setting );
			break;

		case CONFIG_TYPE_GROUP:
		case CONFIG_TYPE_ARRAY:
		case CONFIG_TYPE_LIST:
			*(config_setting_t **) parsed_value = child_setting;
			break;

		default:
			return ERROR_TYPE;
	}

	return ERROR_NONE;
}

/**
 * @brief Returns the name of a given setting, or its nearest named parent setting.
 *
 * TODO
 *
 * @param[in] setting TODO
 * @param[out] found_name TODO
 *
 * @return @ref ERROR_NONE on success.
 * @return @ref ERROR_NULL_VALUE if a NULL argument is provided.
 * @return @ref ERROR_NOT_FOUND if no named parent settings are found.
 */
static Error_t _libconfig_get_setting_name( const config_setting_t *setting, const char **found_name ) {

	if ( setting == NULL || found_name == NULL ) return ERROR_NULL_VALUE;

	while ( setting != NULL ) {
		const char *tmp_string = config_setting_name( setting );

		if ( tmp_string != NULL ) {
			*found_name = tmp_string;
			return ERROR_NONE;
		}

		setting = config_setting_parent( setting );
	}

	return ERROR_NOT_FOUND;
}

/**
 * @brief Look up a float value in a libconfig setting.
 *
 * TODO
 *
 * @param[in] parent_setting Pointer to the libconfig setting to perform the lookup in.
 * @param[in] path Path expression to search within @p parent_setting.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return @ref ERROR_NONE on success.
 * @return @ref ERROR_NULL_VALUE if a NULL argument is provided.
 * @return @ref ERROR_RANGE if @p range_min exceeds @p range_max or
 * if the parsed value at @p path is outside the provided range.
 * @return Any error returned from @ref _libconfig_generic_lookup().
 */
static Error_t _libconfig_lookup_float( config_setting_t *parent_setting, const char *path, const float range_min, const float range_max, float *parsed_value ) {

	if ( parent_setting == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;
	if ( range_min > range_max ) return ERROR_RANGE;

	float tmp_float = 0;
	const Error_t lookup_error = _libconfig_generic_lookup( parent_setting, path, CONFIG_TYPE_FLOAT, &tmp_float );
	if ( lookup_error != ERROR_NONE ) return lookup_error;

	if ( tmp_float < range_min || tmp_float > range_max ) return ERROR_RANGE;

	*parsed_value = tmp_float;

	return ERROR_NONE;
}

/**
 * @brief Look up an integer value in a libconfig setting.
 *
 * TODO
 *
 * @param[in] parent_setting Pointer to the libconfig setting to perform the lookup in.
 * @param[in] path Path expression to search within @p parent_setting.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return @ref ERROR_NONE on success.
 * @return @ref ERROR_NULL_VALUE if a NULL argument is provided.
 * @return @ref ERROR_RANGE if @p range_min exceeds @p range_max or
 * if the parsed value at @p path is outside the provided range.
 * @return Any error returned from @ref _libconfig_generic_lookup().
 */
static Error_t _libconfig_lookup_int( config_setting_t *parent_setting, const char *path, const int range_min, const int range_max, int *parsed_value ) {

	if ( parent_setting == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;
	if ( range_min > range_max ) return ERROR_RANGE;

	int tmp_int = 0;
	const Error_t lookup_error = _libconfig_generic_lookup( parent_setting, path, CONFIG_TYPE_INT, &tmp_int );
	if ( lookup_error != ERROR_NONE ) return lookup_error;

	if ( tmp_int < range_min || tmp_int > range_max ) return ERROR_RANGE;

	*parsed_value = tmp_int;

	return ERROR_NONE;
}

/**
 * @brief Look up a string value in a libconfig setting.
 *
 * TODO
 *
 * @param[in] parent_setting Pointer to the libconfig setting to perform the lookup in.
 * @param[in] path Path expression to search within @p parent_setting.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return @ref ERROR_NONE on success.
 * @return @ref ERROR_NULL_VALUE if a NULL argument is provided.
 * @return Any error returned from @ref _libconfig_generic_lookup().
 */
static Error_t _libconfig_lookup_string( config_setting_t *parent_setting, const char *path, const char **parsed_value ) {

	if ( parent_setting == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;

	const Error_t lookup_error = _libconfig_generic_lookup( parent_setting, path, CONFIG_TYPE_STRING, parsed_value );
	if ( lookup_error != ERROR_NONE ) return lookup_error;

	if ( *parsed_value && strcasecmp( *parsed_value, "NULL" ) == 0 ) {
		*parsed_value = NULL;
	}

	return ERROR_NONE;
}

/**
 * @brief Look up an unsigned integer value in a libconfig setting.
 *
 * TODO
 *
 * @param[in] parent_setting Pointer to the libconfig setting to perform the lookup in.
 * @param[in] path Path expression to search within @p parent_setting.
 * @param[in] range_min Minimum value that can be saved to @p parsed_value.
 * @param[in] range_max Maximum value that can be saved to @p parsed_value.
 * @param[out] parsed_value Pointer to where the parsed value will be stored on success.
 *
 * @return @ref ERROR_NONE on success.
 * @return @ref ERROR_NULL_VALUE if a NULL argument is provided.
 * @return @ref ERROR_RANGE if @p range_min exceeds @p range_max or if the
 * parsed value at @p path is less than zero or outside the provided range.
 * @return Any error returned from @ref _libconfig_generic_lookup().
 */
static Error_t _libconfig_lookup_uint( config_setting_t *parent_setting, const char *path, const unsigned int range_min, const unsigned int range_max, unsigned int *parsed_value ) {

	if ( parent_setting == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;
	if ( range_min > range_max ) return ERROR_RANGE;

	int tmp_int = 0;
	const Error_t lookup_error = _libconfig_generic_lookup( parent_setting, path, CONFIG_TYPE_INT, &tmp_int );
	if ( lookup_error != ERROR_NONE ) return lookup_error;

	if ( tmp_int < 0 ) return ERROR_RANGE;

	const unsigned int tmp_uint = (unsigned int) tmp_int;

	if ( tmp_uint < range_min || tmp_uint > range_max ) return ERROR_RANGE;

	*parsed_value = tmp_uint;

	return ERROR_NONE;
}
