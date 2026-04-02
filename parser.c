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
 * them (many of the utility functions) has been credited accordingly.
 *
 * @todo Finish documentation. Make sure function arguments are noted for being dynamically allocated in that function or its sub functions.
 * @todo Overhaul printing / logging to match the new error handling.
 * @todo It may be worth going back to having a header, this file is very heavy.
 * @todo Find a way to smoothen / harden the length checking of keys, buttons, and rules when other patches are added
 */

#include <errno.h>
#include <libconfig.h>
#include <libgen.h>
#include <limits.h>
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

// Comment or uncomment as necessary to customize the level of logging.
// I would prefer a proper logging system, but that seems out the scope
// of this patch, and this works good enough for the patch's needs.

//#define LOG_TRACE( ... ) _LOG( "TRACE", __VA_ARGS__ ) // Unused, but you are welcome to enable and use it.
#define LOG_DEBUG( ... ) //_LOG( "DEBUG", __VA_ARGS__ )
#define LOG_INFO( ... ) _LOG( "INFO ", __VA_ARGS__ )
#define LOG_WARN( ... ) _LOG( "WARN ", __VA_ARGS__ )
#define LOG_ERROR( ... ) _LOG( "ERROR", __VA_ARGS__ )
#define LOG_FATAL( ... ) _LOG( "FATAL", __VA_ARGS__ )

#define _LOG( LEVEL, ... )\
	fprintf( stdout, LEVEL " [" __FILE__ "::%s::" TOSTRING(__LINE__) "]: ", __func__ );\
	fprintf( stdout, "" __VA_ARGS__ )

// Macros to simplify a commonly used code structure to check and
// deal with NULL function argument values like pointers or strings.
#define RETURN_ERRORS_IF_NULL( VALUE, ERRORS_STRUCT, ... )\
	if( VALUE == NULL ){\
		LOG_ERROR( __VA_ARGS__ );\
		add_error( &ERRORS_STRUCT, ERROR_NULL_VALUE );\
		return ERRORS_STRUCT;\
	}

#define RETURN_VALUE_IF_NULL( VALUE, ERROR, ... )\
	if( VALUE == NULL ){\
		LOG_ERROR( __VA_ARGS__ );\
		return ERROR;\
	}

#define RETURN_IF_NULL( VALUE, ... )\
	if( VALUE == NULL ){\
		LOG_ERROR( __VA_ARGS__ );\
		return;\
	}

#define SET_STATUS_TEXT( ... )\
	snprintf( stext, sizeof( stext ), "" __VA_ARGS__ );\
	XStoreName(dpy, root, stext);\
	XSync(dpy, False);

// Enum used to keep track of what kind of data
// is to be stored in an Arg struct. This is
// needed to properly parse the correct type
// of data for whatever variable the data will
// be stored in.
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

// TODO: Maybe reword IO vs NOT_FOUND, instead do FILE_NOT_FOUND and VALUE_NOT_FOUND?
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
	unsigned int errors_count[ ERROR_ENUM_LENGTH ];
} Errors_t;

// Alias libconfig structs for better name
// separation from the parser's configuration struct
typedef config_t Libconfig_Config_t;
typedef config_setting_t Libconfig_Setting_t;

// Typedef used to abstract config array parsing functions.
// Allows for more generic parsing of multiple types.
typedef Errors_t ( *Array_Element_Parser_Function_t )( Libconfig_Setting_t *element_setting, unsigned int element_index, void *parsed_element );

/// Global Variables ///
char *config_filepath = NULL;
static Libconfig_Config_t libconfig_config = { 0 };

Key *keys = default_keys;
unsigned int keys_count = LENGTH( default_keys );
static bool keys_malloced = false;

Button *buttons = default_buttons;
unsigned int buttons_count = LENGTH( default_buttons );
static bool buttons_malloced = false;

Rule *rules = default_rules;
unsigned int rules_count = LENGTH( default_buttons );
static bool rules_malloced = false;

const char **fonts = default_fonts;
unsigned int fonts_count = LENGTH( default_fonts );
static bool fonts_malloced = false;

/// Public parser functions ///
void config_cleanup( void );
Errors_t parse_config( void );

/// Public utility functions ///
void add_error( Errors_t *errors, Error_t error );
int errors_failure_count( const Errors_t *errors );
char *estrdup( const char *string );
void extend_string( char **source_string, const char *addition );
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
static Error_t _parser_backup_config( Libconfig_Config_t *config );
static Error_t _parse_bind_argument( Libconfig_Setting_t *bind_setting, Data_Type_t argument_type, long double range_min, long double range_max, Arg *parsed_argument );
static Errors_t _parse_bind_core( Libconfig_Setting_t *bind_setting, unsigned int bind_index, unsigned int *parsed_modifier, void ( **parsed_function )( const Arg * ), Arg *parsed_argument );
static Error_t _parse_bind_function( Libconfig_Setting_t *bind_setting, void ( **parsed_function )( const Arg * ), Data_Type_t *parsed_argument_type, long double *parsed_range_min,
				     long double *parsed_range_max );
static Error_t _parse_bind_modifier( Libconfig_Setting_t *bind_setting, unsigned int *parsed_modifier );
static Errors_t _parse_buttonbind( Libconfig_Setting_t *buttonbind_setting, unsigned int buttonbind_index, Button *parsed_buttonbind );
static Errors_t _parse_buttonbind_adapter( Libconfig_Setting_t *buttonbind_setting, unsigned int buttonbind_index, void *parsed_buttonbind );
static Error_t _parse_buttonbind_button( Libconfig_Setting_t *buttonbind_setting, unsigned int *parsed_button );
static Error_t _parse_buttonbind_click( Libconfig_Setting_t *buttonbind_setting, unsigned int *parsed_click );
static Errors_t _parse_buttonbinds_config( const Libconfig_Config_t *config, Button **buttonbind_config, unsigned int *count, bool *malloced );
static Errors_t _parse_config_array( const Libconfig_Config_t *config, Libconfig_Setting_t *setting, const char *config_array_name, size_t element_size,
				     Array_Element_Parser_Function_t array_element_parser_function, bool *dynamically_allocated, void **parsed_config, unsigned int *parsed_config_length );
static Errors_t _parse_font( Libconfig_Setting_t *fonts_setting, unsigned int fonts_index, const char *parsed_font );
static Errors_t _parse_font_adapter( Libconfig_Setting_t *fonts_setting, unsigned int fonts_index, void *parsed_font );
static Errors_t _parse_generic_settings( const Libconfig_Config_t *config );
static Errors_t _parse_keybind( Libconfig_Setting_t *keybind_setting, unsigned int keybind_index, Key *parsed_keybind );
static Errors_t _parse_keybind_adapter( Libconfig_Setting_t *keybind_setting, unsigned int keybind_index, void *parsed_keybind );
static Error_t _parse_keybind_keysym( Libconfig_Setting_t *keybind_setting, KeySym *parsed_keysym );
static Errors_t _parse_keybinds_config( const Libconfig_Config_t *config, Key **keybind_config, unsigned int *keybinds_count, bool *keybinds_dynamically_allocated );
static Errors_t _parser_open_config_file( Libconfig_Config_t *config, char **custom_config_filepath,bool *fallback_config_loaded );
static Error_t _parser_resolve_include_directory( Libconfig_Config_t *config, const char *parsed_config_filepath );
static Errors_t _parse_rule( Libconfig_Setting_t *rule_setting, unsigned int rule_index, Rule *parsed_rule );
static Errors_t _parse_rule_adapter( Libconfig_Setting_t *rule_setting, unsigned int rule_index, void *parsed_rule );
static Errors_t _parse_rules_config( const Libconfig_Config_t *config, Rule **rules_config, unsigned int *rules_count, bool *rules_dynamically_allocated );
static Errors_t _parse_tag( Libconfig_Setting_t *tags_setting, unsigned int tags_index );
static Errors_t _parse_tags_adapter( Libconfig_Setting_t *tags_setting, unsigned int tags_index, void *unused );
static Errors_t _parse_tags_config( const Libconfig_Config_t *config );
static Errors_t _parse_theme( Libconfig_Setting_t *theme_setting, unsigned int theme_index );
static Errors_t _parse_theme_adapter( Libconfig_Setting_t *theme_setting, unsigned int theme_index, void *unused );
static Errors_t _parse_theme_config( const Libconfig_Config_t *config );

