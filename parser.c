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

typedef struct Configuration {

        // Parser internal values
        config_t *libconfig_config;
        char *config_filepath;
        unsigned int max_keys;
        bool default_binds_loaded;

        // dwm configuration values
        bool showbar, topbar, resizehints, lockfullscreen;
        unsigned int borderpx, snap, nmaster, refreshrate;
        float mfact;

        char *tags[ LENGTH( tags ) ];

        const char *font;
        const char *theme[ 2 ][ 3 ];

        unsigned int rules_count;
        Rule *rules;

        unsigned int keybinds_count;
        Key *keybinds;

        unsigned int buttonbinds_count;
        Button *buttonbinds;
} Configuration;

static Configuration dwm_config = { 0 };
static char *custom_config_path = NULL;

// Public functions
static void config_cleanup( Configuration *master_config );
static int parse_config( const char *custom_config_filepath, Configuration *master_config );

// Internal functions
static void backup_config( config_t *config );
static void load_default_buttonbind_config( Button **buttonbind_config, unsigned int *buttonbind_count );
static void load_default_keybind_config( Key **keybind_config, unsigned int *keybind_count );
static void load_default_master_config( Configuration *master_config );
static int open_config( config_t *config, char **config_filepath, Configuration *master_config );
static int parse_bind_argument( const char *argument_string, const enum Argument_Type *arg_type, Arg *arg, long double range_min, long double range_max );
static int parse_bind_function( const char *function_string, enum Argument_Type *arg_type, void ( **function )( const Arg * ), long double *range_min, long double *range_max );
static int parse_bind_modifier( const char *modifier_string, unsigned int *modifier );
static int parse_buttonbind( const char *buttonbind_string, Button *buttonbind, unsigned int max_keys );
static int parse_buttonbind_button( const char *button_string, unsigned int *button );
static int parse_buttonbind_click( const char *click_string, unsigned int *click );
static int parse_buttonbinds_config( const config_t *config, Button **buttonbind_config, unsigned int *buttonbind_count, unsigned int max_keys );
static int parse_generic_settings( const config_t *config, Configuration *master_config );
static int parse_keybind( const char *keybind_string, Key *keybind, unsigned int max_keys );
static int parse_keybind_keysym( const char *keysym_string, KeySym *keysym );
static int parse_keybinds_config( const config_t *config, Key **keybind_config, unsigned int *keybinds_count, unsigned int max_keys );
static int parse_rules_string( const char *input_string, char **output_string );
static int parse_rules_config( const config_t *config, Rule **rules_config, unsigned int *rules_count );
static int parse_tags_config( const config_t *config, Configuration *master_config );
static int parse_theme( const config_setting_t *theme, Configuration *master_config );
static int parse_theme_config( const config_t *config, Configuration *master_config );

// Wrapper functions for better compatability
static void parser_spawn( const Arg *arg );
static void setlayout_floating( const Arg *arg );
static void setlayout_monocle( const Arg *arg );
static void setlayout_tiled( const Arg *arg );

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

static void backup_config( config_t *config ) {

        // Save xdg data folder to buffer (~/.local/share)
        char *buffer = get_xdg_data_home();

        if ( buffer == NULL ) {
                log_error( "Unable to get necessary directory to backup config\n" );
        } else {

                // Append buffer (already has "~/.local/share" or other xdg data directory)
                // with the directory we want to backup the config to, create the directory
                // if it doesn't exist, and then append with the filename we want to backup
                // to config in.
                mstrextend( &buffer, "/dwm/" );
                make_parent_directory( buffer );
                mstrextend( &buffer, "dwm_last.conf" );

                if ( config_write_file( config, buffer ) == CONFIG_FALSE ) {
                        log_error( "Problem backing up current config to \"%s\"\n", buffer );
                } else {
                        log_info( "Current config backed up to \"%s\"\n", buffer );
                }

                SAFE_FREE( buffer );
        }
}

static void load_default_buttonbind_config( Button **buttonbind_config, unsigned int *buttonbind_count ) {
        *buttonbind_count = LENGTH( buttons );
        *buttonbind_config = (Button *) buttons;
}

// Default binds from dwm's `config.def.h`
static void load_default_keybind_config( Key **keybind_config, unsigned int *keybind_count ) {
        *keybind_count = LENGTH( keys );
        *keybind_config = (Key *) keys;
}

