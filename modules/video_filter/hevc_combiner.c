
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_picture.h> 

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  Create      ( vlc_object_t * );
static void Destroy     ( vlc_object_t * );

static picture_t *Filter( filter_t *, picture_t * );

struct filter_sys_t
{
    int field;
    uint8_t *field0_plane[3];
};

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
vlc_module_begin ()
    set_description( N_("HEVC Field Combiner filter") )
    set_shortname( N_("HEVC Field Combiner" ))
    set_category( CAT_VIDEO )
    set_subcategory( SUBCAT_VIDEO_VFILTER )
    set_capability( "video filter", 100 )
    add_shortcut( "hevc_combiner" )
    set_callbacks( Create, Destroy )
vlc_module_end ()

static int Create( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t *)p_this;
    vlc_fourcc_t fourcc = p_filter->fmt_in.video.i_chroma;

    // only supports I420
    if( fourcc != VLC_CODEC_I420) {
        return VLC_EGENERIC;
    }

    // only enabled for 1080/2, 480/2, 576/2
    if ((p_filter->fmt_in.video.i_visible_height != 540) &&
        (p_filter->fmt_in.video.i_visible_height != 288) &&
        (p_filter->fmt_in.video.i_visible_height != 280)) {
        return VLC_EGENERIC;
    }

    p_filter->fmt_out.video.i_visible_height = p_filter->fmt_out.video.i_visible_height * 2;
    p_filter->fmt_out.video.i_height = p_filter->fmt_out.video.i_height * 2;
    p_filter->fmt_out.video.i_interlaced = INTERLACED_INTERLACED_TOP_FIRST;
    msg_Dbg( p_filter, "output size: %d %d  (%d %d)", p_filter->fmt_in.video.i_width, p_filter->fmt_in.video.i_height, p_filter->fmt_in.video.i_visible_width, p_filter->fmt_in.video.i_visible_height );

    // allocate private data from filter
    p_filter->p_sys = malloc(sizeof(filter_sys_t));
    if (p_filter->p_sys == NULL)
        return VLC_ENOMEM;
    p_filter->p_sys->field = 0;
    memset(p_filter->p_sys, 0, sizeof(filter_sys_t));
    
    p_filter->pf_video_filter = Filter;
    return VLC_SUCCESS;
}

static void Destroy( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t *)p_this;
    if (p_filter->p_sys->field0_plane[0] != NULL) {
        free(p_filter->p_sys->field0_plane[0]);
        free(p_filter->p_sys->field0_plane[1]);
        free(p_filter->p_sys->field0_plane[2]);
    }
    free( p_filter->p_sys );

    (void)p_this;
}

static picture_t *Filter( filter_t *p_filter, picture_t *p_pic )
{
    if (!p_pic) return NULL;
    
    // only enabled for 1080/2, 480/2, 576/2
    if ((p_filter->fmt_in.video.i_visible_height != 540) &&
        (p_filter->fmt_in.video.i_visible_height != 280) &&
        (p_filter->fmt_in.video.i_visible_height != 288)) {
        return p_pic;
    }
    // only supports I420
    if (p_pic->format.i_chroma != VLC_CODEC_I420) {
        return p_pic;
    }

    //printf("%" PRId64 "\n", p_pic->date);
    //printf("%d %d\n", p_pic->b_top_field_first, p_pic->i_nb_fields);
    int field = p_filter->p_sys->field;
    if (p_pic->i_nb_fields == 2) {
        field = (p_pic->b_top_field_first & 0x1);
    }

    picture_t *p_outpic = NULL;
    
    // allocate field0 storage if we haven't already
    if (p_filter->p_sys->field0_plane[0] == NULL) {
        for (int i = 0; i < p_pic->i_planes; i++) {
            uint8_t *data = p_pic->p[i].p_pixels;
            int size = p_pic->p[i].i_pitch * p_pic->p[i].i_lines;
            p_filter->p_sys->field0_plane[i] = malloc(size);
        }
    }

    if (field == 0)
    {
        msg_Dbg(p_filter, "storing field0 data");
        for (int i = 0; i < p_pic->i_planes; i++) {
            uint8_t *data = p_pic->p[i].p_pixels;
            int size = p_pic->p[i].i_pitch * p_pic->p[i].i_lines;
            memcpy(p_filter->p_sys->field0_plane[i], data, size);
        }

        p_filter->p_sys->field = 1;
        picture_Release( p_pic );
        return NULL;
    }
    else if (field == 1)
    {
        // allocate output picture
        p_outpic = filter_NewPicture( p_filter );
        if (!p_outpic) {
            msg_Warn( p_filter, "can't get output picture" );
            picture_Release( p_pic );
            return NULL;
        }
        picture_CopyProperties( p_outpic, p_pic );

        msg_Dbg(p_filter, "combining fields");
        //p_outpic->format.i_interlaced = INTERLACED_INTERLACED_TOP_FIRST;
        p_outpic->date = p_pic->date;
        for (int i = 0; i < p_outpic->i_planes; i++) {
            uint8_t *data = p_outpic->p[i].p_pixels;
            int size = p_outpic->p[i].i_pitch * p_outpic->p[i].i_lines;
            msg_Dbg(p_filter, "target plane=%d, lines=%d, visible_lines=%d, size=%d, pitch=%d, visible_pitch=%d", i, p_outpic->p[i].i_lines, p_outpic->p[i].i_visible_lines, size, p_outpic->p[i].i_pitch, p_outpic->p[i].i_visible_pitch);

            int sourceLines = p_pic->p[i].i_lines - 2;
        
            uint8_t *sourceData = p_pic->p[i].p_pixels;
            int pitch = p_pic->p[i].i_pitch;
            int sourceSize = p_pic->p[i].i_pitch * p_pic->p[i].i_lines;

            msg_Dbg(p_filter, "source plane=%d, lines=%d, visible_lines=%d, size=%d, pitch=%d, visible_pitch=%d", i, p_pic->p[i].i_lines, p_pic->p[i].i_visible_lines, sourceSize, p_pic->p[i].i_pitch, p_pic->p[i].i_visible_pitch);
            
            int line = 0;
            uint8_t *target = data + (field * pitch);
            uint8_t *source = sourceData;
            uint8_t *field0 = p_filter->p_sys->field0_plane[i];

            while (line < sourceLines) {
                
                // line for field0
                memcpy(target, field0, pitch);
                target += pitch;

                // line from field1
                memcpy(target, source, pitch);
                target += pitch;

                // advance sources to next line
                source += pitch;
                field0 += pitch;
                line++;
            }
        }

        p_filter->p_sys->field = 0;
    }

    picture_Release( p_pic );
    return p_outpic;
}