/// Parser internal utility functions ///
static Error_t _libconfig_get_setting_name( const Libconfig_Setting_t *setting, const char **found_name );
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
 * @return TODO
 *
 * @todo Polish the status texts a little. I like the idea but could be refined.
 *
 * @authors JeffOfBread <jeffofbreadcoding@gmail.com>
 *
 * @see https://github.com/JeffofBread/dwm-libconfig
 */
Errors_t parse_config( void ) {

	Errors_t errors = { 0 };

	config_init( &libconfig_config );

	bool fallback_config_loaded = false;
	merge_errors( &errors, _parser_open_config_file( &libconfig_config, &config_filepath, &fallback_config_loaded ) );

	// Exit the parser if we haven't acquired a configuration file.
	// Without a configuration file, there isn't a reason to continue parsing.
	// The program will have to rely on the hardcoded default values instead.
	if ( config_filepath == NULL ) {
		LOG_ERROR( "Unable to load any configs. Hardcoded default config values will be used. Exiting parsing\n" );
		SET_STATUS_TEXT( "Parser failed to load config file" );
		config_destroy( &libconfig_config );
		return errors;
	}

	LOG_INFO( "Path to config file: \"%s\"\n", config_filepath );

	add_error( &errors, _parser_resolve_include_directory( &libconfig_config, config_filepath ) );

	config_set_options( &libconfig_config, CONFIG_OPTION_AUTOCONVERT | CONFIG_OPTION_SEMICOLON_SEPARATORS );
	config_set_tab_width( &libconfig_config, 4 );

	merge_errors( &errors, _parse_generic_settings( &libconfig_config ) );
	merge_errors( &errors, _parse_keybinds_config( &libconfig_config, &keys, &keys_count, &keys_malloced ) );
	merge_errors( &errors, _parse_buttonbinds_config( &libconfig_config, &buttons, &buttons_count, &buttons_malloced ) );
	merge_errors( &errors, _parse_rules_config( &libconfig_config, &rules, &rules_count, &rules_malloced ) );
	merge_errors( &errors, _parse_tags_config( &libconfig_config ) );
	merge_errors( &errors, _parse_theme_config( &libconfig_config ) );

	// The error requirement being 0 may be a bit strict, I am not sure. May need
	// some relaxing or possibly come up with a better way of calculating if a config
	// passes, or is valid enough to warrant backing up.

	// TODO: This logic is clumsily structured, it should be improved. It also probably should include rules_malloced
	if ( errors_failure_count( &errors ) == 0 && keys_malloced && buttons_malloced && !fallback_config_loaded ) {
		const Error_t backup_error = _parser_backup_config( &libconfig_config );
		add_error( &errors, backup_error );
	} else {
		if ( !keys_malloced || !buttons_malloced ) {
			LOG_WARN( "Not saving config as backup, as hardcoded default bind values were used, not the user's\n" );
		}
		if ( fallback_config_loaded ) {
			LOG_WARN( "Not saving config as backup, as the parsed configuration file is a system fallback configuration\n" );
		}
		if ( errors_failure_count( &errors ) != 0 ) {
			LOG_WARN( "Not saving config as backup, as the parsed config had too many (%d) errors\n", errors_failure_count( &errors ) );
		}
	}

	SET_STATUS_TEXT( "Parser Errors: %u", errors_failure_count( &errors ) );

	return errors;
}

/// Public utility functions ///

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

	RETURN_IF_NULL( errors, "Pointer to errors NULL, cannot add error\n" );

	if ( error < ERROR_ENUM_LENGTH ) {
		errors->errors_count[ error ]++;
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

	RETURN_VALUE_IF_NULL( errors, -1, "Pointer to errors NULL, cannot count errors\n" );

	int count = 0;

	for ( int i = ERROR_NONE + 1; i < ERROR_ENUM_LENGTH; i++ ) {
		count += (int) errors->errors_count[ i ];
	}

	return count;
}

/**
 * @brief Simple wrapper around strdup() to provide error logging.
 *
 * This function acts as a simple wrapper around strdup() that provides
 * some NULL safety as well as error logging around strdup(). It can
 * be used as a drop in replacement for strdup() safely.
 *
 * @param[in] string String to be copied.
 *
 * @return NULL if allocation failed or @p string was NULL, or the pointer
 * to a dynamically allocated duplicate of @p string.
 *
 * @note Returned string is dynamically allocated and will need to be manually freed.
 */
char *estrdup( const char *string ) {

	RETURN_VALUE_IF_NULL( string, NULL, "String to duplicate is NULL\n" );

	errno = 0;
	char *return_string = strdup( string );

	RETURN_VALUE_IF_NULL( return_string, NULL, "strdup failed to copy \"%s\": %s\n", string, strerror(errno) );

	return return_string;
}

/**
 * @brief Extend one string with another.
 *
 * TODO
 *
 * @param[in,out] source_string Pointer to where the extended string will be stored. Must be NULL or heap allocated.
 * @param[in] addition Additional string to be appended to the string pointed to by @p source_string_pointer.
 *
 * @warning @p source_string_pointer must be NULL or heap allocated. If it points to a string
 * literal, realloc() will crash the program. See @ref join_strings() if you wish to append a string literal.
 *
 * @note @p source_string_pointer is or will be dynamically allocated and must be manually freed.
 *
 * @note This function is derived from [picom's](https://github.com/yshui/picom/) mstrextend().
 * Credit more or less goes to [Yuxuan Shui](yshuiv7@gmail.com), I just made some minor adjustments.
 *
 * @author Yuxuan Shui - <yshuiv7@gmail.com>
 *
 * @see https://github.com/yshui/picom
 * @see https://github.com/yshui/picom/blob/69539edc0638019e3dd88c67007e90ce7f51174e/src/utils/str.c#L54
 */