static void load_default_master_config( Configuration *master_config ) {

        if ( master_config == NULL ) {
                log_fatal( "master_config is NULL, can't load default configuration\n" );
                exit( EXIT_FAILURE );
        }

        // Unique values to the Configuration struct
        master_config->config_filepath = NULL;
        master_config->max_keys = 4;
        master_config->default_binds_loaded = false;

        master_config->rules_count = 0;
        master_config->rules = NULL;

        master_config->keybinds_count = 0;
        master_config->keybinds = NULL;

        master_config->buttonbinds_count = 0;
        master_config->buttonbinds = NULL;

        // Values from config.h
        master_config->showbar = showbar;
        master_config->topbar = topbar;
        master_config->resizehints = resizehints;
        master_config->lockfullscreen = lockfullscreen;

        master_config->borderpx = borderpx;
        master_config->snap = snap;
        master_config->nmaster = nmaster;
        master_config->mfact = mfact;
        master_config->refreshrate = refreshrate;

        master_config->font = strdup( fonts[ 0 ] );
        master_config->theme[ SchemeNorm ][ ColFg ] = strdup( colors[ SchemeNorm ][ ColFg ] );
        master_config->theme[ SchemeNorm ][ ColBg ] = strdup( colors[ SchemeNorm ][ ColBg ] );
        master_config->theme[ SchemeNorm ][ ColBorder ] = strdup( colors[ SchemeNorm ][ ColBorder ] );
        master_config->theme[ SchemeSel ][ ColFg ] = strdup( colors[ SchemeSel ][ ColFg ] );
        master_config->theme[ SchemeSel ][ ColBg ] = strdup( colors[ SchemeSel ][ ColBg ] );
        master_config->theme[ SchemeSel ][ ColBorder ] = strdup( colors[ SchemeSel ][ ColBorder ] );

        for ( int i = 0; i < LENGTH( master_config->tags ); i++ ) {
                master_config->tags[ i ] = strdup( tags[ i ] );
        }
}

static int open_config( config_t *config, char **config_filepath, Configuration *master_config ) {

        int i, config_filepaths_length = 0;
        char *config_filepaths[ 5 ];

        // Check if a custom user config was passed in and copy it if it was
        if ( *config_filepath != NULL ) {
                config_filepaths[ config_filepaths_length++ ] = strdup( *config_filepath );
                SAFE_FREE( *config_filepath );
        }

        // ~/.config/dwm.conf
        char *config_top_directory = get_xdg_config_home();
        mstrextend( &config_top_directory, "/dwm.conf" );
        config_filepaths[ config_filepaths_length++ ] = config_top_directory;

        // ~/.config/dwm/dwm.conf
        char *config_sub_directory = get_xdg_config_home();
        mstrextend( &config_sub_directory, "/dwm/dwm.conf" );
        config_filepaths[ config_filepaths_length++ ] = config_sub_directory;

        // ~/.local/share/dwm/dwm_last.conf
        char *config_backup = get_xdg_data_home();
        mstrextend( &config_backup, "/dwm/dwm_last.conf" );
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
        load_default_keybind_config( &master_config->keybinds, &master_config->keybinds_count );
        load_default_buttonbind_config( &master_config->buttonbinds, &master_config->buttonbinds_count );

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

        SAFE_FREE( master_config->font );

        for ( i = 0; i < LENGTH( master_config->tags ); i++ ) {
                SAFE_FREE( master_config->tags[ i ] );
        }

        for ( i = 0; i < master_config->rules_count; i++ ) {
                SAFE_FREE( master_config->rules[ i ].class );
                SAFE_FREE( master_config->rules[ i ].instance );
                SAFE_FREE( master_config->rules[ i ].title );
        }

        for ( i = 0; i < LENGTH( master_config->theme ); i++ ) {
                for ( int j = 0; j < LENGTH( master_config->theme[ i ] ); j++ ) {
                        SAFE_FREE( master_config->theme[ i ][ j ] );
                }
        }

        if ( !master_config->default_binds_loaded ) {
                for ( i = 0; i < master_config->keybinds_count; i++ ) {
                        if ( master_config->keybinds[ i ].argument_type == ARG_TYPE_POINTER ) {
                                Arg *tmp = (Arg *) &master_config->keybinds[ i ].arg;
                                SAFE_FREE( tmp->v );
                        }
                }
                SAFE_FREE( master_config->keybinds );

                for ( i = 0; i < master_config->buttonbinds_count; i++ ) {
                        if ( master_config->buttonbinds[ i ].argument_type == ARG_TYPE_POINTER ) {
                                Arg *tmp = (Arg *) &master_config->buttonbinds[ i ].arg;
                                SAFE_FREE( tmp->v );
                        }
                }
                SAFE_FREE( master_config->buttonbinds );
        }

        config_destroy( master_config->libconfig_config );
}

