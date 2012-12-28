/*
    DeaDBeeF - ultimate music player for GNU/Linux systems with X11
    Copyright (C) 2009-2012 Alexey Yakovenko <waker@users.sourceforge.net>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <linux/limits.h>
#include <stdlib.h>
#include <string.h>
#include <deadbeef/deadbeef.h>

#include <sega.h>
#include <satsound.h>
#include <dcsound.h>
#include <yam.h>

#include <psflib.h>

# define strdup(s)							      \
  (__extension__							      \
    ({									      \
      const char *__old = (s);						      \
      size_t __len = strlen (__old) + 1;				      \
      char *__new = (char *) malloc (__len);			      \
      (char *) memcpy (__new, __old, __len);				      \
    }))

extern DB_decoder_t ht_plugin;

#define trace(...) { fprintf(stderr, __VA_ARGS__); }
//#define trace(fmt,...)

static DB_functions_t *deadbeef;

#define min(x,y) ((x)<(y)?(x):(y))
#define max(x,y) ((x)>(y)?(x):(y))

#define BORK_TIME 0xC0CAC01A

static unsigned long parse_time_crap(const char *input)
{
    if (!input) return BORK_TIME;
    int len = strlen(input);
    if (!len) return BORK_TIME;
    int value = 0;
    {
        int i;
        for (i = len - 1; i >= 0; i--)
        {
            if ((input[i] < '0' || input[i] > '9') && input[i] != ':' && input[i] != ',' && input[i] != '.')
            {
                return BORK_TIME;
            }
        }
    }

    char * foo = strdup( input );

    if ( !foo )
        return BORK_TIME;

    char * bar = foo;
    char * strs = bar + strlen( foo ) - 1;
    char * end;
    while (strs > bar && (*strs >= '0' && *strs <= '9'))
    {
        strs--;
    }
    if (*strs == '.' || *strs == ',')
    {
        // fraction of a second
        strs++;
        if (strlen(strs) > 3) strs[3] = 0;
        value = strtoul(strs, &end, 10);
        switch (strlen(strs))
        {
        case 1:
            value *= 100;
            break;
        case 2:
            value *= 10;
            break;
        }
        strs--;
        *strs = 0;
        strs--;
    }
    while (strs > bar && (*strs >= '0' && *strs <= '9'))
    {
        strs--;
    }
    // seconds
    if (*strs < '0' || *strs > '9') strs++;
    value += strtoul(strs, &end, 10) * 1000;
    if (strs > bar)
    {
        strs--;
        *strs = 0;
        strs--;
        while (strs > bar && (*strs >= '0' && *strs <= '9'))
        {
            strs--;
        }
        if (*strs < '0' || *strs > '9') strs++;
        value += strtoul(strs, &end, 10) * 60000;
        if (strs > bar)
        {
            strs--;
            *strs = 0;
            strs--;
            while (strs > bar && (*strs >= '0' && *strs <= '9'))
            {
                strs--;
            }
            value += strtoul(strs, &end, 10) * 3600000;
        }
    }
    free( foo );
    return value;
}

struct psf_tag
{
    char * name;
    char * value;
    struct psf_tag * next;
};

static struct psf_tag * add_tag( struct psf_tag * tags, const char * name, const char * value )
{
    struct psf_tag * tag = malloc( sizeof( struct psf_tag ) );
    if ( !tag ) return tags;

    tag->name = strdup( name );
    if ( !tag->name ) {
        free( tag );
        return tags;
    }
    tag->value = strdup( value );
    if ( !tag->value ) {
        free( tag->name );
        free( tag );
        return tags;
    }
    tag->next = tags;
    return tag;
}

static void free_tags( struct psf_tag * tags )
{
    struct psf_tag * tag, * next;

    tag = tags;

    while ( tag )
    {
        next = tag->next;
        free( tag->name );
        free( tag->value );
        free( tag );
        tag = next;
    }
}

struct psf_load_state
{
    uint8_t * data;
    size_t data_size;

    int tag_song_ms;
    int tag_fade_ms;

    int utf8;

    struct psf_tag *tags;
};

static int psf_info_meta(void * context, const char * name, const char * value)
{
    struct psf_load_state * state = ( struct psf_load_state * ) context;

    if ( !strcasecmp( name, "length" ) )
    {
        unsigned long n = parse_time_crap( value );
        if ( n != BORK_TIME ) state->tag_song_ms = n;
    }
    else if ( !strcasecmp( name, "fade" ) )
    {
        unsigned long n = parse_time_crap( value );
        if ( n != BORK_TIME ) state->tag_fade_ms = n;
    }

    return 0;
}

static int psf_info_dump(void * context, const char * name, const char * value)
{
    struct psf_load_state * state = ( struct psf_load_state * ) context;

    if ( !strcasecmp( name, "length" ) )
    {
        unsigned long n = parse_time_crap( value );
        if ( n != BORK_TIME ) state->tag_song_ms = n;
    }
    else if ( !strcasecmp( name, "fade" ) )
    {
        unsigned long n = parse_time_crap( value );
        if ( n != BORK_TIME ) state->tag_fade_ms = n;
    }
    else if ( !strcasecmp( name, "utf8" ) )
    {
        state->utf8 = 1;
    }
    else if ( *name != '_' )
    {
        if ( !strcasecmp( name, "game" ) ) name = "album";
        else if ( !strcasecmp( name, "year" ) ) name = "date";
        else if ( !strcasecmp( name, "tracknumber" ) ) name = "track";
        else if ( !strcasecmp( name, "discnumber" ) ) name = "disc";

        state->tags = add_tag( state->tags, name, value );
    }

    return 0;
}

int sdsf_load(void * context, const uint8_t * exe, size_t exe_size,
                                  const uint8_t * reserved, size_t reserved_size)
{
    if ( exe_size < 4 ) return -1;

    struct psf_load_state * state = ( struct psf_load_state * ) context;

    uint8_t * dst = state->data;

    if ( state->data_size < 4 ) {
        state->data = dst = malloc( exe_size );
        state->data_size = exe_size;
        memcpy( dst, exe, exe_size );
        return 0;
    }

    uint32_t dst_start = *(uint32_t*)dst;
    uint32_t src_start = *(uint32_t*)exe;
    dst_start &= 0x7fffff;
    src_start &= 0x7fffff;
    uint32_t dst_len = state->data_size - 4;
    uint32_t src_len = exe_size - 4;
    if ( dst_len > 0x800000 ) dst_len = 0x800000;
    if ( src_len > 0x800000 ) src_len = 0x800000;

    if ( src_start < dst_start )
    {
        uint32_t diff = dst_start - src_start;
        state->data_size = dst_len + 4 + diff;
        state->data = dst = realloc( dst, state->data_size );
        memmove( dst + 4 + diff, dst + 4, dst_len );
        memset( dst + 4, 0, diff );
        dst_len += diff;
        dst_start = src_start;
        *(uint32_t*)dst = dst_start;
    }
    if ( ( src_start + src_len ) > ( dst_start + dst_len ) )
    {
        uint32_t diff = ( src_start + src_len ) - ( dst_start + dst_len );
        state->data_size = dst_len + 4 + diff;
        state->data = dst = realloc( dst, state->data_size );
        memset( dst + 4 + dst_len, 0, diff );
    }

    memcpy( dst + 4 + ( src_start - dst_start ), exe + 4, src_len );

    return 0;
}

static void * psf_file_fopen( const char * uri )
{
    return deadbeef->fopen( uri );
}

static size_t psf_file_fread( void * buffer, size_t size, size_t count, void * handle )
{
    return deadbeef->fread( buffer, size, count, handle );
}

static int psf_file_fseek( void * handle, int64_t offset, int whence )
{
    return deadbeef->fseek( handle, offset, whence );
}

static int psf_file_fclose( void * handle )
{
    deadbeef->fclose( handle );
    return 0;
}

static long psf_file_ftell( void * handle )
{
    return deadbeef->ftell( handle );
}

const psf_file_callbacks psf_file_system =
{
    "\\/|:",
    psf_file_fopen,
    psf_file_fread,
    psf_file_fseek,
    psf_file_fclose,
    psf_file_ftell
};

typedef struct {
    DB_fileinfo_t info;
    const char *path;
    void *emu;
    int psf_version;
    int samples_played;
    int samples_to_play;
    int samples_to_fade;
} ht_info_t;

DB_fileinfo_t *
ht_open (uint32_t hints) {
    DB_fileinfo_t *_info = (DB_fileinfo_t *)malloc (sizeof (ht_info_t));
    memset (_info, 0, sizeof (ht_info_t));
    return _info;
}

int
ht_init (DB_fileinfo_t *_info, DB_playItem_t *it) {
    ht_info_t *info = (ht_info_t *)_info;

    deadbeef->pl_lock ();
    const char * uri = info->path = strdup( deadbeef->pl_find_meta (it, ":URI") );
    deadbeef->pl_unlock ();
    int psf_version = psf_load( uri, &psf_file_system, 0, 0, 0, 0, 0 );
    if (psf_version < 0) {
        trace ("ht: failed to open %s\n", uri);
        return -1;
    }

    struct psf_load_state state;
    memset( &state, 0, sizeof(state) );

    if ( psf_load( uri, &psf_file_system, psf_version, sdsf_load, &state, psf_info_meta, &state ) <= 0 ) {
        if ( state.data ) free( state.data );
        trace( "ht: invalid PSF file\n" );
        return -1;
    }

    info->emu = malloc( sega_get_state_size( psf_version - 0x10 ) );
    if ( !info->emu ) {
        trace( "ht: out of memory\n" );
        if ( state.data ) free( state.data );
        return -1;
    }

    sega_clear_state( info->emu, psf_version - 0x10 );

    sega_enable_dry( info->emu, 1 );
    sega_enable_dsp( info->emu, 1 );

    sega_enable_dsp_dynarec( info->emu, 1 );

    if ( 1 )
    {
        void * yam = 0;
        if ( psf_version == 0x12 )
        {
            void * dcsound = sega_get_dcsound_state( info->emu );
            yam = dcsound_get_yam_state( dcsound );
        }
        else
        {
            void * satsound = sega_get_satsound_state( info->emu );
            yam = satsound_get_yam_state( satsound );
        }
        if ( yam ) yam_prepare_dynacode( yam );
    }

    uint32_t start  = *(uint32_t*) state.data;
    uint32_t length = state.data_size;
    const uint32_t max_length = ( psf_version == 0x12 ) ? 0x800000 : 0x80000;
    if ( ( start + ( length - 4 ) ) > max_length ) {
        length = max_length - start + 4;
    }
    sega_upload_program( info->emu, state.data, length );
    free( state.data );

    int tag_song_ms = state.tag_song_ms;
    int tag_fade_ms = state.tag_fade_ms;

    if (!tag_song_ms)
    {
        tag_song_ms = ( 2 * 60 + 50 ) * 1000;
        tag_fade_ms =            10   * 1000;
    }

    info->psf_version = psf_version;

    const int srate = 44100;

    info->samples_played = 0;
    info->samples_to_play = (uint64_t)tag_song_ms * (uint64_t)srate / 1000;
    info->samples_to_fade = (uint64_t)tag_fade_ms * (uint64_t)srate / 1000;

    _info->plugin = &ht_plugin;
    _info->fmt.channels = 2;
    _info->fmt.bps = 16;
    _info->fmt.samplerate = srate;
    _info->fmt.channelmask = _info->fmt.channels == 1 ? DDB_SPEAKER_FRONT_LEFT : (DDB_SPEAKER_FRONT_LEFT | DDB_SPEAKER_FRONT_RIGHT);
    _info->readpos = 0;

    return 0;
}

void
ht_free (DB_fileinfo_t *_info) {
    ht_info_t *info = (ht_info_t *)_info;
    if (info) {
        if (info->emu) {
            if ( 1 )
            {
                void * yam = 0;
                if ( info->psf_version == 0x12 )
                {
                    void * dcsound = sega_get_dcsound_state( info->emu );
                    yam = dcsound_get_yam_state( dcsound );
                }
                else
                {
                    void * satsound = sega_get_satsound_state( info->emu );
                    yam = satsound_get_yam_state( satsound );
                }
                if ( yam ) yam_unprepare_dynacode( yam );
            }
            free (info->emu);
            info->emu = NULL;
        }
        if (info->path) {
            free (info->path);
            info->path = NULL;
        }
        free (info);
    }
}

int
ht_read (DB_fileinfo_t *_info, char *bytes, int size) {
    ht_info_t *info = (ht_info_t *)_info;
    short * samples = (short *) bytes;
    uint32_t sample_count = size / ( 2 * sizeof(short) );

    if ( info->samples_played >= info->samples_to_play + info->samples_to_fade ) {
        return -1;
    }

    if ( sega_execute( info->emu, 0x7fffffff, samples, &sample_count ) < 0 ) {
        trace ( "ht: execution error\n" );
        return -1;
    }

    int samples_start = info->samples_played;
    int samples_end   = info->samples_played += sample_count;

    if ( samples && ( samples_end > info->samples_to_play ) )
    {
        int fade_start = info->samples_to_play;
        if ( fade_start < samples_start ) fade_start = samples_start;
        int samples_length = info->samples_to_play + info->samples_to_fade;
        int fade_end = samples_length;
        if ( fade_end > samples_end ) fade_end = samples_end;

        for ( int i = fade_start; i < fade_end; i++ )
        {
            samples[ ( i - samples_start ) * 2 + 0 ] = (int64_t)samples[ ( i - samples_start ) * 2 + 0 ] * ( samples_length - i ) / info->samples_to_fade;
            samples[ ( i - samples_start ) * 2 + 1 ] = (int64_t)samples[ ( i - samples_start ) * 2 + 1 ] * ( samples_length - i ) / info->samples_to_fade;
        }

        if ( samples_end > samples_length ) samples_end = samples_length;
    }

    return ( samples_end - samples_start ) * 2 * sizeof(short);
}

int
ht_seek_sample (DB_fileinfo_t *_info, int sample) {
    ht_info_t *info = (ht_info_t *)_info;
    unsigned long int s = sample;
    if (s < info->samples_played) {

        struct psf_load_state state;
        memset( &state, 0, sizeof(state) );

        if ( psf_load( info->path, &psf_file_system, info->psf_version, sdsf_load, &state, psf_info_meta, &state ) <= 0 ) {
            if ( state.data ) free( state.data );
            trace( "ht: invalid PSF file\n" );
            return -1;
        }

        sega_clear_state( info->emu, info->psf_version - 0x10 );

        sega_enable_dry( info->emu, 1 );
        sega_enable_dsp( info->emu, 1 );

        sega_enable_dsp_dynarec( info->emu, 1 );

        uint32_t start  = *(uint32_t*) state.data;
        uint32_t length = state.data_size;
        const uint32_t max_length = ( info->psf_version == 0x12 ) ? 0x800000 : 0x80000;
        if ( ( start + ( length - 4 ) ) > max_length ) {
            length = max_length - start + 4;
        }
        sega_upload_program( info->emu, state.data, length );
        free( state.data );

        info->samples_played = 0;
    }
    while ( info->samples_played < s ) {
        int to_skip = s - info->samples_played;
        if ( to_skip > 32768 ) to_skip = 1024;
        if ( ht_read( _info, NULL, to_skip * 2 * sizeof(short) ) < 0 ) {
            return -1;
        }
    }
    _info->readpos = s/(float)_info->fmt.samplerate;
    return 0;
}

int
ht_seek (DB_fileinfo_t *_info, float time) {
    return ht_seek_sample (_info, time * _info->fmt.samplerate);
}

static const char *
convstr (const char* str, int sz, char *out, int out_sz) {
    int i;
    for (i = 0; i < sz; i++) {
        if (str[i] != ' ') {
            break;
        }
    }
    if (i == sz) {
        out[0] = 0;
        return out;
    }

    const char *cs = deadbeef->junk_detect_charset (str);
    if (!cs) {
        return str;
    }
    else {
        if (deadbeef->junk_iconv (str, sz, out, out_sz, cs, "utf-8") >= 0) {
            return out;
        }
    }

    trace ("cdumb: failed to detect charset\n");
    return NULL;
}

DB_playItem_t *
ht_insert (ddb_playlist_t *plt, DB_playItem_t *after, const char *fname) {
    DB_playItem_t *it = NULL;

    struct psf_load_state state;
    memset( &state, 0, sizeof(state) );

    int psf_version = psf_load( fname, &psf_file_system, 0, 0, 0, psf_info_dump, &state );

    if ( psf_version < 0 )
        return after;

    if ( psf_version != 0x11 && psf_version != 0x12 )
        return after;

    int tag_song_ms = state.tag_song_ms;
    int tag_fade_ms = state.tag_fade_ms;

    if (!tag_song_ms)
    {
        tag_song_ms = ( 2 * 60 + 50 ) * 1000;
        tag_fade_ms =            10   * 1000;
    }

    it = deadbeef->pl_item_alloc_init (fname, ht_plugin.plugin.id);

    char junk_buffer[2][1024];

    struct psf_tag * tag = state.tags;
    while ( tag ) {
        if ( !strncasecmp( tag->name, "replaygain_", 11 ) ) {
            double fval = atof( tag->value );
            if ( !strcasecmp( tag->name + 11, "album_gain" ) ) {
                deadbeef->pl_set_item_replaygain( it, DDB_REPLAYGAIN_ALBUMGAIN, fval );
            } else if ( !strcasecmp( tag->name + 11, "album_peak" ) ) {
                deadbeef->pl_set_item_replaygain( it, DDB_REPLAYGAIN_ALBUMPEAK, fval );
            } else if ( !strcasecmp( tag->name + 11, "track_gain" ) ) {
                deadbeef->pl_set_item_replaygain( it, DDB_REPLAYGAIN_TRACKGAIN, fval );
            } else if ( !strcasecmp( tag->name + 11, "track_peak" ) ) {
                deadbeef->pl_set_item_replaygain( it, DDB_REPLAYGAIN_TRACKPEAK, fval );
            }
        } else {
            if ( !state.utf8 ) {
                junk_buffer[0][ 1023 ] = '\0';
                junk_buffer[1][ 1023 ] = '\0';
                deadbeef->pl_add_meta (it, convstr( tag->name, strlen( tag->name ), junk_buffer[0], 1023 ),
                        convstr( tag->value, strlen( tag->value ), junk_buffer[1], 1023 ));
            } else {
                deadbeef->pl_add_meta (it, tag->name, tag->value);
            }
        }
        tag = tag->next;
    }
    free_tags( state.tags );

    deadbeef->plt_set_item_duration (plt, it, (float)(tag_song_ms + tag_fade_ms) / 1000.f);
    deadbeef->pl_add_meta (it, ":FILETYPE", psf_version == 2 ? "PSF2" : "PSF");
    after = deadbeef->plt_insert_item (plt, after, it);
    deadbeef->pl_item_unref (it);
    return after;
}

int
ht_start (void) {
    sega_init();
    return 0;
}

int
ht_stop (void) {
    return 0;
}

DB_plugin_t *
ht_load (DB_functions_t *api) {
    deadbeef = api;
    return DB_PLUGIN (&ht_plugin);
}

static const char *exts[] = { "ssf", "minissf", "dsf", "minidsf", NULL };

// define plugin interface
DB_decoder_t ht_plugin = {
    .plugin.api_vmajor = 1,
    .plugin.api_vminor = 0,
    .plugin.type = DB_PLUGIN_DECODER,
    .plugin.version_major = 1,
    .plugin.version_minor = 0,
    .plugin.name = "Highly Theoretical SSF/DSF player",
    .plugin.descr = "SSF and DSF player based on Neill Corlett's Highly Theoretical.",
    .plugin.copyright = 
        "Copyright (C) 2003-2012 Chris Moeller <kode54@gmail.com>\n"
        "Copyright (C) 2003-2012 Neill Corlett <neill@neillcorlett.com>\n"
        "\n"
        "This program is free software; you can redistribute it and/or\n"
        "modify it under the terms of the GNU General Public License\n"
        "as published by the Free Software Foundation; either version 2\n"
        "of the License, or (at your option) any later version.\n"
        "\n"
        "This program is distributed in the hope that it will be useful,\n"
        "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
        "GNU General Public License for more details.\n"
        "\n"
        "You should have received a copy of the GNU General Public License\n"
        "along with this program; if not, write to the Free Software\n"
        "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n"
    ,
    .plugin.website = "http://github.com/kode54",
    .plugin.start = ht_start,
    .plugin.stop = ht_stop,
    .plugin.id = "ht",
    .open = ht_open,
    .init = ht_init,
    .free = ht_free,
    .read = ht_read,
    .seek = ht_seek,
    .seek_sample = ht_seek_sample,
    .insert = ht_insert,
    .exts = exts,
};