void extend_string( char **source_string, const char *addition ) {

	RETURN_IF_NULL( source_string, "Pointer to source string is NULL\n" );

	if ( *source_string == NULL ) {
		*source_string = estrdup( addition );
		LOG_WARN( "Source string was NULL, addition was copied into its place\n" );
		return;
	}

	const size_t length_1 = strlen( *source_string );
	const size_t length_2 = strlen( addition );
	const size_t total_length = length_1 + length_2 + 1;

	errno = 0;
	*source_string = realloc( *source_string, total_length * sizeof( char ) );

	RETURN_IF_NULL( *source_string, "realloc() failed to reallocate *source_string to %lu bytes: %s\n", total_length * sizeof( char ), strerror(errno) );

	strncpy( *source_string + length_1, addition, length_2 );
	( *source_string )[ total_length - 1 ] = '\0';
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
 * and will need to be manually freed.
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
 * @return Pointer of a dynamically allocated string containing the now joined strings,
 * or NULL on failure.
 *
 * @note Returned string is dynamically allocated and will need to be manually freed.
 *
 * @note This function is derived from [picom's](https://github.com/yshui/picom/) mstrjoin().
 * Credit more or less goes to [Yuxuan Shui](yshuiv7@gmail.com), I just made some minor adjustments.
 *
 * @note gcc warns about legitimate truncation worries in strncpy() in @ref join_strings().
 * `strncpy( joined_string, string_1, length_1 )` intentionally truncates the null byte
 * from @p string_1, however. `strncpy( joined_string + length_1, string_2, length_2 )`
 * uses bounds depending on the source argument, but `joined_string` is allocated with
 * `length_1 + length_2 + 1`, so this strncpy() can't overflow.
 *
 * @author Yuxuan Shui - <yshuiv7@gmail.com>
 *
 * @see https://github.com/yshui/picom
 * @see https://github.com/yshui/picom/blob/69961987e1238f9bc3af53fa0774fc19fdec44a4/src/utils/str.c#L24
 */
char *join_strings( const char *string_1, const char *string_2 ) {

	RETURN_VALUE_IF_NULL( string_1, NULL, "string_1 is NULL\n" );
	RETURN_VALUE_IF_NULL( string_2, NULL, "string_2 is NULL\n" );

	const size_t length_1 = strlen( string_1 );
	const size_t length_2 = strlen( string_2 );
	const size_t total_length = length_1 + length_2 + 1;

	errno = 0;
	char *joined_string = calloc( total_length, sizeof( char ) );

	RETURN_VALUE_IF_NULL( joined_string, NULL, "calloc() failed trying to join \"%s\" and \"%s\" (%lu bytes): %s\n", string_1, string_2, total_length * sizeof( char ), strerror(errno) )

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

	RETURN_VALUE_IF_NULL( path, -1, "Given path was NULL, unable to make directory path\n" );

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

	RETURN_IF_NULL( destination, "destination is NULL, no where to merge errors\n" );

	for ( int i = 0; i < ERROR_ENUM_LENGTH; i++ ) {
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
 * allocated and will need to be manually freed.
 *
 * @return TODO
 *
 * @note @p normalized_path can be dynamically allocated and will need to be manually freed.
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

	RETURN_VALUE_IF_NULL( original_path, -1, "Given path to normalize is NULL\n" );
	RETURN_VALUE_IF_NULL( normalized_path, -1, "Pointer to string to allocate and store normalized path is NULL\n" );

	const size_t original_length = strlen( original_path );

	errno = 0;
	*normalized_path = calloc( ( original_length + 1 ), sizeof( char ) );

	RETURN_VALUE_IF_NULL( *normalized_path, -1, "calloc() failed trying to allocate %lu bytes: %s\n", ( original_length + 1 ) * sizeof( char ), strerror(errno) );

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

	RETURN_VALUE_IF_NULL( *normalized_path, -1, "realloc() failed to reallocate *normalized_path to %lu bytes: %s\n", new_length * sizeof( char ), strerror(errno) );

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
		LOG_WARN( "setlayout_floating() failed to find floating layout in \"layouts\" array\n" );
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
		LOG_WARN( "setlayout_monocle() failed to find monocle layout in \"layouts\" array\n" );
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
		LOG_WARN( "setlayout_tiled() failed to find tile layout in \"layouts\" array\n" );
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

	RETURN_IF_NULL( arg, "arg is NULL\n" );
	RETURN_IF_NULL( arg->v, "String stored in arg is NULL\n" );

	// Process argv to work with default spawn() behavior
	const char *cmd = arg->v;
	char *argv[ ] = { "/bin/sh", "-c", (char *) cmd, NULL };
	LOG_DEBUG( "Attempting to spawn \"%s\"\n", (char *) cmd );

	// Call spawn with our new processed value
	const Arg tmp = { .v = argv };
	spawn( &tmp );
}

/// Parser internal functions ///

/**
 * @brief Backs up a libconfig configuration to disk.
 *
 * This function backs up the given libconfig @ref Libconfig_Config_t
 * @p config following the XDG specification. If the function
 * @ref get_xdg_data_home() fails to find the XDG data directory
 * (which is likely), we will default to using "~/.local/share/"
 * instead. Meaning that, in the latter case, the filepath to
 * the backup file will be "~/.local/share/dwm/dwm_last.conf".
 * "/dwm/dwm_last.conf" will always be appended to the end of
 * whatever path is returned by @ref get_xdg_data_home().
 * The backup file is then created and written to by libconfig's
 * config_write_file().
 *
 * @param[in] config Pointer to the libconfig configuration
 * to be backed up.
 *
 * @return TODO
 *
 * @todo This function may need a little TLC when it comes to logging
 */
static Error_t _parser_backup_config( Libconfig_Config_t *config ) {

	RETURN_VALUE_IF_NULL( config, ERROR_NULL_VALUE, "Pointer to configuration is NULL, unable to backup config\n" );

	// Save xdg data directory to buffer (~/.local/share)
	char *buffer = get_xdg_data_home();

	RETURN_VALUE_IF_NULL( buffer, ERROR_NOT_FOUND, "Failed to get necessary directory, unable to backup config\n" );

	// Append buffer (already has "~/.local/share" or other xdg data directory)
	// with the directory we want to backup the config to, create the directory
	// if it doesn't exist, and then extend with the filename we want to backup
	// to config in.
	extend_string( &buffer, "/dwm/" );
	RETURN_VALUE_IF_NULL( buffer, ERROR_NULL_VALUE, "Failed to extend buffer with necessary path section, unable to backup config\n" );

	if ( make_directory_path( buffer ) != 0 ) return ERROR_IO;

	extend_string( &buffer, "dwm_last.conf" );
	RETURN_VALUE_IF_NULL( buffer, ERROR_NULL_VALUE, "Failed to extend buffer with the backup files filename, unable to backup config\n" );

	if ( config_write_file( config, buffer ) == CONFIG_FALSE ) {
		LOG_ERROR( "Problem writing backup configuration to \"%s\"\n", buffer );
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

	RETURN_VALUE_IF_NULL( bind_setting, ERROR_NULL_VALUE, "Pointer to configuration setting is NULL, unable to parse the bind's argument\n" );
	RETURN_VALUE_IF_NULL( parsed_argument, ERROR_NULL_VALUE, "Pointer to where to store the parsed argument is NULL, unable to parse the bind's argument\n" );

	const char *path = "argument";
	Error_t lookup_error = ERROR_NONE;
	switch ( argument_type ) {
		case TYPE_NONE: {
			return ERROR_NONE;
		}

		case TYPE_BOOLEAN: {
			lookup_error = _libconfig_setting_lookup_bool( bind_setting, path, (bool *) &parsed_argument->ui );
			break;
		}

		case TYPE_INT: {
			lookup_error = _libconfig_setting_lookup_int( bind_setting, path, range_min, range_max, &parsed_argument->i );
			break;
		}

		case TYPE_UINT: {
			lookup_error = _libconfig_setting_lookup_uint( bind_setting, path, range_min, range_max, &parsed_argument->ui );
			break;
		}

		case TYPE_FLOAT: {
			lookup_error = _libconfig_setting_lookup_float( bind_setting, path, range_min, range_max, &parsed_argument->f );
			break;
		}

		case TYPE_STRING: {
			lookup_error = _libconfig_setting_lookup_string( bind_setting, path, (const char **) &parsed_argument->v );
			break;
		}

		default: {
			LOG_ERROR( "Unknown argument type during bind parsing: %d. Please reprogram to a valid type\n", argument_type );
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
 * @param bind_setting TODO
 * @param bind_index TODO
 * @param parsed_modifier TODO
 * @param parsed_function TODO
 * @param parsed_argument TODO
 *
 * @return TODO
 */
static Errors_t _parse_bind_core( Libconfig_Setting_t *bind_setting, const unsigned int bind_index, unsigned int *parsed_modifier, void ( **parsed_function )( const Arg * ), Arg *parsed_argument ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( bind_setting, returned_errors, "Pointer to configuration setting is NULL, unable to parse bind core\n" );
	RETURN_ERRORS_IF_NULL( parsed_modifier, returned_errors, "Pointer to where to store the parsed modifier is NULL, unable to parse bind core\n" );
	RETURN_ERRORS_IF_NULL( parsed_function, returned_errors, "Pointer to where to store the parsed function is NULL, unable to parse bind core\n" );
	RETURN_ERRORS_IF_NULL( parsed_argument, returned_errors, "Pointer to where to store the parsed argument is NULL, unable to parse bind core\n" );

	const char *setting_name = NULL;
	const Error_t setting_name_error = _libconfig_get_setting_name( bind_setting, &setting_name );
	add_error( &returned_errors, setting_name_error );

	if ( setting_name_error != ERROR_NONE ) {
		LOG_WARN( "Unable to acquire setting name, logs in this function may not print correctly: %s\n", ERROR_ENUM_STRINGS[ setting_name_error ] );
	}

	// TODO: It would be nice to find a clean way to allow for no modifier at all in a bind, not just an empty string
	const Error_t modifier_error = _parse_bind_modifier( bind_setting, parsed_modifier );
	add_error( &returned_errors, modifier_error );

	if ( modifier_error != ERROR_NONE ) {
		LOG_ERROR( "\"%s\" index %d invalid, unable to parse the bind's modifier: %s\n", setting_name, bind_index + 1, ERROR_ENUM_STRINGS[ modifier_error ] );
		return returned_errors;
	}

	Data_Type_t argument_type = TYPE_NONE;
	long double range_min = 0, range_max = 0;
	const Error_t bind_error = _parse_bind_function( bind_setting, parsed_function, &argument_type, &range_min, &range_max );
	add_error( &returned_errors, bind_error );

	if ( bind_error != ERROR_NONE ) {
		LOG_ERROR( "\"%s\" index %d invalid, unable to parse the bind's function: %s\n", setting_name, bind_index + 1, ERROR_ENUM_STRINGS[ bind_error ] );
		return returned_errors;
	}

	const Error_t argument_error = _parse_bind_argument( bind_setting, argument_type, range_min, range_max, parsed_argument );
	add_error( &returned_errors, argument_error );

	if ( argument_error != ERROR_NONE ) {
		LOG_ERROR( "\"%s\" index %d invalid, unable to parse the bind's arguments: %s\n", setting_name, bind_index + 1, ERROR_ENUM_STRINGS[ argument_error ] );
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
 * @param[out] parsed_argument_type Pointer to where to store the data type of the @ref Arg that can be passed to @p parsed_function.
 * @param[out] parsed_range_min Pointer to where to store the minimum value of the @ref Arg that can be passed to @p parsed_function.
 * @param[out] parsed_range_max Pointer to where to store the maximum value of the @ref Arg that can be passed to @p parsed_function.
 *
 * @return TODO
 */

static Error_t _parse_bind_function( Libconfig_Setting_t *bind_setting, void ( **parsed_function )( const Arg * ), Data_Type_t *parsed_argument_type, long double *parsed_range_min,
				     long double *parsed_range_max ) {

	RETURN_VALUE_IF_NULL( bind_setting, ERROR_NULL_VALUE, "Pointer to configuration setting is NULL, unable to parse the bind's function\n" );
	RETURN_VALUE_IF_NULL( parsed_function, ERROR_NULL_VALUE, "Pointer to where to store the parsed function pointer is NULL, unable to parse the bind's function\n" );
	RETURN_VALUE_IF_NULL( parsed_argument_type, ERROR_NULL_VALUE, "Pointer to where to store the parsed function's argument type is NULL, unable to parse the bind's function\n" );
	RETURN_VALUE_IF_NULL( parsed_range_min, ERROR_NULL_VALUE, "Pointer to where to store the parsed function's minimum range is NULL, unable to parse the bind's function\n" );
	RETURN_VALUE_IF_NULL( parsed_range_max, ERROR_NULL_VALUE, "Pointer to where to store the parsed function's maximum range is NULL, unable to parse the bind's function\n" );

	const char *function_string = NULL;
	const Error_t lookup_error = _libconfig_setting_lookup_string( bind_setting, "function", &function_string );

	if ( lookup_error != ERROR_NONE ) return lookup_error;

	for ( int i = 0; i < LENGTH( FUNCTION_ALIAS_MAP ); i++ ) {
		if ( strcasecmp( function_string, FUNCTION_ALIAS_MAP[ i ].name ) == 0 ) {
			*parsed_function = FUNCTION_ALIAS_MAP[ i ].func;
			*parsed_argument_type = FUNCTION_ALIAS_MAP[ i ].arg_type;
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
 * @todo This function could use some logging TLC
 *
 * @see https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/master/include/X11/X.h
 */
static Error_t _parse_bind_modifier( Libconfig_Setting_t *bind_setting, unsigned int *parsed_modifier ) {

	RETURN_VALUE_IF_NULL( bind_setting, ERROR_NULL_VALUE, "Pointer to configuration setting is NULL, unable to parse the bind's modifier\n" );
	RETURN_VALUE_IF_NULL( parsed_modifier, ERROR_NULL_VALUE, "Pointer to where to store the parsed modifier is NULL, unable to parse the bind's modifier\n" );

	const char *modifier_string = NULL;
	const Error_t lookup_error = _libconfig_setting_lookup_string( bind_setting, "modifier", &modifier_string );

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
		for ( int i = 0; i < LENGTH( MODIFIER_ALIAS_MAP ); i++ ) {
			if ( strcasecmp( modifier_token, MODIFIER_ALIAS_MAP[ i ].name ) == 0 ) {
				*parsed_modifier |= MODIFIER_ALIAS_MAP[ i ].mask;
				found = true;
				break;
			}
		}

		if ( !found ) {
			LOG_ERROR( "Invalid modifier: \"%s\"\n", modifier_token );
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

	RETURN_ERRORS_IF_NULL( buttonbind_setting, returned_errors, "Pointer to configuration setting is NULL, unable to parse buttonbind (index %u)\n", buttonbind_index );
	RETURN_ERRORS_IF_NULL( parsed_buttonbind, returned_errors, "Pointer to where to store the parsed buttonbind is NULL, unable to parse buttonbind (index %u)\n", buttonbind_index );

	merge_errors( &returned_errors, _parse_bind_core( buttonbind_setting, buttonbind_index, &parsed_buttonbind->mask, &parsed_buttonbind->func, &parsed_buttonbind->arg ) );

	const Error_t button_error = _parse_buttonbind_button( buttonbind_setting, &parsed_buttonbind->button );
	add_error( &returned_errors, button_error );

	if ( button_error != ERROR_NONE ) {
		LOG_ERROR( "Buttonbind %d invalid, unable to parse the bind's button: %s\n", buttonbind_index + 1, ERROR_ENUM_STRINGS[ button_error ] );
		return returned_errors;
	}

	const Error_t click_error = _parse_buttonbind_click( buttonbind_setting, &parsed_buttonbind->click );
	add_error( &returned_errors, click_error );

	if ( click_error != ERROR_NONE ) {
		LOG_ERROR( "Buttonbind %d invalid, unable to parse the bind's click: %s\n", buttonbind_index + 1, ERROR_ENUM_STRINGS[ click_error ] );
	}

	// Ensure button is zeroed and doesn't accidentally have any
	// data left over from parsing that could cause unusual behavior
	if ( errors_failure_count( &returned_errors ) != 0 ) {
		parsed_buttonbind->click = 0;
		parsed_buttonbind->mask = 0;
		parsed_buttonbind->button = 0;
		parsed_buttonbind->func = NULL;
		parsed_buttonbind->arg.i = 0;
	}

	return returned_errors;
}

/**
 * @brief Thin adapter function to allow for buttonbind parsing inside a generic array parsing function.
 *
 * See @ref _parse_buttonbind() for function documentation.
 *
 * @see @ref _parse_buttonbind()
 */
static Errors_t _parse_buttonbind_adapter( Libconfig_Setting_t *buttonbind_setting, const unsigned int buttonbind_index, void *parsed_buttonbind ) {
	return _parse_buttonbind( buttonbind_setting, buttonbind_index, (Button *) parsed_buttonbind );
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] buttonbind_setting
 * @param[out] parsed_button TODO
 *
 * @return TODO
 *
 * @see https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/master/include/X11/X.h
 */
static Error_t _parse_buttonbind_button( Libconfig_Setting_t *buttonbind_setting, unsigned int *parsed_button ) {

	RETURN_VALUE_IF_NULL( buttonbind_setting, ERROR_NULL_VALUE, "Pointer to configuration setting is NULL, unable to parse buttonbind button\n" );
	RETURN_VALUE_IF_NULL( parsed_button, ERROR_NULL_VALUE, "Pointer to where to store the parsed button is NULL, unable to parse buttonbind button\n" );

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

	RETURN_VALUE_IF_NULL( buttonbind_setting, ERROR_NULL_VALUE, "Pointer to configuration setting is NULL, unable to parse buttonbind click\n" );
	RETURN_VALUE_IF_NULL( parsed_click, ERROR_NULL_VALUE, "Pointer to where to store the parsed click is NULL, unable to parse buttonbind click\n" );

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
 * @param[in] config Pointer to the libconfig configuration containing the buttonbinds to
 * be parsed into @p buttonbind_config.
 * @param[out] buttonbind_config Pointer to where to dynamically allocate and store all the parsed buttonbinds.
 * @param[out] count Pointer to where to store the number of buttonbinds to be parsed.
 * This value does not take into account any failures during parsing the buttonbinds.
 * @param[out] malloced Boolean to track if @p buttonbind_config has been dynamically
 * allocated yet or not, analogous to if the default buttonbinds are being used or not. Value is set
 * to `false` after allocation.
 *
 * @return TODO
 *
 * @note TODO Maybe mention dynamic allocation in _parse_binds_config()?
 */
static Errors_t _parse_buttonbinds_config( const Libconfig_Config_t *config, Button **buttonbind_config, unsigned int *count, bool *malloced ) {
	return _parse_config_array( config, NULL, "buttonbinds", sizeof( Button ), _parse_buttonbind_adapter, malloced, (void **) buttonbind_config, count );
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param config TODO
 * @param setting TODO
 * @param config_array_name TODO
 * @param element_size TODO
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
 *
 * @todo I don't like the config and setting logic. It may be worth revisiting and rewriting.
 */
static Errors_t _parse_config_array( const Libconfig_Config_t *config, Libconfig_Setting_t *setting, const char *config_array_name, const size_t element_size,
				     const Array_Element_Parser_Function_t array_element_parser_function, bool *dynamically_allocated, void **parsed_config, unsigned int *parsed_config_length ) {

	Errors_t returned_errors = { 0 };

	// dynamically_allocated and parsed_config are not null checked intentionally.
	// They can be left to null to control behavior in this function.
	RETURN_ERRORS_IF_NULL( config_array_name, returned_errors, "Array name to search was NULL, unable to parse array\n" );
	RETURN_ERRORS_IF_NULL( parsed_config_length, returned_errors, "Pointer to where to store parsed array length was NULL, unable to parse array\n" );

	if ( ( config == NULL ) == ( setting == NULL ) ) {
		LOG_ERROR( "Exactly one config or setting must be provided, unable to parse array\n" );
		add_error( &returned_errors, ERROR_NULL_VALUE );
		return returned_errors;
	}

	const Libconfig_Setting_t *parent_setting = NULL;

	if ( config != NULL ) {
		parent_setting = config_lookup( config, config_array_name );
	} else {
		parent_setting = config_setting_lookup( setting, config_array_name );
	}

	if ( parent_setting == NULL ) {
		LOG_ERROR( "Problem reading config value \"%s\": Not found. Default values will be used instead\n", config_array_name );
		add_error( &returned_errors, ERROR_NOT_FOUND );
		return returned_errors;
	}

	*parsed_config_length = config_setting_length( parent_setting );
	if ( *parsed_config_length == 0 ) {
		LOG_WARN( "No %s listed. Default values will be used\n", config_array_name );
		add_error( &returned_errors, ERROR_NOT_FOUND );
		return returned_errors;
	}

	LOG_DEBUG( "%u elements detected in \"%s\"\n", *parsed_config_length, config_array_name );

	// dynamically_allocated is also used to determine if we
	// should dynamically allocate the array or if it is already
	// allocated some other way, usually on the stack, as well
	// as report that it has been dynamically allocated here.
	if ( dynamically_allocated != NULL && parsed_config != NULL ) {
		LOG_DEBUG( "Dynamically allocating %lu bytes of memory for \"%s\"\n", *parsed_config_length * element_size, config_array_name );

		errno = 0;
		void *calloced_memory = calloc( *parsed_config_length, element_size );

		if ( calloced_memory == NULL ) {
			LOG_ERROR( "Failed to allocate %lu bytes for \"%s\": %s\n", *parsed_config_length * element_size, config_array_name, strerror(errno) );
			add_error( &returned_errors, ERROR_ALLOCATION );
			return returned_errors;
		}

		*parsed_config = calloced_memory;
		*dynamically_allocated = true;
	}

	for ( unsigned int i = 0; i < *parsed_config_length; i++ ) {

		Libconfig_Setting_t *child_setting = config_setting_get_elem( parent_setting, i );

		RETURN_ERRORS_IF_NULL( child_setting, returned_errors, "element index %u in \"%s\" returned NULL\n", i, config_array_name );

		// Yes, parsed_config can be NULL, that is intended. It is used by things like
		// theme or tag parsing that rely on global values.
		void *element = parsed_config ? (char *) ( *parsed_config ) + ( i * element_size ) : NULL;

		const Errors_t parsing_error = array_element_parser_function( child_setting, i, element );

		if ( errors_failure_count( &parsing_error ) ) {
			LOG_WARN( "\"%s\" element number %d failed to be parsed. It had %d errors\n", config_array_name, i + 1, errors_failure_count( &parsing_error ) );
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

	RETURN_ERRORS_IF_NULL( fonts_setting, returned_errors, "Pointer to configuration setting is NULL, unable to parse theme font index %u\n", fonts_index );

	if ( config_setting_type( fonts_setting ) != CONFIG_TYPE_STRING ) {
		LOG_ERROR( "Font element at index %u is not a string\n", fonts_index );
		add_error( &returned_errors, ERROR_TYPE );
		return returned_errors;
	}

	parsed_font = config_setting_get_string( fonts_setting );

	if ( parsed_font == NULL ) {
		LOG_ERROR( "Failed to parse theme font at index %u for unknown an reason\n", fonts_index );
		add_error( &returned_errors, ERROR_NULL_VALUE );
		return returned_errors;
	}

	return returned_errors;
}

/**
 * @brief Thin adapter function to allow for font parsing inside a generic array parsing function.
 *
 * See @ref _parse_font() for function documentation.
 *
 * @see @ref _parse_font()
 */
static Errors_t _parse_font_adapter( Libconfig_Setting_t *fonts_setting, const unsigned int fonts_index, void *parsed_font ) {
	return _parse_font( fonts_setting, fonts_index, (const char *) parsed_font );
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
static Errors_t _parse_generic_settings( const Libconfig_Config_t *config ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( config, returned_errors, "Pointer to configuration is NULL, unable to parse generic settings\n" );

	for ( int i = 0; i < LENGTH( SETTING_ALIAS_MAP ); ++i ) {
		Error_t returned_error = ERROR_NONE;

		switch ( SETTING_ALIAS_MAP[ i ].type ) {
			case TYPE_BOOLEAN:
				returned_error = _libconfig_lookup_bool( config, SETTING_ALIAS_MAP[ i ].name, SETTING_ALIAS_MAP[ i ].value );
				break;

			case TYPE_INT:
				returned_error = _libconfig_lookup_int( config, SETTING_ALIAS_MAP[ i ].name, (int) SETTING_ALIAS_MAP[ i ].range_min, (int) SETTING_ALIAS_MAP[ i ].range_max,
									SETTING_ALIAS_MAP[ i ].value );
				break;

			case TYPE_UINT:
				returned_error = _libconfig_lookup_uint( config, SETTING_ALIAS_MAP[ i ].name, (unsigned int) SETTING_ALIAS_MAP[ i ].range_min,
									 (unsigned int) SETTING_ALIAS_MAP[ i ].range_max, SETTING_ALIAS_MAP[ i ].value );
				break;

			case TYPE_FLOAT:
				returned_error = _libconfig_lookup_float( config, SETTING_ALIAS_MAP[ i ].name, (float) SETTING_ALIAS_MAP[ i ].range_min, (float) SETTING_ALIAS_MAP[ i ].range_max,
									  SETTING_ALIAS_MAP[ i ].value );
				break;

			case TYPE_STRING:
				returned_error = _libconfig_lookup_string( config, SETTING_ALIAS_MAP[ i ].name, SETTING_ALIAS_MAP[ i ].value );
				break;

			default:
				returned_error = ERROR_TYPE;
				LOG_ERROR( "Setting \"%s\" is programmed with an invalid type: \"%s\"\n", SETTING_ALIAS_MAP[ i ].name, DATA_TYPE_ENUM_STRINGS[ SETTING_ALIAS_MAP[ i ].type ] );
				break;
		}

		if ( returned_error != ERROR_NONE ) {
			if ( returned_error == ERROR_NOT_FOUND && SETTING_ALIAS_MAP[ i ].optional ) {
				LOG_DEBUG( "\"%s\" was not found but is flagged as optional, continuing", SETTING_ALIAS_MAP[ i ].name );
			} else {
				add_error( &returned_errors, returned_error );
				LOG_ERROR( "Issue while parsing \"%s\": %s\n", SETTING_ALIAS_MAP[ i ].name, ERROR_ENUM_STRINGS[ returned_error ] );
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
 * @param[in] keybind_setting TODO
 * @param[in] keybind_index TODO
 * @param[out] parsed_keybind TODO
 *
 * @return TODO
 */
static Errors_t _parse_keybind( Libconfig_Setting_t *keybind_setting, const unsigned int keybind_index, Key *parsed_keybind ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( keybind_setting, returned_errors, "Pointer to configuration setting is NULL, unable to parse keybind\n" );
	RETURN_ERRORS_IF_NULL( parsed_keybind, returned_errors, "Pointer to where to store parsed keybind is NULL, unable to parse keybind\n" );

	const Errors_t bind_core_errors = _parse_bind_core( keybind_setting, keybind_index, &parsed_keybind->mod, &parsed_keybind->func, &parsed_keybind->arg );
	merge_errors( &returned_errors, bind_core_errors );

	const Error_t keysym_error = _parse_keybind_keysym( keybind_setting, &parsed_keybind->keysym );
	add_error( &returned_errors, keysym_error );

	if ( keysym_error != ERROR_NONE ) {
		LOG_ERROR( "Keybind %d invalid, unable to parse the bind's key: %s\n", keybind_index + 1, ERROR_ENUM_STRINGS[ keysym_error ] );
	}

	// Ensure key is zeroed and doesn't accidentally have any
	// data left over from parsing that could cause unusual behavior
	if ( errors_failure_count( &returned_errors ) != 0 ) {
		parsed_keybind->mod = 0;
		parsed_keybind->keysym = NoSymbol;
		parsed_keybind->func = NULL;
		parsed_keybind->arg.i = 0;
	}

	return returned_errors;
}

/**
 * @brief Thin adapter function to allow for keybind parsing inside a generic array parsing function.
 *
 * See @ref _parse_keybind() for function documentation.
 *
 * @see @ref _parse_keybind()
 */
static Errors_t _parse_keybind_adapter( Libconfig_Setting_t *keybind_setting, const unsigned int keybind_index, void *parsed_keybind ) {
	return _parse_keybind( keybind_setting, keybind_index, (Key *) parsed_keybind );
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param[in] keybind_setting TODO
 * @param[out] parsed_keysym Pointer to where to store the keysym value parsed from @p keysym_string.
 *
 * @return TODO
 *
 * @note `xev` is likely your best bet at finding the keysym values that will work with XStringToKeysym().
 * If someone knows a better way, please reach out and let me know.
 *
 * @see https://gitlab.freedesktop.org/xorg/app/xev
 * @see https://gitlab.freedesktop.org/xorg/lib/libx11/-/blob/master/src/StrKeysym.c?ref_type=heads#L74
 */
static Error_t _parse_keybind_keysym( Libconfig_Setting_t *keybind_setting, KeySym *parsed_keysym ) {

	RETURN_VALUE_IF_NULL( keybind_setting, ERROR_NULL_VALUE, "Pointer to configuration setting is NULL, unable to parse keysym\n" );
	RETURN_VALUE_IF_NULL( parsed_keysym, ERROR_NULL_VALUE, "Pointer to where to store parsed keysym is NULL, unable to parse keysym\n" );

	const char *keybind_string = NULL;
	const Error_t lookup_error = _libconfig_setting_lookup_string( keybind_setting, "key", &keybind_string );

	if ( lookup_error != ERROR_NONE ) return lookup_error;

	*parsed_keysym = XStringToKeysym( keybind_string );

	if ( *parsed_keysym == NoSymbol ) {
		LOG_ERROR( "Failed to convert string (\"%s\") to keysym\n", keybind_string );
		return ERROR_NOT_FOUND;
	}

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
 * @param[in] config Pointer to the libconfig configuration containing the keybinds to
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
static Errors_t _parse_keybinds_config( const Libconfig_Config_t *config, Key **keybind_config, unsigned int *keybinds_count, bool *keybinds_dynamically_allocated ) {
	return _parse_config_array( config, NULL, "keybinds", sizeof( Key ), _parse_keybind_adapter, keybinds_dynamically_allocated, (void **) keybind_config, keybinds_count );
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
 * loaded during @ref _parser_load_default_config().
 *
 * @param config TODO
 * @param custom_config_filepath
 * @param fallback_config_loaded TODO
 *
 * @return TODO
 *
 * @todo These error returns may not be the most accurate, not sure exactly the best fits.
 * @todo Should the parser even look for another config file if one is passed from the CLI? Could be deceptive behavior.
 */

static Errors_t _parser_open_config_file( Libconfig_Config_t *config, char **custom_config_filepath, bool *fallback_config_loaded ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( config, returned_errors, "Pointer to configuration is NULL, unable to open config file\n" );
	RETURN_ERRORS_IF_NULL( custom_config_filepath, returned_errors, "Pointer to where to store parsed config file's path is NULL, unable open config file\n" );
	RETURN_ERRORS_IF_NULL( fallback_config_loaded, returned_errors, "Pointer to where to store fallback config boolean is NULL, unable open config file\n" );

	char *xdg_config_home = get_xdg_config_home();
	char *xdg_data_home = get_xdg_data_home();

	// @formatter:off
	const struct {
		const char *filepath_1;
		const char *filepath_2;
		const bool is_fallback_config;
	} config_filepaths[ ] = {
		{ *custom_config_filepath, "", false },
		{ xdg_config_home, "/dwm.conf", false },
		{ xdg_config_home, "/dwm/dwm.conf", false },
		{ xdg_data_home, "/dwm/dwm_last.conf", true },
		{ "/etc/dwm.conf", "", true }
	};
	// @formatter:on

	for ( int i = 0; i < LENGTH( config_filepaths ); i++ ) {
		char constructed_path[ PATH_MAX ];

		if ( config_filepaths[ i ].filepath_1 != NULL && config_filepaths[ i ].filepath_2 != NULL ) {
			snprintf( constructed_path, sizeof( constructed_path ), "%s%s", config_filepaths[ i ].filepath_1, config_filepaths[ i ].filepath_2 );
		} else {
			LOG_WARN( "Part of a path was NULL while trying top open a configuration file, skipping invalid path" );
			continue;
		}

		LOG_DEBUG( "Attempting to open config file \"%s\"\n", constructed_path );

		FILE *configuration_file = fopen( constructed_path, "r" );

		if ( configuration_file == NULL ) {
			LOG_WARN( "Unable to open config file \"%s\"\n", constructed_path );
			add_error( &returned_errors, ERROR_NOT_FOUND );
			continue;
		}

		if ( config_read( config, configuration_file ) == CONFIG_FALSE ) {
			LOG_WARN( "Problem parsing config file \"%s\", line %d: %s\n", constructed_path, config_error_line( config ), config_error_text( config ) );
			add_error( &returned_errors, ERROR_NULL_VALUE );
			fclose( configuration_file );
			continue;
		}

		*custom_config_filepath = estrdup( constructed_path );
		if ( *custom_config_filepath == NULL ) add_error( &returned_errors, ERROR_ALLOCATION );

		*fallback_config_loaded = config_filepaths->is_fallback_config;

		fclose( configuration_file );

		break;
	}

	free( xdg_config_home );
	free( xdg_data_home );

	return returned_errors;
}

/**
 * @brief TODO
 *
 * TODO
 *
 * @param config TODO
 * @param parsed_config_filepath TODO
 *
 * @return TODO
 */
static Error_t _parser_resolve_include_directory( Libconfig_Config_t *config, const char *parsed_config_filepath ) {

	RETURN_VALUE_IF_NULL( config, ERROR_NULL_VALUE, "Pointer to configuration is NULL, unable to resolve config include directory\n" );
	RETURN_VALUE_IF_NULL( parsed_config_filepath, ERROR_NULL_VALUE, "Config filepath string is NULL, unable to resolve config include directory\n" );

	char *config_include_directory = realpath( parsed_config_filepath, NULL );

	RETURN_VALUE_IF_NULL( config_include_directory, ERROR_ALLOCATION, "Failed to allocate memory for the configuration file's include path\n" );

	config_include_directory = dirname( config_include_directory );

	if ( config_include_directory[ 0 ] == '.' ) {
		LOG_WARN( "Unable to resolve configuration file's include directory\n" );
		free( config_include_directory );
		return ERROR_NOT_FOUND;
	}

	config_set_include_dir( config, config_include_directory );

	free( config_include_directory );

	return ERROR_NONE;
}

/**
 * @brief Parse a rule from a libconfig configuration setting.
 *
 * TODO
 *
 * @param[in] rule_setting Pointer to the libconfig setting containing the rule to be parsed into @p parsed_rule.
 * @param[in] rule_index Index of the current rule in the larger array. Used purely for debug printing.
 * @param[out] parsed_rule Pointer to the @ref Rule struct where the values parsed from @p rule_setting are stored.
 *
 * @return TODO
 */
static Errors_t _parse_rule( Libconfig_Setting_t *rule_setting, const unsigned int rule_index, Rule *parsed_rule ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( rule_setting, returned_errors, "Pointer to configuration setting is NULL, unable to parse rule index %u\n", rule_index );
	RETURN_ERRORS_IF_NULL( parsed_rule, returned_errors, "Pointer to where to store the parsed rule is NULL, unable to parse rule index %u\n", rule_index );

	add_error( &returned_errors, _libconfig_setting_lookup_string( rule_setting, "class", &parsed_rule->class ) );
	add_error( &returned_errors, _libconfig_setting_lookup_string( rule_setting, "instance", &parsed_rule->instance ) );
	add_error( &returned_errors, _libconfig_setting_lookup_string( rule_setting, "title", &parsed_rule->title ) );
	add_error( &returned_errors, _libconfig_setting_lookup_uint( rule_setting, "tag-mask", 0, TAGMASK, &parsed_rule->tags ) );
	add_error( &returned_errors, _libconfig_setting_lookup_int( rule_setting, "monitor", -1, 99, &parsed_rule->monitor ) );

	// Note: This logically should be a boolean value, but I didn't want to
	// deviate from the coded type, so I kept it an int and range check it.
	add_error( &returned_errors, _libconfig_setting_lookup_int( rule_setting, "floating", 0, 1, &parsed_rule->isfloating ) );

	// Ensure rule is zeroed and doesn't accidentally have any
	// data left over from parsing that could cause unusual behavior
	if ( errors_failure_count( &returned_errors ) != 0 ) {
		parsed_rule->class = NULL;
		parsed_rule->instance = NULL;
		parsed_rule->title = NULL;
		parsed_rule->tags = 0;
		parsed_rule->isfloating = 0;
		parsed_rule->monitor = 0;
	}

	LOG_DEBUG( "Rule %d: class: \"%s\", instance: \"%s\", title: \"%s\", tag-mask: %d, monitor: %d, floating: %d\n", rule_index, parsed_rule->class, parsed_rule->instance, parsed_rule->title,
		   parsed_rule->tags, parsed_rule->monitor, parsed_rule->isfloating );

	return returned_errors;
}

/**
 * @brief Thin adapter function to allow for rule parsing inside a generic array parsing function.
 *
 * See @ref _parse_rule() for function documentation.
 *
 * @see @ref _parse_rule()
 */
static Errors_t _parse_rule_adapter( Libconfig_Setting_t *rule_setting, const unsigned int rule_index, void *parsed_rule ) {
	return _parse_rule( rule_setting, rule_index, (Rule *) parsed_rule );
}

/**
 * @brief Parse a list of rules from a libconfig configuration into a list of @ref Rule structs.
 *
 * TODO
 *
 * @param[in] config Pointer to the libconfig configuration containing the rules to be parsed into @p rules_config.
 * @param[out] rules_config Pointer to where to dynamically allocate memory and store all the parsed rules.
 * @param[out] rules_count Pointer to where to store the number of rules to be parsed. This value does not take into account
 * any failures during parsing the rules.
 * @param[out] rules_dynamically_allocated Boolean to track if @p rules_config has been dynamically allocated yet or not, analogous
 * to if the default rules are being used or not. Value is set to `false` after allocation.
 *
 * @return TODO
 */
static Errors_t _parse_rules_config( const Libconfig_Config_t *config, Rule **rules_config, unsigned int *rules_count, bool *rules_dynamically_allocated ) {
	return _parse_config_array( config, NULL, "rules", sizeof( Rule ), _parse_rule_adapter, rules_dynamically_allocated, (void **) rules_config, rules_count );
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

	RETURN_ERRORS_IF_NULL( tags_setting, returned_errors, "Pointer to configuration setting is NULL, unable to parse tag index %u\n", tags_index );

	if ( tags_index >= LENGTH( tags ) ) {
		LOG_ERROR( "Parsed tag's index exceeds limits of the tags array\n" );
		add_error( &returned_errors, ERROR_RANGE );
		return returned_errors;
	}

	const char *original_tag_name = tags[ tags_index ];

	if ( config_setting_type( tags_setting ) != CONFIG_TYPE_STRING ) {
		LOG_ERROR( "Tag element at index %u is not a string\n", tags_index );
		add_error( &returned_errors, ERROR_TYPE );
		return returned_errors;
	}

	tags[ tags_index ] = config_setting_get_string( tags_setting );

	if ( tags[ tags_index ] == NULL ) {
		tags[ tags_index ] = original_tag_name;
		LOG_ERROR( "Failed to parse tag at index %u for unknown an reason\n", tags_index );
		add_error( &returned_errors, ERROR_NULL_VALUE );
		return returned_errors;
	}

	return returned_errors;
}

/**
 * @brief Thin adapter function to allow for tag parsing inside a generic array parsing function.
 *
 * See @ref _parse_tag() for function documentation.
 *
 * @see @ref _parse_tag()
 */
static Errors_t _parse_tags_adapter( Libconfig_Setting_t *tags_setting, const unsigned int tags_index, void *unused ) {
	return _parse_tag( tags_setting, tags_index );
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
static Errors_t _parse_tags_config( const Libconfig_Config_t *config ) {

	unsigned int tags_count = 0;
	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( config, returned_errors, "Pointer to configuration is NULL, unable to parse tags\n" );

	merge_errors( &returned_errors, _parse_config_array( config, NULL, "tags", 0, _parse_tags_adapter, NULL, NULL, &tags_count ) );

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
 * @param[in] theme_setting Pointer to the libconfig setting containing the theme to be parsed.
 * @param[in] theme_index Index of the current theme being parsed.
 *
 * @return TODO
 */
static Errors_t _parse_theme( Libconfig_Setting_t *theme_setting, const unsigned int theme_index ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( theme_setting, returned_errors, "Pointer to configuration setting is NULL, unable to parse theme at index %u\n", theme_index );

	// dwm does not support more than 1 theme
	if ( theme_index > 0 ) {
		LOG_WARN( "%d themes detected. dwm can only supports using 1 theme\n", theme_index + 1 );
		add_error( &returned_errors, ERROR_RANGE );
		return returned_errors;
	}

	// I would rather pass the font variables by reference in a theme data structure, but that is outside the scope of this patch.
	const Errors_t font_errors = _parse_config_array( NULL, theme_setting, "fonts", sizeof( char * ), _parse_font_adapter, &fonts_malloced, (void **) fonts, &fonts_count );
	merge_errors( &returned_errors, font_errors );

	for ( int i = 0; i < LENGTH( THEME_ALIAS_MAP ); i++ ) {
		const Error_t error = _libconfig_setting_lookup_string( theme_setting, THEME_ALIAS_MAP[ i ].path, THEME_ALIAS_MAP[ i ].value );
		add_error( &returned_errors, error );
		if ( error != ERROR_NONE ) {
			LOG_ERROR( "Failed to parse theme %d's element \"%s\": %s\n", theme_index, THEME_ALIAS_MAP[ i ].path, ERROR_ENUM_STRINGS[ error ] );
		}
	}

	return returned_errors;
}

/**
 * @brief Thin adapter function to allow for theme parsing inside a generic array parsing function.
 *
 * See @ref _parse_theme() for function documentation.
 *
 * @see @ref _parse_theme()
 */
static Errors_t _parse_theme_adapter( Libconfig_Setting_t *theme_setting, const unsigned int theme_index, void *unused ) {
	return _parse_theme( theme_setting, theme_index );
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
static Errors_t _parse_theme_config( const Libconfig_Config_t *config ) {

	Errors_t returned_errors = { 0 };

	RETURN_ERRORS_IF_NULL( config, returned_errors, "Pointer to configuration is NULL, unable to parse theme config\n" );

	unsigned int unused = 0; // Unused, but the pointer must exist to satisfy config array parsing
	merge_errors( &returned_errors, _parse_config_array( config, NULL, "themes", 0, _parse_theme_adapter, NULL, NULL, &unused ) );

	return returned_errors;
}

/// Parser internal utility functions ///

/**
 * @brief TODO
 *
 * TODO
 *
 * @param setting TODO
 * @param found_name TODO
 *
 * @return TODO
 */
static Error_t _libconfig_get_setting_name( const Libconfig_Setting_t *setting, const char **found_name ) {

	RETURN_VALUE_IF_NULL( setting, ERROR_NULL_VALUE, "Pointer to configuration setting is NULL, unable to get setting name\n" );
	RETURN_VALUE_IF_NULL( found_name, ERROR_NULL_VALUE, "Pointer to where to store the found setting name string is NULL, unable to get setting name\n" );

	while ( setting != NULL ) {
		const char *tmp_string = config_setting_name( setting );

		if ( tmp_string != NULL ) {
			*found_name = tmp_string;
			return ERROR_NONE;
		}

		setting = config_setting_parent( setting );
	}

	LOG_ERROR( "Could not find name of setting (%p)\n", (void *) setting );

	return ERROR_NOT_FOUND;
}

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

	if ( config == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;

	const config_setting_t *setting = config_lookup( config, path );

	if ( setting == NULL ) return ERROR_NOT_FOUND;
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

	if ( config == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;

	const config_setting_t *setting = config_lookup( config, path );

	if ( setting == NULL ) return ERROR_NOT_FOUND;
	if ( config_setting_type( setting ) != CONFIG_TYPE_FLOAT ) return ERROR_TYPE;

	const float tmp = (float) config_setting_get_float( setting );

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

	if ( config == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;

	const config_setting_t *setting = config_lookup( config, path );

	if ( setting == NULL ) return ERROR_NOT_FOUND;
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

	if ( config == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;

	const config_setting_t *setting = config_lookup( config, path );

	if ( setting == NULL ) return ERROR_NOT_FOUND;
	if ( config_setting_type( setting ) != CONFIG_TYPE_STRING ) return ERROR_TYPE;

	*parsed_value = config_setting_get_string( setting );

	if ( strcasecmp( *parsed_value, "NULL" ) == 0 ) *parsed_value = NULL;

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

	if ( config == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;

	const config_setting_t *setting = config_lookup( config, path );

	if ( setting == NULL ) return ERROR_NOT_FOUND;
	if ( config_setting_type( setting ) != CONFIG_TYPE_INT ) return ERROR_TYPE;

	const int tmp_int = config_setting_get_int( setting );

	if ( tmp_int < 0 ) return ERROR_RANGE;

	const unsigned int tmp_uint = (unsigned int) tmp_int;

	if ( tmp_uint < range_min || tmp_uint > range_max ) return ERROR_RANGE;

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

	if ( setting == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;

	const config_setting_t *child_setting = config_setting_lookup( setting, path );

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

	if ( setting == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;

	const config_setting_t *child_setting = config_setting_lookup( setting, path );

	if ( !child_setting ) return ERROR_NOT_FOUND;
	if ( config_setting_type( child_setting ) != CONFIG_TYPE_FLOAT ) return ERROR_TYPE;

	const float tmp = (float) config_setting_get_float( child_setting );

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

	if ( setting == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;

	const config_setting_t *child_setting = config_setting_lookup( setting, path );

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

	if ( setting == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;

	const config_setting_t *child_setting = config_setting_lookup( setting, path );

	if ( !child_setting ) return ERROR_NOT_FOUND;
	if ( config_setting_type( child_setting ) != CONFIG_TYPE_STRING ) return ERROR_TYPE;

	*parsed_value = config_setting_get_string( child_setting );

	if ( strcasecmp( *parsed_value, "NULL" ) == 0 ) *parsed_value = NULL;

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

	if ( setting == NULL || path == NULL || parsed_value == NULL ) return ERROR_NULL_VALUE;

	const config_setting_t *child_setting = config_setting_lookup( setting, path );

	if ( !child_setting ) return ERROR_NOT_FOUND;
	if ( config_setting_type( child_setting ) != CONFIG_TYPE_INT ) return ERROR_TYPE;

	const int tmp_int = config_setting_get_int( child_setting );

	if ( tmp_int < 0 ) return ERROR_RANGE;

	const unsigned int tmp_uint = (unsigned int) tmp_int;

	if ( tmp_uint < range_min || tmp_uint > range_max ) return ERROR_RANGE;

	*parsed_value = tmp_uint;

	return ERROR_NONE;
}