int parse_config( const char *custom_config_filepath, Configuration *master_config ) {

        static config_t libconfig_config;
        char *config_filepath = NULL;
        int total_errors = 0;

        // Initialize libconfig context
        config_init( &libconfig_config );
        master_config->libconfig_config = &libconfig_config;

        // Populate master dwm configuration with default values
        load_default_master_config( master_config );

        // Simple way of passing users custom config to open_config()
        // This strdup is freed at the start of open_config()
        if ( custom_config_filepath != NULL ) config_filepath = strdup( custom_config_filepath );

        if ( open_config( &libconfig_config, &config_filepath, master_config ) ) return -1;

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
        total_errors += parse_generic_settings( &libconfig_config, master_config );
        total_errors += parse_keybinds_config( &libconfig_config, &master_config->keybinds, &master_config->keybinds_count, master_config->max_keys );
        total_errors += parse_buttonbinds_config( &libconfig_config, &master_config->buttonbinds, &master_config->buttonbinds_count, master_config->max_keys );
        total_errors += parse_rules_config( &libconfig_config, &master_config->rules, &master_config->rules_count );
        total_errors += parse_tags_config( &libconfig_config, master_config );
        total_errors += parse_theme_config( &libconfig_config, master_config );

        // The error requirement being 0 may be a bit strict, I am not sure yet. May need
        // some relaxing or possibly come up with a better way of calculating if a config
        // passes, or is valid enough to warrant backing up.
        if ( total_errors == 0 && !master_config->default_binds_loaded ) {
                backup_config( &libconfig_config );
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

static int parse_bind_argument( const char *argument_string, const enum Argument_Type *arg_type, Arg *arg, const long double range_min, const long double range_max ) {

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

static int parse_bind_function( const char *function_string, enum Argument_Type *arg_type, void ( **function )( const Arg * ), long double *range_min, long double *range_max ) {

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
                { "spawn", parser_spawn, ARG_TYPE_POINTER },
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

static int parse_bind_modifier( const char *modifier_string, unsigned int *modifier ) {

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

static int parse_buttonbind( const char *buttonbind_string, Button *buttonbind, const unsigned int max_keys ) {

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
                if ( parse_bind_modifier( trimmed_modifier_token_list[ i ], &buttonbind->mask ) ) {
                        log_error( "Invalid modifier \"%s\" in buttonbind \"%s\"\n", trimmed_modifier_token_list[ i ], buttonbind_string );
                        return -1;
                }
        }

        if ( parse_buttonbind_button( trimmed_modifier_token_list[ modifier_token_count - 1 ], &buttonbind->button ) ) {
                log_error( "Invalid button \"%s\" in buttonbind \"%s\"\n", function_token, buttonbind_string );
                return -1;
        }

        if ( parse_buttonbind_click( click_token, &buttonbind->click ) ) {
                log_error( "Invalid click \"%s\" in buttonbind \"%s\"\n", function_token, buttonbind_string );
                return -1;
        }

        long double range_min, range_max;
        if ( parse_bind_function( function_token, &buttonbind->argument_type, &buttonbind->func, &range_min, &range_max ) ) {
                log_error( "Invalid function \"%s\" in buttonbind \"%s\"\n", function_token, buttonbind_string );
                return -1;
        }

        if ( buttonbind->argument_type != ARG_TYPE_NONE ) {
                if ( parse_bind_argument( argument_token, &buttonbind->argument_type, (Arg *) &buttonbind->arg, range_min, range_max ) ) {
                        log_error( "Invalid argument \"%s\" in buttonbind \"%s\"\n", argument_token, buttonbind_string );
                        return -1;
                }
        } else {
                log_trace( "Argument type none\n" );
        }

        return 0;
}

static int parse_buttonbind_button( const char *button_string, unsigned int *button ) {

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

static int parse_buttonbind_click( const char *click_string, unsigned int *click ) {

        const struct {
                const char *name;
                const int click;
        } click_alias_map[ ] = { { "tag", ClkTagBar }, { "layout", ClkLtSymbol }, { "status", ClkStatusText }, { "title", ClkWinTitle }, { "window", ClkClientWin }, { "desktop", ClkRootWin }, };

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

static int parse_buttonbinds_config( const config_t *config, Button **buttonbind_config, unsigned int *buttonbind_count, const unsigned int max_keys ) {

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
                        load_default_buttonbind_config( buttonbind_config, buttonbind_count );
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

                        if ( parse_buttonbind( config_setting_get_string( buttonbind ), &( *buttonbind_config )[ i ], max_keys ) == -1 ) {
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

static int parse_generic_settings( const config_t *config, Configuration *master_config ) {

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
                { "showbar", &master_config->showbar, TYPE_BOOL, true },
                { "topbar", &master_config->topbar, TYPE_BOOL, true },
                { "resizehints", &master_config->resizehints, TYPE_BOOL, true },
                { "lockfullscreen", &master_config->lockfullscreen, TYPE_BOOL, true },
                { "borderpx", &master_config->borderpx, TYPE_UINT, true, 0, 9999 },
                { "snap", &master_config->snap, TYPE_UINT, true, 0, 9999 },
                { "nmaster", &master_config->nmaster, TYPE_UINT, true, 0, 99 },
                { "refreshrate", &master_config->refreshrate, TYPE_UINT, true, 0, 999 },
                { "mfact", &master_config->mfact, TYPE_FLOAT, true, 0.05f, 0.95f },

                // Advanced
                { "max-keys", &master_config->max_keys, TYPE_INT, true, 1, 10 },
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

static int parse_keybind( const char *keybind_string, Key *keybind, const unsigned int max_keys ) {

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
        if ( parse_bind_function( function_token, &keybind->argument_type, &keybind->func, &range_min, &range_max ) ) {
                log_error( "Invalid function \"%s\" in keybind \"%s\"\n", function_token, keybind_string );
                return -1;
        }

        if ( keybind->argument_type != ARG_TYPE_NONE ) {
                if ( parse_bind_argument( argument_token, &keybind->argument_type, (Arg *) &keybind->arg, range_min, range_max ) ) {
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
                if ( parse_bind_modifier( trimmed_modifier_token_list[ i ], &keybind->mod ) ) {
                        log_error( "Invalid modifier \"%s\" in keybind \"%s\"\n", trimmed_modifier_token_list[ i ], keybind_string );
                        return -1;
                }
        }

        if ( parse_keybind_keysym( trimmed_modifier_token_list[ modifier_token_count - 1 ], &keybind->keysym ) ) {
                log_error( "Invalid keysym \"%s\" in keybind \"%s\"\n", trimmed_modifier_token_list[ modifier_token_count - 1 ], keybind_string );
                return -1;
        }

        return 0;
}

static int parse_keybind_keysym( const char *keysym_string, KeySym *keysym ) {

        log_trace( "Keysym being parsed: \"%s\"\n", keysym_string );

        *keysym = XStringToKeysym( keysym_string );
        if ( *keysym == NoSymbol ) return -1;

        KeySym dummy = 0; // Unused, just needs to exist to satisfy compiler
        XConvertCase( *keysym, keysym, &dummy );

        log_trace( "Keysym successfully parsed as parsed: \"%s\" -> 0x%lx\n", XKeysymToString( *keysym ), *keysym );
        return 0;
}

static int parse_keybinds_config( const config_t *config, Key **keybind_config, unsigned int *keybinds_count, const unsigned int max_keys ) {

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
                        load_default_keybind_config( keybind_config, keybinds_count );
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

                        if ( parse_keybind( config_setting_get_string( keybind ), &( *keybind_config )[ i ], max_keys ) == -1 ) {
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

static int parse_rules_string( const char *input_string, char **output_string ) {

        if ( input_string == NULL ) return -1;

        if ( strcasecmp( input_string, "null" ) == 0 ) {
                *output_string = strdup( "\0" );
                return 0;
        }

        *output_string = strdup( input_string );

        return 0;
}

static int parse_rules_config( const config_t *config, Rule **rules_config, unsigned int *rules_count ) {

        int failed_rules_count = 0;
        int failed_rules_elements_count = 0;

        const config_setting_t *rules = config_lookup( config, "rules" );
        if ( rules != NULL ) {
                *rules_count = config_setting_length( rules );

                if ( *rules_count == 0 ) {
                        log_warn( "No rules listed, exiting rules parsing\n" );
                        return 1;
                }

                log_debug( "Rules detected: %d\n", *rules_count );

                *rules_config = ecalloc( *rules_count, sizeof( Rule ) );

                const char *tmp_string = NULL;
                const config_setting_t *rule = NULL;

                for ( int i = 0; i < *rules_count; i++ ) {
                        rule = config_setting_get_elem( rules, i );
                        if ( rule != NULL ) {

                                libconfig_setting_lookup_string( rule, "class", &tmp_string, false );
                                if ( parse_rules_string( tmp_string, (char **) &( *rules_config )[ i ].class ) ) {
                                        log_error( "Problem parsing \"class\" value of rule %d\n", i + 1 );
                                        failed_rules_elements_count++;
                                }

                                libconfig_setting_lookup_string( rule, "instance", &tmp_string, false );
                                if ( parse_rules_string( tmp_string, (char **) &( *rules_config )[ i ].instance ) ) {
                                        log_error( "Problem parsing \"instance\" value of rule %d\n", i + 1 );
                                        failed_rules_elements_count++;
                                }

                                libconfig_setting_lookup_string( rule, "title", &tmp_string,false );
                                if ( parse_rules_string( tmp_string, (char **) &( *rules_config )[ i ].title ) ) {
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

static int parse_tags_config( const config_t *config, Configuration *master_config ) {

        int tags_failed_count = 0;

        const config_setting_t *tag_names = config_lookup( config, "tag-names" );
        if ( tag_names != NULL ) {
                int i = 0;
                const char *tag_name = NULL;
                const int tags_count = config_setting_length( tag_names );

                if ( tags_count == 0 ) {
                        log_warn( "No tag names detected while parsing config, default tag names will be used\n" );
                        return 1;
                }

                log_debug( "Tags detected: %d\n", tags_count );

                if ( tags_count > LENGTH( tags ) ) {
                        log_warn( "More than %lu tag names detected (%d were detected) while parsing config, only the first %lu will be used\n", LENGTH( tags ), tags_count, LENGTH( tags ) );
                } else if ( tags_count < LENGTH( tags ) ) {
                        log_warn( "Less than %lu tag names detected while parsing config, filler tags will be used for the remainder\n", LENGTH( tags ) );
                }

                for ( i = 0; i < tags_count && i < 9; i++ ) {
                        tag_name = config_setting_get_string_elem( tag_names, i );

                        if ( tag_name == NULL ) {
                                log_error( "Problem reading tag array element %d: Value doesn't exist or isn't a string\n", i + 1 );
                                tags_failed_count++;
                                continue;
                        }

                        SAFE_FREE( master_config->tags[ i ] );
                        master_config->tags[ i ] = strdup( tag_name );
                        if ( master_config->tags[ i ] == NULL ) {
                                log_error( "strdup failed while copying parsed tag %d\n", i );
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

static int parse_theme( const config_setting_t *theme, Configuration *master_config ) {

        const char *tmp_string = NULL;

        int theme_elements_failed_count = 0;

        const struct {
                const char *path;
                const char **value;
        } Theme_Mapping[ ] = {
                { "font", &master_config->font },
                { "normal-foreground", &master_config->theme[ SchemeNorm ][ ColFg ] },
                { "normal-background", &master_config->theme[ SchemeNorm ][ ColBg ] },
                { "normal-border", &master_config->theme[ SchemeNorm ][ ColBorder ] },
                { "selected-foreground", &master_config->theme[ SchemeSel ][ ColFg ] },
                { "selected-background", &master_config->theme[ SchemeSel ][ ColBg ] },
                { "selected-border", &master_config->theme[ SchemeSel ][ ColBorder ] },
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

static int parse_theme_config( const config_t *config, Configuration *master_config ) {

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

                        failed_theme_elements_count += parse_theme( theme, master_config );
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

void parser_spawn( const Arg *arg ) {

        // Process argv to work with default spawn() behavior
        const char *cmd = arg->v;
        char *argv[ ] = { "/bin/sh", "-c", (char *) cmd, NULL };
        log_debug( "Attempting to spawn \"%s\"\n", (char *) cmd );

        // Call spawn with our new processed value
        const Arg tmp = { .v = argv };
        spawn( &tmp );
}

static void setlayout_floating( const Arg *arg ) {
        const Arg tmp = { .v = &layouts[ 1 ] };
        setlayout( &tmp );
}

static void setlayout_monocle( const Arg *arg ) {
        const Arg tmp = { .v = &layouts[ 2 ] };
        setlayout( &tmp );
}

static void setlayout_tiled( const Arg *arg ) {
        const Arg tmp = { .v = &layouts[ 0 ] };
        setlayout( &tmp );
}
