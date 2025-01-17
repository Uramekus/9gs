/* Copyright (C) 2001-2012 Artifex Software, Inc.
   All Rights Reserved.

   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied,
   modified or distributed except as expressly authorized under the terms
   of the license contained in the file LICENSE in this distribution.

   Refer to licensing information at http://www.artifex.com or contact
   Artifex Software, Inc.,  7 Mt. Lassen Drive - Suite A-134, San Rafael,
   CA  94903, U.S.A., +1(415)492-9861, for further information.
*/

/*  GS ICC Manager.  Initial stubbing of functions.  */

#include "std.h"
#include "stdpre.h"
#include "gstypes.h"
#include "gsmemory.h"
#include "gsstruct.h"
#include "scommon.h"
#include "strmio.h"
#include "gx.h"
#include "gxistate.h"
#include "gxcspace.h"
#include "gscms.h"
#include "gsicc_manage.h"
#include "gsicc_cache.h"
#include "gsicc_profilecache.h"
#include "gsicc_cms.h"
#include "gserrors.h"
#include "string_.h"
#include "gxclist.h"
#include "gxcldev.h"
#include "gzstate.h"
#include "gsicc_create.h"
#include "gpmisc.h"
#include "gxdevice.h"
#include <stdio.h>

#define ICC_HEADER_SIZE 128

#if ICC_DUMP
unsigned int global_icc_index = 0;
#endif

/* Static prototypes */

static void gsicc_set_default_cs_value(cmm_profile_t *picc_profile,
                                       gs_imager_state *pis);
static gsicc_namelist_t* gsicc_new_namelist(gs_memory_t *memory);
static gsicc_colorname_t* gsicc_new_colorname(gs_memory_t *memory);
static void gsicc_copy_colorname( const char *cmm_name,
                                 gsicc_colorname_t *colorname,
                                 gs_memory_t *memory );
static gsicc_namelist_t* gsicc_get_spotnames(gcmmhprofile_t profile,
                                             gs_memory_t *memory);
static void rc_gsicc_manager_free(gs_memory_t * mem, void *ptr_in,
                                  client_name_t cname);
static void rc_free_icc_profile(gs_memory_t * mem, void *ptr_in,
                                client_name_t cname);
static int gsicc_load_profile_buffer(cmm_profile_t *profile, stream *s,
                                     gs_memory_t *memory);
static stream* gsicc_open_search(const char* pname, int namelen,
                                 gs_memory_t *mem_gc,
                                 const char* dirname, int dir_namelen);
static int64_t gsicc_search_icc_table(clist_icctable_t *icc_table,
                                      int64_t icc_hashcode, int *size);
static int gsicc_load_namedcolor_buffer(cmm_profile_t *profile, stream *s,
                          gs_memory_t *memory);
static cmm_srcgtag_profile_t* gsicc_new_srcgtag_profile(gs_memory_t *memory);

/* profile data structure */
/* profile_handle should NOT be garbage collected since it is allocated by the external CMS */
gs_private_st_ptrs2(st_gsicc_colorname, gsicc_colorname_t, "gsicc_colorname",
                    gsicc_colorname_enum_ptrs, gsicc_colorname_reloc_ptrs, name, next);

gs_private_st_ptrs1(st_gsicc_manager, gsicc_manager_t, "gsicc_manager",
                    gsicc_manager_enum_ptrs, gsicc_manager_profile_reloc_ptrs,
                    smask_profiles);

gs_private_st_ptrs2(st_gsicc_devicen, gsicc_devicen_t, "gsicc_devicen",
                gsicc_devicen_enum_ptrs, gsicc_devicen_reloc_ptrs, head, final);

gs_private_st_ptrs2(st_gsicc_devicen_entry, gsicc_devicen_entry_t,
                    "gsicc_devicen_entry", gsicc_devicen_entry_enum_ptrs,
                    gsicc_devicen_entry_reloc_ptrs, iccprofile, next);

static const gs_color_space_type gs_color_space_type_icc = {
    gs_color_space_index_ICC,       /* index */
    true,                           /* can_be_base_space */
    true,                           /* can_be_alt_space */
    NULL                            /* This is going to be outside the norm. struct union*/
};

typedef struct default_profile_def_s {
    const char *path;
    gsicc_profile_t default_type;
} default_profile_def_t;

static default_profile_def_t default_profile_params[] =
{
    {DEFAULT_GRAY_ICC, DEFAULT_GRAY},
    {DEFAULT_RGB_ICC, DEFAULT_RGB},
    {DEFAULT_CMYK_ICC, DEFAULT_CMYK},
    {LAB_ICC, LAB_TYPE}
};

/* Get the size of the ICC profile that is in the buffer */
unsigned int gsicc_getprofilesize(unsigned char *buffer)
{
    return ( (buffer[0] << 24) + (buffer[1] << 16) +
             (buffer[2] << 8)  +  buffer[3] );
}

void gscms_set_icc_range(cmm_profile_t **icc_profile)
{
    int num_comp = (*icc_profile)->num_comps;
    int k;

    for ( k = 0; k < num_comp; k++) {
        (*icc_profile)->Range.ranges[k].rmin = 0.0;
        (*icc_profile)->Range.ranges[k].rmax = 1.0;
    }
}

cmm_profile_t*
gsicc_set_iccsmaskprofile(const char *pname,
                          int namelen, gsicc_manager_t *icc_manager,
                          gs_memory_t *mem)
{
    stream *str;
    int code;
    cmm_profile_t *icc_profile;

    if (icc_manager == NULL) {
        str = gsicc_open_search(pname, namelen, mem, NULL, 0);
    } else {
        str = gsicc_open_search(pname, namelen, mem, mem->gs_lib_ctx->profiledir,
                                mem->gs_lib_ctx->profiledir_len);
    }
    if (str != NULL) {
        icc_profile = gsicc_profile_new(str, mem, pname, namelen);
        code = sfclose(str);
        /* Get the profile handle */
        icc_profile->profile_handle =
            gsicc_get_profile_handle_buffer(icc_profile->buffer,
                                            icc_profile->buffer_size);
        /* Compute the hash code of the profile. Everything in the
           ICC manager will have it's hash code precomputed */
        gsicc_get_icc_buff_hash(icc_profile->buffer, &(icc_profile->hashcode),
                                        icc_profile->buffer_size);
        icc_profile->hash_is_valid = true;
        icc_profile->num_comps =
            gscms_get_input_channel_count(icc_profile->profile_handle);
        icc_profile->num_comps_out =
            gscms_get_output_channel_count(icc_profile->profile_handle);
        icc_profile->data_cs =
            gscms_get_profile_data_space(icc_profile->profile_handle);
        gscms_set_icc_range(&icc_profile);
        return(icc_profile);
    } else {
        return(NULL);
    }
}

gsicc_smask_t*
gsicc_new_iccsmask(gs_memory_t *memory)
{
    gsicc_smask_t *result;

    result = (gsicc_smask_t *) gs_alloc_bytes(memory, sizeof(gsicc_smask_t), "gsicc_new_iccsmask");
    if (result != NULL) {
        result->smask_gray = NULL;
        result->smask_rgb = NULL;
        result->smask_cmyk = NULL;
        result->memory = memory;
        result->swapped = false;
    }
    return(result);
}

/* Allocate a new structure to hold the profiles that contains the profiles
   used when we are in a softmask group */
int
gsicc_initialize_iccsmask(gsicc_manager_t *icc_manager)
{
    gs_memory_t *stable_mem = icc_manager->memory->stable_memory;

    /* Allocations need to be done in stable memory.  We want to maintain
       the smask_profiles object */
    icc_manager->smask_profiles = gsicc_new_iccsmask(stable_mem);
    if (icc_manager->smask_profiles == NULL)
        return gs_rethrow(-1, "insufficient memory to allocate smask profiles");
    /* Load the gray, rgb, and cmyk profiles */
    if ((icc_manager->smask_profiles->smask_gray =
        gsicc_set_iccsmaskprofile(SMASK_GRAY_ICC, strlen(SMASK_GRAY_ICC),
        icc_manager, stable_mem) ) == NULL) {
        return gs_rethrow(-1, "failed to load gray smask profile");
    }
    if ((icc_manager->smask_profiles->smask_rgb =
        gsicc_set_iccsmaskprofile(SMASK_RGB_ICC, strlen(SMASK_RGB_ICC),
        icc_manager, stable_mem)) == NULL) {
        return gs_rethrow(-1, "failed to load rgb smask profile");
    }
    if ((icc_manager->smask_profiles->smask_cmyk =
        gsicc_set_iccsmaskprofile(SMASK_CMYK_ICC, strlen(SMASK_CMYK_ICC),
        icc_manager, stable_mem)) == NULL) {
        return gs_rethrow(-1, "failed to load cmyk smask profile");
    }
    /* Set these as "default" so that pdfwrite or other high level devices
       will know that these are manufactured profiles, and default spaces
       should be used */
    icc_manager->smask_profiles->smask_gray->default_match = DEFAULT_GRAY;
    icc_manager->smask_profiles->smask_rgb->default_match = DEFAULT_RGB;
    icc_manager->smask_profiles->smask_cmyk->default_match = DEFAULT_CMYK;
    return(0);
}

static int
gsicc_new_devicen(gsicc_manager_t *icc_manager)
{
/* Allocate a new deviceN ICC profile entry in the deviceN list */
    gsicc_devicen_entry_t *device_n_entry =
        gs_alloc_struct(icc_manager->memory, gsicc_devicen_entry_t,
                &st_gsicc_devicen_entry, "gsicc_new_devicen");
    if (device_n_entry == NULL)
        return gs_rethrow(-1, "insufficient memory to allocate device n profile");
    device_n_entry->next = NULL;
    device_n_entry->iccprofile = NULL;
/* Check if we already have one in the manager */
    if ( icc_manager->device_n == NULL ) {
        /* First one.  Need to allocate the DeviceN main object */
        icc_manager->device_n = gs_alloc_struct(icc_manager->memory,
            gsicc_devicen_t, &st_gsicc_devicen, "gsicc_new_devicen");

        if (icc_manager->device_n == NULL)
            return gs_rethrow(-1, "insufficient memory to allocate device n profile");

        icc_manager->device_n->head = device_n_entry;
        icc_manager->device_n->final = device_n_entry;
        icc_manager->device_n->count = 1;
        return(0);
    } else {
        /* We have one or more in the list. */
        icc_manager->device_n->final->next = device_n_entry;
        icc_manager->device_n->final = device_n_entry;
        icc_manager->device_n->count++;
        return(0);
    }
}

cmm_profile_t*
gsicc_finddevicen(const gs_color_space *pcs, gsicc_manager_t *icc_manager)
{
    int k,j,i;
    gsicc_devicen_entry_t *curr_entry;
    int num_comps;
    const gs_separation_name *names = pcs->params.device_n.names;
    unsigned char *pname;
    unsigned int name_size;
    gsicc_devicen_t *devicen_profiles = icc_manager->device_n;
    gsicc_colorname_t *icc_spot_entry;
    int match_count = 0;
    bool permute_needed = false;

    num_comps = gs_color_space_num_components(pcs);

    /* Go through the list looking for a match */
    curr_entry = devicen_profiles->head;
    for ( k = 0; k < devicen_profiles->count; k++ ) {
        if (curr_entry->iccprofile->num_comps == num_comps ) {

            /* Now check the names.  The order is important
               since this is supposed to be the laydown order.
               If the order is off, the ICC profile will likely
               not be accurate.  The ICC profile drives the laydown
               order here.  A permutation vector is used to
               reorganize the data prior to the transform application */
            for ( j = 0; j < num_comps; j++) {
                /* Get the character string and length for the component name. */
                pcs->params.device_n.get_colorname_string(icc_manager->memory,
                                                    names[j], &pname, &name_size);
                /* Compare to the jth entry in the ICC profile */
                icc_spot_entry = curr_entry->iccprofile->spotnames->head;
                for ( i = 0; i < num_comps; i++) {
                    if( strncmp((const char *) pname,
                        icc_spot_entry->name, name_size) == 0 ) {
                        /* Found a match */
                        match_count++;
                        curr_entry->iccprofile->devicen_permute[j] = i;
                        if ( j != i) {
                            /* Document ink order does not match ICC
                               profile ink order */
                            permute_needed = true;
                        }
                        break;
                    } else
                        icc_spot_entry = icc_spot_entry->next;
                }
                if (match_count < j+1)
                    return(NULL);
            }
            if ( match_count == num_comps) {
                /* We have a match.  Order of components does not match laydown
                   order specified by the ICC profile.  Set a flag.  This may
                   be an issue if we are using 2 DeviceN color spaces with the
                   same colorants but with different component orders.  The problem
                   comes about since we would be sharing the profile in the
                   DeviceN entry of the icc manager. */
                curr_entry->iccprofile->devicen_permute_needed = permute_needed;
                return(curr_entry->iccprofile);
            }
            match_count = 0;
        }
    }
    return(NULL);
}

/* Populate the color names entries that should
   be contained in the DeviceN ICC profile */
static gsicc_namelist_t*
gsicc_get_spotnames(gcmmhprofile_t profile, gs_memory_t *memory)
{
    int k;
    gsicc_namelist_t *list;
    gsicc_colorname_t *name;
    gsicc_colorname_t **curr_entry;
    int num_colors;
    char *clr_name;

    num_colors = gscms_get_numberclrtnames(profile);
    if (num_colors == 0)
        return(NULL);
    /* Allocate structure for managing this */
    list = gsicc_new_namelist(memory);
    if (list == NULL)
        return(NULL);
    curr_entry = &(list->head);
    for (k = 0; k < num_colors; k++) {
       /* Allocate a new name object */
        name = gsicc_new_colorname(memory);
        *curr_entry = name;
        /* Get the name */
        clr_name = gscms_get_clrtname(profile, k);
        gsicc_copy_colorname(clr_name, *curr_entry, memory);
        curr_entry = &((*curr_entry)->next);
    }
    list->count = num_colors;
    return(list);
}

static void
gsicc_get_devicen_names(cmm_profile_t *icc_profile, gs_memory_t *memory)
{
    /* The names are contained in the
       named color tag.  We use the
       CMM to extract the data from the
       profile */
    if (icc_profile->profile_handle == NULL) {
        if (icc_profile->buffer != NULL) {
            icc_profile->profile_handle =
                gsicc_get_profile_handle_buffer(icc_profile->buffer,
                                                icc_profile->buffer_size);
        } else
            return;
    }
    icc_profile->spotnames =
        gsicc_get_spotnames(icc_profile->profile_handle, memory->non_gc_memory);
    return;
}

/* Allocate new spot name list object.  */
static gsicc_namelist_t*
gsicc_new_namelist(gs_memory_t *memory)
{
    gsicc_namelist_t *result;

    result = (gsicc_namelist_t *) gs_alloc_bytes(memory->non_gc_memory, sizeof(gsicc_namelist_t),
                             "gsicc_new_namelist");
    result->count = 0;
    result->head = NULL;
    return(result);
}

/* Allocate new spot name.  */
static gsicc_colorname_t*
gsicc_new_colorname(gs_memory_t *memory)
{
    gsicc_colorname_t *result;

    result = gs_alloc_struct(memory,gsicc_colorname_t,
                &st_gsicc_colorname, "gsicc_new_colorname");
    result->length = 0;
    result->name = NULL;
    result->next = NULL;
    return(result);
}

/* Copy the name from the CMM dependent stucture to ours */
void
gsicc_copy_colorname(const char *cmm_name, gsicc_colorname_t *colorname,
                     gs_memory_t *memory )
{
    int length;

    length = strlen(cmm_name);
    colorname->name = (char*) gs_alloc_bytes(memory, length,
                                        "gsicc_copy_colorname");
    strcpy(colorname->name, cmm_name);
    colorname->length = length;
}

/* If the profile is one of the default types that were set in the iccmanager,
   then the index for that type is returned.  Otherwise the ICC index is returned.
   This is currently used to keep us from writing out the default profiles for
   high level devices, if desired. */
gs_color_space_index
gsicc_get_default_type(cmm_profile_t *profile_data)
{
    switch ( profile_data->default_match ) {
        case DEFAULT_GRAY:
            return(gs_color_space_index_DeviceGray);
        case DEFAULT_RGB:
            return(gs_color_space_index_DeviceRGB);
        case DEFAULT_CMYK:
            return(gs_color_space_index_DeviceCMYK);
        case CIE_A:
            return(gs_color_space_index_CIEA);
        case CIE_ABC:
            return(gs_color_space_index_CIEABC);
        case CIE_DEF:
            return(gs_color_space_index_CIEDEF);
        case CIE_DEFG:
            return(gs_color_space_index_CIEDEFG);
        default:
            return(gs_color_space_index_ICC);
    }
}

/* This inititializes the srcgtag structure in the ICC manager */
int
gsicc_set_srcgtag_struct(gsicc_manager_t *icc_manager, const char* pname,
                        int namelen)
{
    gs_memory_t *mem;
    stream *str;
    int code;
    int info_size;
    char *buffer_ptr, *curr_ptr;
    int num_bytes;
    char str_format_key[6], str_format_file[6];
    int count;
    int k;
    static const char *const srcgtag_keys[] = {GSICC_SRCGTAG_KEYS};
    cmm_profile_t *icc_profile;
    int ri;
    cmm_srcgtag_profile_t *srcgtag;
    bool start = true;

    /* If we don't have an icc manager or if this thing is already set
       then ignore the call.  For now, I am going going to allow it to
       be set one time. */
    if (icc_manager == NULL || icc_manager->srcgtag_profile != NULL) {
        return 0;
    } else {
        mem = icc_manager->memory->non_gc_memory;
        str = gsicc_open_search(pname, namelen, mem, mem->gs_lib_ctx->profiledir,
                                mem->gs_lib_ctx->profiledir_len);
    }
    if (str != NULL) {
        /* Get the information in the file */
        code = sfseek(str,0,SEEK_END);
        info_size = sftell(str);
        code = srewind(str);
        if (info_size > (GSICC_NUM_SRCGTAG_KEYS + 1) * FILENAME_MAX) {
            return gs_rethrow1(-1, "setting of %s src obj color info failed",
                               pname);
        }
        /* Allocate the buffer, stuff with the data */
        buffer_ptr = (char*) gs_alloc_bytes(mem, info_size+1, 
                                            "gsicc_set_srcgtag_struct");
        if (buffer_ptr == NULL) {
            return gs_rethrow1(-1, "setting of %s src obj color info failed",
                               pname);
        }
        num_bytes = sfread(buffer_ptr,sizeof(unsigned char), info_size, str);
        code = sfclose(str);
        buffer_ptr[info_size] = 0;
        if (num_bytes != info_size) {
            gs_free_object(mem, buffer_ptr, "gsicc_set_srcgtag_struct");
            return gs_rethrow1(-1, "setting of %s src obj color info failed",
                               pname);
        }
        /* Create the structure in which we will store this data */
        srcgtag = gsicc_new_srcgtag_profile(mem);
        /* Now parse through the data opening the profiles that are needed */
        /* First create the format that we should read for the key */
        sprintf(str_format_key, "%%%ds", GSICC_SRCGTAG_MAX_KEY);
        sprintf(str_format_file, "%%%ds", FILENAME_MAX);
        curr_ptr = buffer_ptr;

        while (start || strlen(curr_ptr) > 0) {
            if (start) {
                curr_ptr = strtok(buffer_ptr, "\t,\32\n\r");
                start = false;
            } else {
                curr_ptr = strtok(NULL, "\t,\32\n\r");
            }
            if (curr_ptr == NULL) break;
            /* Now go ahead and see if we have a match */
            for (k = 0; k < GSICC_NUM_SRCGTAG_KEYS; k++) {
                if (strncmp(curr_ptr, srcgtag_keys[k], strlen(srcgtag_keys[k])) == 0 ) {
                    /* Try to open the file and set the profile */
                    curr_ptr = strtok(NULL, "\t,\32\n\r");
                    str = gsicc_open_search(curr_ptr, strlen(curr_ptr), mem, 
                                            mem->gs_lib_ctx->profiledir,
                                            mem->gs_lib_ctx->profiledir_len);
                    if (str != NULL) {
                        icc_profile =
                            gsicc_profile_new(str, mem, curr_ptr, strlen(curr_ptr));
                        code = sfclose(str);
                        gsicc_init_profile_info(icc_profile);
                        break;
                    } else {
                        /* Failed to open profile file. End this now. */
                        gs_free_object(mem, buffer_ptr, "gsicc_set_srcgtag_struct");
                        rc_decrement(srcgtag, "gsicc_set_srcgtag_struct");
                        return gs_rethrow1(-1,
                                "setting of %s src obj color info failed", pname);
                    }
                }
            }
            /* Get the intent now and set the profile. If GSICC_SRCGTAG_KEYS
               order changes this switch needs to change also */
            switch (k) {
                case COLOR_TUNE:
                    /* Color tune profile. No intent */
                    srcgtag->color_warp_profile = icc_profile;
                    break;
                case GRAPHIC_CMYK:
                    srcgtag->cmyk_profiles[gsSRC_GRAPPRO] = icc_profile;
                    /* Get the intent */
                    curr_ptr = strtok(NULL, "\t,\32\n\r");
                    count = sscanf(curr_ptr, "%d", &ri);
                    srcgtag->cmyk_intent[gsSRC_GRAPPRO] = ri | gsRI_OVERRIDE;
                    break;
                case IMAGE_CMYK:
                    srcgtag->cmyk_profiles[gsSRC_IMAGPRO] = icc_profile;
                    /* Get the intent */
                    curr_ptr = strtok(NULL, "\t,\32\n\r");
                    count = sscanf(curr_ptr, "%d", &ri);
                    srcgtag->cmyk_intent[gsSRC_IMAGPRO] = ri | gsRI_OVERRIDE;
                    break;
                case TEXT_CMYK:
                    srcgtag->cmyk_profiles[gsSRC_TEXTPRO] = icc_profile;
                    /* Get the intent */
                    curr_ptr = strtok(NULL, "\t,\32\n\r");
                    count = sscanf(curr_ptr, "%d", &ri);
                    srcgtag->cmyk_intent[gsSRC_TEXTPRO] = ri | gsRI_OVERRIDE;
                    break;
                case GRAPHIC_RGB:
                    srcgtag->rgb_profiles[gsSRC_GRAPPRO] = icc_profile;
                     /* Get the intent */
                    curr_ptr = strtok(NULL, "\t,\32\n\r");
                    count = sscanf(curr_ptr, "%d", &ri);
                    srcgtag->rgb_intent[gsSRC_GRAPPRO] = ri | gsRI_OVERRIDE;
                   break;
                case IMAGE_RGB:
                    srcgtag->rgb_profiles[gsSRC_IMAGPRO] = icc_profile;
                    /* Get the intent */
                    curr_ptr = strtok(NULL, "\t,\32\n\r");
                    count = sscanf(curr_ptr, "%d", &ri);
                    srcgtag->rgb_intent[gsSRC_IMAGPRO] = ri | gsRI_OVERRIDE;
                    break;
                case TEXT_RGB:
                    srcgtag->rgb_profiles[gsSRC_TEXTPRO] = icc_profile;
                    /* Get the intent */
                    curr_ptr = strtok(NULL, "\t,\32\n\r");
                    count = sscanf(curr_ptr, "%d", &ri);
                    srcgtag->rgb_intent[gsSRC_TEXTPRO] = ri | gsRI_OVERRIDE;
                    break;
                case GSICC_NUM_SRCGTAG_KEYS:
                    /* Failed to match the key */
                    gs_free_object(mem, buffer_ptr, "gsicc_set_srcgtag_struct");
                    rc_decrement(srcgtag, "gsicc_set_srcgtag_struct");
                    return gs_rethrow1(-1, "failed to find key in %s", pname);
                    break;
                default:
                    /* Some issue */
                    gs_free_object(mem, buffer_ptr, "gsicc_set_srcgtag_struct");
                    rc_decrement(srcgtag, "gsicc_set_srcgtag_struct");
                    return gs_rethrow1(-1, "Error in srcgtag data %s", pname);
                    break;
            }
        }
    } else {
        return gs_rethrow1(-1, "setting of %s src obj color info failed", pname);
    }
    gs_free_object(mem, buffer_ptr, "gsicc_set_srcgtag_struct");
    srcgtag->name_length = strlen(pname);
    srcgtag->name = (char*) gs_alloc_bytes(mem, srcgtag->name_length,
                                  "gsicc_set_srcgtag_struct");
    strncpy(srcgtag->name, pname, srcgtag->name_length);
    icc_manager->srcgtag_profile = srcgtag;
    return 0;
}

/*  This computes the hash code for the ICC data and assigns the code and the
    profile to the appropriate member variable in the ICC manager */
int
gsicc_set_profile(gsicc_manager_t *icc_manager, const char* pname, int namelen,
                  gsicc_profile_t defaulttype)
{
    cmm_profile_t *icc_profile;
    cmm_profile_t **manager_default_profile = NULL; /* quite compiler */
    stream *str;
    gs_memory_t *mem_gc = icc_manager->memory;
    int code;
    int k;
    int num_comps = 0;
    gsicc_colorbuffer_t default_space; /* Used to verify that we have the correct type */

    /* We need to check for the smask swapped profile condition.  If we are in
       that state, then any requests for setting the profile will be ignored.
       This is valid, since we are in the middle of drawing right now and this
       only would occur if we are doing a vmreclaim while in the middle of
       soft mask rendering */
    default_space = gsUNDEFINED;
    if (icc_manager->smask_profiles !=NULL &&
        icc_manager->smask_profiles->swapped == true) {
            return 0;
    } else {
        switch(defaulttype) {
            case DEFAULT_GRAY:
                manager_default_profile = &(icc_manager->default_gray);
                default_space = gsGRAY;
                num_comps = 1;
                break;
            case DEFAULT_RGB:
                manager_default_profile = &(icc_manager->default_rgb);
                default_space = gsRGB;
                num_comps = 3;
                break;
            case DEFAULT_CMYK:
                 manager_default_profile = &(icc_manager->default_cmyk);
                 default_space = gsCMYK;
                 num_comps = 4;
                 break;
            case NAMED_TYPE:
                 manager_default_profile = &(icc_manager->device_named);
                 default_space = gsNAMED;
                 break;
            case LAB_TYPE:
                 manager_default_profile = &(icc_manager->lab_profile);
                 num_comps = 3;
                 default_space = gsCIELAB;
                 break;
            case DEVICEN_TYPE:
                code = gsicc_new_devicen(icc_manager);
                 default_space = gsNCHANNEL;
                if (code == 0) {
                    manager_default_profile =
                        &(icc_manager->device_n->final->iccprofile);
                } else {
                    return code;
                }
                break;
            case DEFAULT_NONE:
            default:
                return 0;
                break;
        }
    }
    /* If it is not NULL then it has already been set. If it is different than
       what we already have then we will want to free it.  Since other imager
       states could have different default profiles, this is done via reference
       counting.  If it is the same as what we already have then we DONT
       increment, since that is done when the imager state is duplicated.  It
       could be the same, due to a resetting of the user params. To avoid
       recreating the profile data, we compare the string names. */
    if ((*manager_default_profile) != NULL) {
        /* Check if this is what we already have.  Also check if it is the
           output intent profile.  */
        icc_profile = *manager_default_profile;
        if ( namelen == icc_profile->name_length ) {
            if( memcmp(pname, icc_profile->name, namelen) == 0)
                return 0;
        }
        if (strncmp(icc_profile->name, OI_PROFILE, 
                    strlen(icc_profile->name)) == 0) {
                return 0;
        }
        rc_decrement(icc_profile,"gsicc_set_profile");
    }
    /* We need to do a special check for DeviceN since we have a linked list of
       profiles and we can have multiple specifications */
    if ( defaulttype == DEVICEN_TYPE ) {
        gsicc_devicen_entry_t *current_entry = icc_manager->device_n->head;
        for ( k = 0; k < icc_manager->device_n->count; k++ ) {
            if ( current_entry->iccprofile != NULL ) {
                icc_profile = current_entry->iccprofile;
                if ( namelen == icc_profile->name_length )
                    if( memcmp(pname, icc_profile->name, namelen) == 0)
                        return 0;
            }
            current_entry = current_entry->next;
        }
    }
    str = gsicc_open_search(pname, namelen, mem_gc, mem_gc->gs_lib_ctx->profiledir,
                                mem_gc->gs_lib_ctx->profiledir_len);
    if (str != NULL) {
        icc_profile = gsicc_profile_new(str, mem_gc, pname, namelen);
        /* Add check so that we detect cases where we are loading a named
           color structure that is not a standard profile type */
        if (icc_profile == NULL && defaulttype == NAMED_TYPE) {
            /* Failed to load the named color profile.  Just load the file
               into the buffer as it is.  The profile_handle member
               variable can then be used to hold the named color
               structure that is actually search. This is created later
               when needed. */
            char *nameptr;

            icc_profile = gsicc_profile_new(NULL, mem_gc, NULL, 0);
            icc_profile->data_cs = gsNAMED;
            code = gsicc_load_namedcolor_buffer(icc_profile, str, mem_gc);
            if (code < 0) gs_rethrow1(-1, "problems with profile %s", pname);
            *manager_default_profile = icc_profile;
            nameptr = (char*) gs_alloc_bytes(icc_profile->memory, namelen+1,
                                             "gsicc_set_profile");
            memcpy(nameptr, pname, namelen);
            nameptr[namelen] = '\0';
            icc_profile->name = nameptr;
            icc_profile->name_length = namelen;
            return 0;  /* Done now, since this is not a standard ICC profile */
        }
        code = sfclose(str);
        if (icc_profile == NULL) {
            return gs_rethrow1(-1, "problems with profile %s",pname);
        }
         *manager_default_profile = icc_profile;
        icc_profile->default_match = defaulttype;
        if (defaulttype == LAB_TYPE)
            icc_profile->islab = true;
        if ( defaulttype == DEVICEN_TYPE ) {
            /* Lets get the name information out of the profile.
               The names are contained in the icSigNamedColor2Tag
               item.  The table is in the A2B0Tag item.
               The names are in the order such that the fastest
               index in the table is the first name */
            gsicc_get_devicen_names(icc_profile, icc_manager->memory);
        }
        /* Delay the loading of the handle buffer until we need the profile. 
           But set some basic stuff that we need */
        icc_profile->num_comps = num_comps;
        icc_profile->num_comps_out = 3;
        gscms_set_icc_range(&icc_profile);
        icc_profile->data_cs = default_space;
        return 0;
    }
    return -1;
}

/* This is used ONLY for delayed initialization of the "default" ICC profiles
   that are in the ICC manager.  This way we avoid getting these profile handles
   until we actually need them. Note that defaulttype is preset.  These are
   the *only* profiles that are delayed in this manner.  All embedded profiles
   and internally generated profiles have their handles found immediately */
int
gsicc_initialize_default_profile(cmm_profile_t *icc_profile) 
{
    gsicc_profile_t defaulttype = icc_profile->default_match;
    gsicc_colorbuffer_t default_space = gsUNDEFINED;
    int num_comps, num_comps_out;

    /* Get the profile handle if it is not already set */
    if (icc_profile->profile_handle != NULL) {
        icc_profile->profile_handle = 
                        gsicc_get_profile_handle_buffer(icc_profile->buffer,
                                                        icc_profile->buffer_size);
        if (icc_profile->profile_handle == NULL) {
            return gs_rethrow1(-1, "allocation of profile %s handle failed", 
                               icc_profile->name);
        }
    }
    if (icc_profile->buffer != NULL && icc_profile->hash_is_valid == false) {
        /* Compute the hash code of the profile. */
        gsicc_get_icc_buff_hash(icc_profile->buffer, &(icc_profile->hashcode),
                                icc_profile->buffer_size);
        icc_profile->hash_is_valid = true;
    }
    num_comps = icc_profile->num_comps;
    icc_profile->num_comps =
        gscms_get_input_channel_count(icc_profile->profile_handle);
    num_comps_out = icc_profile->num_comps_out;
    icc_profile->num_comps_out =
        gscms_get_output_channel_count(icc_profile->profile_handle);
    icc_profile->data_cs =
        gscms_get_profile_data_space(icc_profile->profile_handle);
    if_debug0(gs_debug_flag_icc,"[icc] Setting ICC profile in Manager\n"); 
    switch(defaulttype) {
        case DEFAULT_GRAY:
            if_debug0(gs_debug_flag_icc,"[icc] Default Gray\n"); 
            default_space = gsGRAY;
            break;
        case DEFAULT_RGB:
            if_debug0(gs_debug_flag_icc,"[icc] Default RGB\n");
            default_space = gsRGB;
            break;
        case DEFAULT_CMYK:
            if_debug0(gs_debug_flag_icc,"[icc] Default CMYK\n");
            default_space = gsCMYK;
             break;
        case NAMED_TYPE:
            if_debug0(gs_debug_flag_icc,"[icc] Named Color\n"); 
             break;
        case LAB_TYPE:
            if_debug0(gs_debug_flag_icc,"[icc] CIELAB Profile\n"); 
             break;
        case DEVICEN_TYPE:
            if_debug0(gs_debug_flag_icc,"[icc] DeviceN Profile\n"); 
            break;
        case DEFAULT_NONE:
        default:
            return(0);
            break;
    }
    if_debug1(gs_debug_flag_icc,"[icc] name = %s\n", icc_profile->name); 
    if_debug1(gs_debug_flag_icc,"[icc] num_comps = %d\n", icc_profile->num_comps); 
    /* Check that we have the proper color space for the ICC
       profiles that can be externally set */
    if (default_space != gsUNDEFINED ||
        num_comps != icc_profile->num_comps ||
        num_comps_out != icc_profile->num_comps_out) {
        if (icc_profile->data_cs != default_space) {
            return gs_rethrow(-1, "A default profile has an incorrect color space");
        }
    }
    return 0;
}

/* This is used to get the profile handle given a file name  */
cmm_profile_t*
gsicc_get_profile_handle_file(const char* pname, int namelen, gs_memory_t *mem)
{
    cmm_profile_t *result;
    stream* str;
    int code;

    /* First see if we can get the stream.  NOTE  icc directory not used! */
    str = gsicc_open_search(pname, namelen, mem, NULL, 0);
    if (str != NULL) {
        result = gsicc_profile_new(str, mem, pname, namelen);
        code = sfclose(str);
        gsicc_init_profile_info(result);
        return(result);
    }
    return(NULL);
}

/* Given that we already have a profile in a buffer (e.g. generated from a PS object)
   this gets the handle and initializes the various member variables that we need */
void
gsicc_init_profile_info(cmm_profile_t *profile)
{
    int k;

    /* Get the profile handle */
    profile->profile_handle =
        gsicc_get_profile_handle_buffer(profile->buffer,
                                        profile->buffer_size);

    /* Compute the hash code of the profile. */
    gsicc_get_icc_buff_hash(profile->buffer, &(profile->hashcode),
                            profile->buffer_size);
    profile->hash_is_valid = true;
    profile->default_match = DEFAULT_NONE;
    profile->num_comps = gscms_get_input_channel_count(profile->profile_handle);
    profile->num_comps_out = gscms_get_output_channel_count(profile->profile_handle);
    profile->data_cs = gscms_get_profile_data_space(profile->profile_handle);

    /* Initialize the range to default values */
    for ( k = 0; k < profile->num_comps; k++) {
        profile->Range.ranges[k].rmin = 0.0;
        profile->Range.ranges[k].rmax = 1.0;
    }
}

/* This is used to try to find the specified or default ICC profiles */
/* This is where we would enhance the directory searching to use a   */
/* list of paths separated by ':' (unix) or ';' Windows              */
static stream*
gsicc_open_search(const char* pname, int namelen, gs_memory_t *mem_gc,
                  const char* dirname, int dirlen)
{
    char *buffer;
    stream* str;

    /* Check if we need to prepend the file name  */
    if ( dirname != NULL) {
        /* If this fails, we will still try the file by itself and with
           %rom% since someone may have left a space some of the spaces
           as our defaults, even if they defined the directory to use.
           This will occur only after searching the defined directory.
           A warning is noted.  */
        buffer = (char *) gs_alloc_bytes(mem_gc, namelen + dirlen + 1,
                                     "gsicc_open_search");
        strcpy(buffer, dirname);
        strcat(buffer, pname);
        /* Just to make sure we were null terminated */
        buffer[namelen + dirlen] = '\0';
        str = sfopen(buffer, "rb", mem_gc);
        gs_free_object(mem_gc, buffer, "gsicc_open_search");
        if (str != NULL) return(str);
    }

    /* First just try it like it is */
    str = sfopen(pname, "rb", mem_gc);
    if (str != NULL)
        return(str);

    /* If that fails, try %rom% */ /* FIXME: Not sure this is needed or correct */
                                   /* A better approach might be to have built in defaults */
    buffer = (char *) gs_alloc_bytes(mem_gc, 1 + namelen +
                        strlen(DEFAULT_DIR_ICC),"gsicc_open_search");
    strcpy(buffer, DEFAULT_DIR_ICC);
    strcat(buffer, pname);
    /* Just to make sure we were null terminated */
    buffer[namelen + strlen(DEFAULT_DIR_ICC)] = '\0';
    str = sfopen(buffer, "rb", mem_gc);
    gs_free_object(mem_gc, buffer, "gsicc_open_search");
    if (str == NULL) {
        gs_warn1("Could not find %s ",pname);
    }
    return(str);
}

/* Free source object icc array structure.  */
static void
rc_free_srcgtag_profile(gs_memory_t * mem, void *ptr_in, client_name_t cname)
{
    cmm_srcgtag_profile_t *srcgtag_profile = (cmm_srcgtag_profile_t *)ptr_in;
    int k;
    gs_memory_t *mem_nongc =  srcgtag_profile->memory;

    if (srcgtag_profile->rc.ref_count <= 1 ) {
        /* Decrement any profiles. */
        for (k = 0; k < NUM_SOURCE_PROFILES; k++) {
            if (srcgtag_profile->rgb_profiles[k] != NULL) {
                rc_decrement(srcgtag_profile->rgb_profiles[k],
                             "rc_free_srcgtag_profile");
            }
            if (srcgtag_profile->cmyk_profiles[k] != NULL) {
                rc_decrement(srcgtag_profile->cmyk_profiles[k],
                             "rc_free_srcgtag_profile");
            }
            if (srcgtag_profile->color_warp_profile != NULL) {
                rc_decrement(srcgtag_profile->color_warp_profile,
                             "rc_free_srcgtag_profile");
            }
        }
        gs_free_object(mem_nongc, srcgtag_profile->name, "rc_free_srcgtag_profile");
        gs_free_object(mem_nongc, srcgtag_profile, "rc_free_srcgtag_profile");
    }
}

/* Allocate source object icc structure. */
static cmm_srcgtag_profile_t*
gsicc_new_srcgtag_profile(gs_memory_t *memory)
{
    cmm_srcgtag_profile_t *result;
    int k;

    result = (cmm_srcgtag_profile_t *) gs_alloc_bytes(memory->non_gc_memory,
                                            sizeof(cmm_srcgtag_profile_t),
                                            "gsicc_new_srcgtag_profile");
    result->memory = memory->non_gc_memory;

    for (k = 0; k < NUM_SOURCE_PROFILES; k++) {
        result->rgb_profiles[k] = NULL;
        result->rgb_intent[k] = gsPERCEPTUAL;
        result->cmyk_profiles[k] = NULL;
        result->cmyk_intent[k] = gsPERCEPTUAL;
        result->color_warp_profile = NULL;
    }
    result->name = NULL;
    result->name_length = 0;
    rc_init_free(result, memory->non_gc_memory, 1, rc_free_srcgtag_profile);
    return(result);
}

/* Free device icc array structure.  */
static void
rc_free_profile_array(gs_memory_t * mem, void *ptr_in, client_name_t cname)
{
    cmm_dev_profile_t *icc_struct = (cmm_dev_profile_t *)ptr_in;
    int k;
    gs_memory_t *mem_nongc =  icc_struct->memory;

    if (icc_struct->rc.ref_count <= 1 ) {
        /* Decrement any profiles. */
        for (k = 0; k < NUM_DEVICE_PROFILES; k++) {
            if (icc_struct->device_profile[k] != NULL) {
                if_debug1(gs_debug_flag_icc,"[icc] Releasing device profile %d\n", k);
                rc_decrement(icc_struct->device_profile[k],
                             "rc_free_profile_array");
            }
        }
        if (icc_struct->link_profile != NULL) {
            if_debug0(gs_debug_flag_icc,"[icc] Releasing link profile\n");
            rc_decrement(icc_struct->link_profile, "rc_free_profile_array");
        }
        if (icc_struct->proof_profile != NULL) {
            if_debug0(gs_debug_flag_icc,"[icc] Releasing proof profile\n");
            rc_decrement(icc_struct->proof_profile, "rc_free_profile_array");
        }
        if (icc_struct->oi_profile != NULL) {
            if_debug0(gs_debug_flag_icc, "[icc] Releasing oi profile\n");
            rc_decrement(icc_struct->oi_profile, "rc_free_profile_array");
        }
        if_debug0(gs_debug_flag_icc,"[icc] Releasing device profile struct\n");
        gs_free_object(mem_nongc, icc_struct, "rc_free_profile_array");
    }
}

/* Allocate device icc structure. The actual profiles are in this structure */
cmm_dev_profile_t*
gsicc_new_device_profile_array(gs_memory_t *memory)
{
    cmm_dev_profile_t *result;
    int k;

    if_debug0(gs_debug_flag_icc,"[icc] Allocating device profile struct\n");
    result = (cmm_dev_profile_t *) gs_alloc_bytes(memory->non_gc_memory,
                                            sizeof(cmm_dev_profile_t),
                                            "gsicc_new_device_profile_array");
    result->memory = memory->non_gc_memory;

    for (k = 0; k < NUM_DEVICE_PROFILES; k++) {
        result->device_profile[k] = NULL;
        result->intent[k] = gsPERCEPTUAL;
    }
    result->proof_profile = NULL;
    result->link_profile = NULL;
    result->oi_profile = NULL;
    result->devicegraytok = true;  /* Default is to map gray to pure K */
    result->usefastcolor = false;  /* Default is to not use fast color */
    result->supports_devn = false;
    rc_init_free(result, memory->non_gc_memory, 1, rc_free_profile_array);
    return(result);
}

int
gsicc_set_device_profile_intent(gx_device *dev, gsicc_profile_types_t intent,
                                gsicc_profile_types_t profile_type)
{
    int code;
    cmm_dev_profile_t *profile_struct;
   
    if (dev->procs.get_profile == NULL) {
        profile_struct = dev->icc_struct;
    } else {
        code = dev_proc(dev, get_profile)(dev,  &profile_struct);
    }
    if (profile_struct ==  NULL)
        return 0;
    profile_struct->intent[profile_type] = intent;
    return 0;
}

/* This sets the device profiles. If the device does not have a defined
   profile, then a default one is selected. */
int
gsicc_init_device_profile_struct(gx_device * dev,
                                 char *profile_name,
                                 gsicc_profile_types_t profile_type)
{
    int code;
    cmm_profile_t *curr_profile;
    cmm_dev_profile_t *profile_struct;

    /* See if the device has a profile structure.  If it does, then do a
       check to see if the profile that we are trying to set is already
       set and the same.  If it is not, then we need to free it and then
       reset. */
    profile_struct = dev->icc_struct;
    if (profile_struct != NULL) {
        /* Get the profile of interest */
        if (profile_type < gsPROOFPROFILE) {
            curr_profile = profile_struct->device_profile[profile_type];      
        } else {
            /* The proof or link profile */
            if (profile_type == gsPROOFPROFILE) {
                curr_profile = profile_struct->proof_profile;      
            } else {
                curr_profile = profile_struct->link_profile; 
            } 
        }
        /* See if we have the same profile in this location */
        if (curr_profile != NULL) {
            /* There is something there now.  See if what we have coming in
               is different and it is not the output intent.  In this  */
            if (profile_name != NULL) {
                if (strncmp(curr_profile->name, profile_name,
                            strlen(profile_name)) != 0 &&
                    strncmp(curr_profile->name, OI_PROFILE, 
                            strlen(curr_profile->name)) != 0) {
                    /* A change in the profile.  rc decrement this one as it 
                       will be replaced */
                    rc_decrement(dev->icc_struct->device_profile[profile_type],
                                 "gsicc_init_device_profile_struct");
                } else {
                    /* Nothing to change.  It was either the same or is the
                       output intent */
                    return 0;
                }
            }
        }
    } else {
        /* We have no profile structure at all. Allocate the structure in
           non-GC memory.  */
        dev->icc_struct = gsicc_new_device_profile_array(dev->memory);
        profile_struct = dev->icc_struct;
    }
    /* Either use the incoming or a default */
    if (profile_name == NULL) {
        profile_name = 
            (char *) gs_alloc_bytes(dev->memory, 
                                    MAX_DEFAULT_ICC_LENGTH,
                                    "gsicc_init_device_profile_struct");
        switch(dev->color_info.num_components) {
            case 1:
                strncpy(profile_name, DEFAULT_GRAY_ICC, strlen(DEFAULT_GRAY_ICC));
                profile_name[strlen(DEFAULT_GRAY_ICC)] = 0;
                break;
            case 3:
                strncpy(profile_name, DEFAULT_RGB_ICC, strlen(DEFAULT_RGB_ICC));
                profile_name[strlen(DEFAULT_RGB_ICC)] = 0;
                break;
            case 4:
                strncpy(profile_name, DEFAULT_CMYK_ICC, strlen(DEFAULT_CMYK_ICC));
                profile_name[strlen(DEFAULT_CMYK_ICC)] = 0;
                break;
            default:
                strncpy(profile_name, DEFAULT_CMYK_ICC, strlen(DEFAULT_CMYK_ICC));
                profile_name[strlen(DEFAULT_CMYK_ICC)] = 0;
                break;
        }
        /* Go ahead and set the profile */
        code = gsicc_set_device_profile(dev, dev->memory, profile_name, 
                                        profile_type);
        gs_free_object(dev->memory, profile_name, 
                       "gsicc_init_device_profile_struct");
        return code;
    } else {
        /* Go ahead and set the profile */
        code = gsicc_set_device_profile(dev, dev->memory, profile_name, 
                                        profile_type);
        return code;
    }
}

/*  This computes the hash code for the device profile and assigns the profile
    in the icc_struct member variable of the device.  This should
    really occur only one time, but may occur twice if a color model is
    specified or a nondefault profile is specified on the command line */
int
gsicc_set_device_profile(gx_device * pdev, gs_memory_t * mem, 
                         char *file_name, gsicc_profile_types_t pro_enum)
{
    cmm_profile_t *icc_profile;
    stream *str;
    int code;

    /* Check if device has a profile for this slot. Note that we already
       decremented for any profile that we might be replacing
       in gsicc_init_device_profile_struct */
    if (file_name != '\0') {
        str = gsicc_open_search(file_name, strlen(file_name), mem,
                                mem->gs_lib_ctx->profiledir,
                                mem->gs_lib_ctx->profiledir_len);
        if (str != NULL) {
            icc_profile =
                gsicc_profile_new(str, mem, file_name, strlen(file_name));
            code = sfclose(str);
            if (pro_enum < gsPROOFPROFILE) {
                if_debug1(gs_debug_flag_icc, "[icc] Setting device profile %d\n", pro_enum);
                pdev->icc_struct->device_profile[pro_enum] = icc_profile;
            } else {
                /* The proof, link or output intent profile */
                if (pro_enum == gsPROOFPROFILE) {
                    if_debug0(gs_debug_flag_icc, "[icc] Setting proof profile\n");
                    pdev->icc_struct->proof_profile = icc_profile;
                } else {
                    if_debug0(gs_debug_flag_icc, "[icc] Setting link profile\n");
                    pdev->icc_struct->link_profile = icc_profile;
                } 
            }
            /* Get the profile handle */
            icc_profile->profile_handle =
                gsicc_get_profile_handle_buffer(icc_profile->buffer,
                                                icc_profile->buffer_size);
            /* Compute the hash code of the profile. Everything in the
               ICC manager will have it's hash code precomputed */
            gsicc_get_icc_buff_hash(icc_profile->buffer,
                                    &(icc_profile->hashcode),
                                    icc_profile->buffer_size);
            icc_profile->hash_is_valid = true;
            /* Get the number of channels in the output profile */
            icc_profile->num_comps =
                gscms_get_input_channel_count(icc_profile->profile_handle);
            if_debug1(gs_debug_flag_icc, "[icc] Profile has %d components\n", 
                      icc_profile->num_comps);
            icc_profile->num_comps_out =
                gscms_get_output_channel_count(icc_profile->profile_handle);
            icc_profile->data_cs =
                gscms_get_profile_data_space(icc_profile->profile_handle);
            /* We need to know if this is one of the "default" profiles or
               if someone has externally set it.  The reason is that if there
               is an output intent in the file, and someone wants to use the
               output intent our handling of the output intent profile is
               different depending upon if someone specified a particular
               output profile */
            switch (icc_profile->num_comps) {
                case 1:
                    if (strncmp(icc_profile->name, DEFAULT_GRAY_ICC, 
                    strlen(icc_profile->name)) == 0) {
                        icc_profile->default_match = DEFAULT_GRAY;
                    }
                    break;
                case 3:
                    if (strncmp(icc_profile->name, DEFAULT_RGB_ICC, 
                    strlen(icc_profile->name)) == 0) {
                        icc_profile->default_match = DEFAULT_RGB;
                    }
                    break;
                case 4:
                    if (strncmp(icc_profile->name, DEFAULT_CMYK_ICC, 
                    strlen(icc_profile->name)) == 0) {
                        icc_profile->default_match = DEFAULT_CMYK;
                    }
                    break;
                default:
                    break;
            }
            if_debug1(gs_debug_flag_icc, "[icc] Profile data CS is %d\n", 
                          icc_profile->data_cs);
        } else
            return gs_rethrow(-1, "cannot find device profile");
    }
    return(0);
}

/* Set the icc profile in the gs_color_space object */
int
gsicc_set_gscs_profile(gs_color_space *pcs, cmm_profile_t *icc_profile,
                       gs_memory_t * mem)
{
    if (pcs == NULL)
        return (-1);
#if ICC_DUMP
    if (icc_profile->buffer) {
        dump_icc_buffer(icc_profile->buffer_size, "set_gscs",
                        icc_profile->buffer);
        global_icc_index++;
    }
#endif

    rc_increment(icc_profile);
    if (pcs->cmm_icc_profile_data != NULL) {
        /* There is already a profile set there */
        /* free it and then set to the new one.  */
        /* should we check the hash code and retain if the same
           or place this job on the caller?  */
        rc_decrement(pcs->cmm_icc_profile_data, "gsicc_set_gscs_profile");
    }
    pcs->cmm_icc_profile_data = icc_profile;
    return(0);
}

cmm_profile_t *
gsicc_profile_new(stream *s, gs_memory_t *memory, const char* pname,
                  int namelen)
{
    cmm_profile_t *result;
    int code;
    char *nameptr;
    gs_memory_t *mem_nongc = memory->non_gc_memory;

    result = (cmm_profile_t*) gs_alloc_bytes(mem_nongc, sizeof(cmm_profile_t),
                                    "gsicc_profile_new");
    if (result == NULL)
        return result;
    memset(result,0,sizeof(gsicc_serialized_profile_t));
    if (namelen > 0) {
        nameptr = (char*) gs_alloc_bytes(mem_nongc, namelen+1,
                             "gsicc_profile_new");
        memcpy(nameptr, pname, namelen);
        nameptr[namelen] = '\0';
        result->name = nameptr;
    } else {
        result->name = NULL;
    }
    result->name_length = namelen;

    /* We may not have a stream if we are creating this
       object from our own constructed buffer.  For
       example if we are converting CalRGB to an ICC type */
    if ( s != NULL) {
        code = gsicc_load_profile_buffer(result, s, mem_nongc);
        if (code < 0) {
            gs_free_object(mem_nongc, result, "gsicc_profile_new");
            return NULL;
        }
    } else {
        result->buffer = NULL;
        result->buffer_size = 0;
    }
    rc_init_free(result, mem_nongc, 1, rc_free_icc_profile);
    result->profile_handle = NULL;
    result->spotnames = NULL;
    result->dev = NULL;
    result->memory = mem_nongc;
    result->lock = gx_monitor_alloc(mem_nongc);
    if (result->lock == NULL ) {
        gs_free_object(mem_nongc, result, "gsicc_profile_new");
        return(NULL);
    }
    if_debug1(gs_debug_flag_icc,"[icc] allocating ICC profile = 0x%x\n", result);
    return(result);
}

static void
rc_free_icc_profile(gs_memory_t * mem, void *ptr_in, client_name_t cname)
{
    cmm_profile_t *profile = (cmm_profile_t *)ptr_in;
    int k;
    gsicc_colorname_t *curr_name, *next_name;
    gs_memory_t *mem_nongc =  profile->memory;

    if_debug2(gs_debug_flag_icc,"[icc] rc decrement profile = 0x%x rc = %ld\n",
        ptr_in, profile->rc.ref_count);
    if (profile->rc.ref_count <= 1 ) {
        /* Clear out the buffer if it is full */
        if(profile->buffer != NULL) {
            gs_free_object(mem_nongc, profile->buffer, "rc_free_icc_profile");
            profile->buffer = NULL;
        }
        if_debug0(gs_debug_flag_icc,"[icc] profile freed\n");
        /* Release this handle if it has been set */
        if(profile->profile_handle != NULL) {
            gscms_release_profile(profile->profile_handle);
            profile->profile_handle = NULL;
        }
        /* Release the name if it has been set */
        if(profile->name != NULL) {
            gs_free_object(mem_nongc ,profile->name,"rc_free_icc_profile");
            profile->name = NULL;
            profile->name_length = 0;
        }
        profile->hash_is_valid = 0;
        if (profile->lock != NULL) {
            gs_free_object(mem_nongc ,profile->lock,"rc_free_icc_profile");
        }
        /* If we had a DeviceN profile with names
           deallocate that now */
        if (profile->spotnames != NULL) {
            curr_name = profile->spotnames->head;
            for ( k = 0; k < profile->spotnames->count; k++) {
                next_name = curr_name->next;
                /* Free the name */
                gs_free_object(mem_nongc, curr_name->name, "rc_free_icc_profile");
                /* Free the name structure */
                gs_free_object(mem_nongc, curr_name, "rc_free_icc_profile");
                curr_name = next_name;
            }
            /* Free the main object */
            gs_free_object(mem_nongc, profile->spotnames, "rc_free_icc_profile");
        }
        gs_free_object(mem_nongc, profile, "rc_free_icc_profile");
    }
}

/* We are just starting up.  We need to set the initial color space in the
   graphic state at this time */
int
gsicc_init_gs_colors(gs_state *pgs)
{
    int             code = 0;
    gs_color_space  *cs_old;
    gs_color_space  *cs_new;
    int k;

    if (pgs->in_cachedevice)
        return_error(gs_error_undefined);

    for (k = 0; k < 2; k++) {
        /* First do color space 0 */
        cs_old = pgs->color[k].color_space;
        cs_new = gs_cspace_new_DeviceGray(pgs->memory);
        rc_increment_cs(cs_new);
        pgs->color[k].color_space = cs_new;
        if ( (code = cs_new->type->install_cspace(cs_new, pgs)) < 0 ) {
            pgs->color[k].color_space = cs_old;
            rc_decrement_only_cs(cs_new, "gsicc_init_gs_colors");
            return code;
        } else {
            rc_decrement_only_cs(cs_old, "gsicc_init_gs_colors");
        }
    }
    return code;
}

/* Only set those that have not already been set. */
int
gsicc_init_iccmanager(gs_state * pgs)
{
    int code = 0, k;
    const char *pname;
    int namelen;
    gsicc_manager_t *iccmanager = pgs->icc_manager;
    cmm_profile_t *profile; 

    for (k = 0; k < 4; k++) {
        pname = default_profile_params[k].path;
        namelen = strlen(pname);

        switch(default_profile_params[k].default_type) {
            case DEFAULT_GRAY:
                profile = iccmanager->default_gray;
                break;
            case DEFAULT_RGB:
                profile = iccmanager->default_rgb;
                break;
            case DEFAULT_CMYK:
                 profile = iccmanager->default_cmyk;
                 break;
            default:
                profile = NULL;
        }
        if (profile == NULL)
            code = gsicc_set_profile(iccmanager, pname, namelen+1,
                                     default_profile_params[k].default_type);
        if (code < 0)
            return gs_rethrow(code, "cannot find default icc profile");
    }
    return 0;
}

gsicc_manager_t *
gsicc_manager_new(gs_memory_t *memory)
{
    gsicc_manager_t *result;

    /* Allocated in stable gc memory.  This done since the profiles
       may be introduced late in the process. */
    result = gs_alloc_struct(memory->stable_memory, gsicc_manager_t, &st_gsicc_manager,
                             "gsicc_manager_new");
    if ( result == NULL )
        return(NULL);
   rc_init_free(result, memory->stable_memory, 1, rc_gsicc_manager_free);
   result->default_gray = NULL;
   result->default_rgb = NULL;
   result->default_cmyk = NULL;
   result->lab_profile = NULL;
   result->graytok_profile = NULL;
   result->device_named = NULL;
   result->device_n = NULL;
   result->smask_profiles = NULL;
   result->memory = memory->stable_memory;
   result->srcgtag_profile = NULL;
   result->override_internal = false;
   result->override_ri = false;
   return(result);
}

static void
rc_gsicc_manager_free(gs_memory_t * mem, void *ptr_in, client_name_t cname)
{
    /* Ending the manager.  Decrement the ref counts of the profiles
       and then free the structure */
    gsicc_manager_t *icc_manager = (gsicc_manager_t * ) ptr_in;
    int k;
    gsicc_devicen_entry_t *device_n, *device_n_next;

   rc_decrement(icc_manager->default_cmyk, "rc_gsicc_manager_free");
   rc_decrement(icc_manager->default_gray, "rc_gsicc_manager_free");
   rc_decrement(icc_manager->default_rgb, "rc_gsicc_manager_free");
   rc_decrement(icc_manager->device_named, "rc_gsicc_manager_free");
   rc_decrement(icc_manager->lab_profile, "rc_gsicc_manager_free");
   rc_decrement(icc_manager->graytok_profile, "rc_gsicc_manager_free");
   rc_decrement(icc_manager->srcgtag_profile, "rc_gsicc_manager_free");

   /* Loop through the DeviceN profiles */
   if ( icc_manager->device_n != NULL) {
       device_n = icc_manager->device_n->head;
       for ( k = 0; k < icc_manager->device_n->count; k++) {
           rc_decrement(device_n->iccprofile, "rc_gsicc_manager_free");
           device_n_next = device_n->next;
           gs_free_object(icc_manager->memory, device_n, "rc_gsicc_manager_free");
           device_n = device_n_next;
       }
       gs_free_object(icc_manager->memory, icc_manager->device_n,
                      "rc_gsicc_manager_free");
   }
   /* The soft mask profiles */
   if ( icc_manager->smask_profiles != NULL) {
       rc_decrement(icc_manager->smask_profiles->smask_gray,
           "rc_gsicc_manager_free");
       rc_decrement(icc_manager->smask_profiles->smask_rgb,
           "rc_gsicc_manager_free");
       rc_decrement(icc_manager->smask_profiles->smask_cmyk,
           "rc_gsicc_manager_free");
   }
   gs_free_object(icc_manager->memory, icc_manager, "rc_gsicc_manager_free");
}

/* Allocates and loads the icc buffer from the stream. */
static int
gsicc_load_profile_buffer(cmm_profile_t *profile, stream *s,
                          gs_memory_t *memory)
{
    int                     num_bytes,profile_size;
    unsigned char           *buffer_ptr;
    int                     code;

    code = srewind(s);  /* Work around for issue with sfread return 0 bytes
                        and not doing a retry if there is an issue.  This
                        is a bug in the stream logic or strmio layer.  Occurs
                        with smask_withicc.pdf on linux 64 bit system */
    /* Get the size from doing a seek to the end and then a rewind instead
       of relying upon the profile size indicated in the header */
    code = sfseek(s,0,SEEK_END);
    profile_size = sftell(s);
    code = srewind(s);
    if (profile_size < ICC_HEADER_SIZE)
        return(-1);
    /* Allocate the buffer, stuff with the profile */
   buffer_ptr = gs_alloc_bytes(memory, profile_size,
                                        "gsicc_load_profile");
   if (buffer_ptr == NULL)
        return(-1);
   num_bytes = sfread(buffer_ptr,sizeof(unsigned char),profile_size,s);
   if( num_bytes != profile_size) {
       gs_free_object(memory, buffer_ptr, "gsicc_load_profile");
       return(-1);
   }
   profile->buffer = buffer_ptr;
   profile->buffer_size = num_bytes;
   return(0);
}

/* Allocates and loads the named color structure from the stream. */
static int
gsicc_load_namedcolor_buffer(cmm_profile_t *profile, stream *s,
                          gs_memory_t *memory)
{
    int                     num_bytes,profile_size;
    unsigned char           *buffer_ptr;
    int                     code;

    code = srewind(s);
    code = sfseek(s,0,SEEK_END);
    profile_size = sftell(s);
    code = srewind(s);
    /* Allocate the buffer, stuff with the profile */
   buffer_ptr = gs_alloc_bytes(memory, profile_size,
                                        "gsicc_load_profile");
   if (buffer_ptr == NULL)
        return(-1);
   num_bytes = sfread(buffer_ptr,sizeof(unsigned char),profile_size,s);
   if( num_bytes != profile_size) {
       gs_free_object(memory, buffer_ptr, "gsicc_load_profile");
       return(-1);
   }
   profile->buffer = buffer_ptr;
   profile->buffer_size = num_bytes;
   return(0);
}
/* Check if the embedded profile is the same as any of the default profiles */
static void
gsicc_set_default_cs_value(cmm_profile_t *picc_profile, gs_imager_state *pis)
{
    gsicc_manager_t *icc_manager = pis->icc_manager;
    int64_t hashcode = picc_profile->hashcode;

    if ( picc_profile->default_match == DEFAULT_NONE ) {
        switch ( picc_profile->data_cs ) {
            case gsGRAY:
                if ( hashcode == icc_manager->default_gray->hashcode )
                    picc_profile->default_match = DEFAULT_GRAY_s;
                break;
            case gsRGB:
                if ( hashcode == icc_manager->default_rgb->hashcode )
                    picc_profile->default_match = DEFAULT_RGB_s;
                break;
            case gsCMYK:
                if ( hashcode == icc_manager->default_cmyk->hashcode )
                    picc_profile->default_match = DEFAULT_CMYK_s;
                break;
            case gsCIELAB:
                if ( hashcode == icc_manager->lab_profile->hashcode )
                    picc_profile->default_match = LAB_TYPE_s;
                break;
            case gsCIEXYZ:
                return;
                break;
            case gsNCHANNEL:
                return;
                break;
            default:
                return;
        }
    }
}

/* Initialize the hash code value */
void
gsicc_init_hash_cs(cmm_profile_t *picc_profile, gs_imager_state *pis)
{
    if ( !(picc_profile->hash_is_valid) ) {
        gsicc_get_icc_buff_hash(picc_profile->buffer, &(picc_profile->hashcode),
                                picc_profile->buffer_size);
        picc_profile->hash_is_valid = true;
    }
    gsicc_set_default_cs_value(picc_profile, pis);
}

/* Interface code to get the profile handle for data
   stored in the clist device */
gcmmhprofile_t
gsicc_get_profile_handle_clist(cmm_profile_t *picc_profile, gs_memory_t *memory)
{
    gcmmhprofile_t profile_handle = NULL;
    unsigned int profile_size;
    int size;
    gx_device_clist_reader *pcrdev = (gx_device_clist_reader*) picc_profile->dev;
    unsigned char *buffer_ptr;
    int64_t position;
    gsicc_serialized_profile_t profile_header;
    int k;

    if( pcrdev != NULL) {

        /* Check ICC table for hash code and get the whole size icc raw buffer
           plus serialized header information */
        position = gsicc_search_icc_table(pcrdev->icc_table,
                                          picc_profile->hashcode, &size);
        if ( position < 0 )
            return(0);  /* Not found. */

        /* Get the ICC buffer.  We really want to avoid this transfer.
           I need to write  an interface to the CMM to do this through
           the clist ioprocs */
        /* Allocate the buffer */
        profile_size = size - sizeof(gsicc_serialized_profile_t);
        /* Profile and its members are ALL in non-gc memory */
        buffer_ptr = gs_alloc_bytes(memory->non_gc_memory, profile_size,
                                            "gsicc_get_profile_handle_clist");
        if (buffer_ptr == NULL)
            return(0);
        picc_profile->buffer = buffer_ptr;
        clist_read_chunk(pcrdev, position+sizeof(gsicc_serialized_profile_t),
            profile_size, (unsigned char *) buffer_ptr);
        profile_handle = gscms_get_profile_handle_mem(buffer_ptr, profile_size);
        /* We also need to get some of the serialized information */
        clist_read_chunk(pcrdev, position, sizeof(gsicc_serialized_profile_t),
                        (unsigned char *) (&profile_header));
        picc_profile->buffer_size = profile_header.buffer_size;
        picc_profile->data_cs = profile_header.data_cs;
        picc_profile->default_match = profile_header.default_match;
        picc_profile->hash_is_valid = profile_header.hash_is_valid;
        picc_profile->hashcode = profile_header.hashcode;
        picc_profile->islab = profile_header.islab;
        picc_profile->num_comps = profile_header.num_comps;
        for ( k = 0; k < profile_header.num_comps; k++ ) {
            picc_profile->Range.ranges[k].rmax =
                profile_header.Range.ranges[k].rmax;
            picc_profile->Range.ranges[k].rmin =
                profile_header.Range.ranges[k].rmin;
        }
        return(profile_handle);
     }
     return(0);
}

gcmmhprofile_t
gsicc_get_profile_handle_buffer(unsigned char *buffer, int profile_size)
{

    gcmmhprofile_t profile_handle = NULL;

     if( buffer != NULL) {
         if (profile_size < ICC_HEADER_SIZE) {
             return(0);
         }
         profile_handle = gscms_get_profile_handle_mem(buffer, profile_size);
         return(profile_handle);
     }
     return(0);
}

 /*  If we have a profile for the color space already, then we use that.
     If we do not have one then we will use data from
     the ICC manager that is based upon the current color space. */
 cmm_profile_t*
 gsicc_get_gscs_profile(gs_color_space *gs_colorspace,
                        gsicc_manager_t *icc_manager)
 {
     cmm_profile_t *profile = gs_colorspace->cmm_icc_profile_data;
     gs_color_space_index color_space_index =
            gs_color_space_get_index(gs_colorspace);
     int code;
     bool islab;

     if (profile != NULL )
        return(profile);
     /* else, return the default types */
     switch( color_space_index ) {
        case gs_color_space_index_DeviceGray:
            return(icc_manager->default_gray);
            break;
        case gs_color_space_index_DeviceRGB:
            return(icc_manager->default_rgb);
            break;
        case gs_color_space_index_DeviceCMYK:
            return(icc_manager->default_cmyk);
            break;
            /* Only used in 3x types */
        case gs_color_space_index_DevicePixel:
            return 0;
            break;
       case gs_color_space_index_DeviceN:
            /* If we made it to here, then we will need to use the
               alternate colorspace */
            return 0;
            break;
       case gs_color_space_index_CIEDEFG:
           /* For now just use default CMYK to avoid segfault.  MJV to fix */
           gs_colorspace->cmm_icc_profile_data = icc_manager->default_cmyk;
           rc_increment(icc_manager->default_cmyk);
           return(gs_colorspace->cmm_icc_profile_data);
           /* Need to convert to an ICC form */
           break;
        case gs_color_space_index_CIEDEF:
           /* For now just use default RGB to avoid segfault.  MJV to fix */
           gs_colorspace->cmm_icc_profile_data = icc_manager->default_rgb;
           rc_increment(icc_manager->default_rgb);
           return(gs_colorspace->cmm_icc_profile_data);
           /* Need to convert to an ICC form */
           break;
        case gs_color_space_index_CIEABC:
            gs_colorspace->cmm_icc_profile_data =
                gsicc_profile_new(NULL, icc_manager->memory, NULL, 0);
            code =
                gsicc_create_fromabc(gs_colorspace,
                        &(gs_colorspace->cmm_icc_profile_data->buffer),
                        &(gs_colorspace->cmm_icc_profile_data->buffer_size),
                        icc_manager->memory,
                        &(gs_colorspace->params.abc->caches.DecodeABC.caches[0]),
                        &(gs_colorspace->params.abc->common.caches.DecodeLMN[0]),
                        &islab);
            if (islab) {
                /* Destroy the profile */
                rc_decrement(gs_colorspace->cmm_icc_profile_data,
                             "gsicc_get_gscs_profile");
                /* This may be an issue for pdfwrite */
                return(icc_manager->lab_profile);
            }
            gs_colorspace->cmm_icc_profile_data->default_match = CIE_ABC;
            return(gs_colorspace->cmm_icc_profile_data);
            break;
        case gs_color_space_index_CIEA:
            gs_colorspace->cmm_icc_profile_data =
                gsicc_profile_new(NULL, icc_manager->memory, NULL, 0);
            code =
                gsicc_create_froma(gs_colorspace,
                            &(gs_colorspace->cmm_icc_profile_data->buffer),
                            &(gs_colorspace->cmm_icc_profile_data->buffer_size),
                            icc_manager->memory,
                            &(gs_colorspace->params.a->caches.DecodeA),
                            &(gs_colorspace->params.a->common.caches.DecodeLMN[0]));
            gs_colorspace->cmm_icc_profile_data->default_match = CIE_A;
            return(gs_colorspace->cmm_icc_profile_data);
            break;
        case gs_color_space_index_Separation:
            /* Caller should use named color path */
            return(0);
            break;
        case gs_color_space_index_Pattern:
        case gs_color_space_index_Indexed:
            /* Caller should use the base space for these */
            return(0);
            break;
        case gs_color_space_index_ICC:
            /* This should not occur, as the space
               should have had a populated profile handle */
            return(0);
            break;
     }
    return(0);
 }

static int64_t
gsicc_search_icc_table(clist_icctable_t *icc_table, int64_t icc_hashcode, int *size)
{
    int tablesize = icc_table->tablesize, k;
    clist_icctable_entry_t *curr_entry;

    curr_entry = icc_table->head;
    for (k = 0; k < tablesize; k++ ) {
        if ( curr_entry->serial_data.hashcode == icc_hashcode ) {
            *size = curr_entry->serial_data.size;
            return(curr_entry->serial_data.file_position);
        }
        curr_entry = curr_entry->next;
    }

    /* Did not find it! */
    *size = 0;
    return(-1);
}

/* This is used to get only the serial data from the clist.  We don't bother
   with the whole profile until we actually need it.  It may be that the link
   that we need is already in the link cache */
cmm_profile_t*
gsicc_read_serial_icc(gx_device *dev, int64_t icc_hashcode)
{
    cmm_profile_t *profile;
    int64_t position;
    int size;
    int code;
    gx_device_clist_reader *pcrdev = (gx_device_clist_reader*) dev;

    /* Create a new ICC profile structure */
    profile = gsicc_profile_new(NULL, pcrdev->memory, NULL, 0);
    if (profile == NULL)
        return(NULL);

    /* Check ICC table for hash code and get the whole size icc raw buffer
       plus serialized header information. Make sure the icc_table has
       been intialized */
    if (pcrdev->icc_table == NULL) {
        code = clist_read_icctable(pcrdev);
        if (code<0)
            return(NULL);
    }
    position = gsicc_search_icc_table(pcrdev->icc_table, icc_hashcode, &size);
    if ( position < 0 )
        return(NULL);

    /* Get the serialized portion of the ICC profile information */
    clist_read_chunk(pcrdev, position, sizeof(gsicc_serialized_profile_t),
                     (unsigned char *) profile);
    return(profile);
}

void
gsicc_profile_serialize(gsicc_serialized_profile_t *profile_data,
                        cmm_profile_t *icc_profile)
{
    int k;

    if (icc_profile == NULL)
        return;
    profile_data->buffer_size = icc_profile->buffer_size;
    profile_data->data_cs = icc_profile->data_cs;
    profile_data->default_match = icc_profile->default_match;
    profile_data->hash_is_valid = icc_profile->hash_is_valid;
    profile_data->hashcode = icc_profile->hashcode;
    profile_data->islab = icc_profile->islab;
    profile_data->num_comps = icc_profile->num_comps;
    for ( k = 0; k < profile_data->num_comps; k++ ) {
        profile_data->Range.ranges[k].rmax =
            icc_profile->Range.ranges[k].rmax;
        profile_data->Range.ranges[k].rmin =
            icc_profile->Range.ranges[k].rmin;
    }
}

/* Utility functions */

int
gsicc_getsrc_channel_count(cmm_profile_t *icc_profile)
{
    return(gscms_get_input_channel_count(icc_profile->profile_handle));
}

/*
 * Adjust the reference count of the profile. This is intended to support
 * applications (such as XPS) which maintain ICC profiles outside of the
 * graphic state.
 */
void
gsicc_profile_reference(cmm_profile_t *icc_profile, int delta)
{
    if (icc_profile != NULL)
        rc_adjust(icc_profile, delta, "gsicc_profile_reference");
}

void
gsicc_get_srcprofile(gsicc_colorbuffer_t data_cs,
                     gs_graphics_type_tag_t graphics_type_tag,
                     cmm_srcgtag_profile_t *srcgtag_profile,
                     cmm_profile_t **profile, 
                     gsicc_rendering_intents_t *rendering_intent)
{
    (*profile) = NULL;
    *rendering_intent = gsPERCEPTUAL;
    switch (graphics_type_tag & ~GS_DEVICE_ENCODES_TAGS) {
        case GS_UNKNOWN_TAG:
        case GS_UNTOUCHED_TAG:
        default:
            break;
        case GS_PATH_TAG:
            if (data_cs == gsRGB) {
                (*profile) = srcgtag_profile->rgb_profiles[gsSRC_GRAPPRO];
                *rendering_intent =  srcgtag_profile->rgb_intent[gsSRC_GRAPPRO];
            } else if (data_cs == gsCMYK) {
                (*profile) = srcgtag_profile->cmyk_profiles[gsSRC_GRAPPRO];
                *rendering_intent =  srcgtag_profile->cmyk_intent[gsSRC_GRAPPRO];
            }
            break;
        case GS_IMAGE_TAG:
            if (data_cs == gsRGB) {
                (*profile) = srcgtag_profile->rgb_profiles[gsSRC_IMAGPRO];
                *rendering_intent =  srcgtag_profile->rgb_intent[gsSRC_IMAGPRO];
            } else if (data_cs == gsCMYK) {
                (*profile) = srcgtag_profile->cmyk_profiles[gsSRC_IMAGPRO];
                *rendering_intent =  srcgtag_profile->cmyk_intent[gsSRC_IMAGPRO];
            }
            break;
        case GS_TEXT_TAG:
            if (data_cs == gsRGB) {
                (*profile) = srcgtag_profile->rgb_profiles[gsSRC_TEXTPRO];
                *rendering_intent =  srcgtag_profile->rgb_intent[gsSRC_TEXTPRO];
            } else if (data_cs == gsCMYK) {
                (*profile) = srcgtag_profile->cmyk_profiles[gsSRC_TEXTPRO];
                *rendering_intent =  srcgtag_profile->cmyk_intent[gsSRC_TEXTPRO];
            }
            break;
        }
}

void
gsicc_extract_profile(gs_graphics_type_tag_t graphics_type_tag,
                       cmm_dev_profile_t *profile_struct,
                       cmm_profile_t **profile,
                       gsicc_rendering_intents_t *rendering_intent)
{
    switch (graphics_type_tag & ~GS_DEVICE_ENCODES_TAGS) {
        case GS_UNKNOWN_TAG:
        case GS_UNTOUCHED_TAG:
        default:
            (*profile) = profile_struct->device_profile[0];
            *rendering_intent =  profile_struct->intent[0];
            break;
        case GS_PATH_TAG:
            if (profile_struct->device_profile[1] != NULL) {
                (*profile) = profile_struct->device_profile[1];
                *rendering_intent =  profile_struct->intent[1];
            } else {
                (*profile) = profile_struct->device_profile[0];
                *rendering_intent =  profile_struct->intent[0];
            }
            break;
        case GS_IMAGE_TAG:
            if (profile_struct->device_profile[2] != NULL) {
                (*profile) = profile_struct->device_profile[2];
                *rendering_intent =  profile_struct->intent[2];
            } else {
                (*profile) = profile_struct->device_profile[0];
                *rendering_intent =  profile_struct->intent[0];
            }
            break;
        case GS_TEXT_TAG:
            if (profile_struct->device_profile[3] != NULL) {
                (*profile) = profile_struct->device_profile[3];
                *rendering_intent =  profile_struct->intent[3];
            } else {
                (*profile) = profile_struct->device_profile[0];
                *rendering_intent =  profile_struct->intent[0];
            }
            break;
        }
}

/* internal ICC and rendering intent override control */
void
gs_setoverrideicc(gs_imager_state *pis, bool value)
{
    if (pis->icc_manager != NULL) {
        pis->icc_manager->override_internal = value;
    }
}
bool
gs_currentoverrideicc(const gs_imager_state *pis)
{
    if (pis->icc_manager != NULL) {
        return pis->icc_manager->override_internal;
    } else {
        return false;
    }
}
void
gs_setoverride_ri(gs_imager_state *pis, bool value)
{
    if (pis->icc_manager != NULL) {
        pis->icc_manager->override_ri = value;
    }
}
bool
gs_currentoverride_ri(const gs_imager_state *pis)
{
    if (pis->icc_manager != NULL) {
        return pis->icc_manager->override_ri;
    } else {
        return false;
    }
}

void
gsicc_setrange_lab(cmm_profile_t *profile)
{
    profile->Range.ranges[0].rmin = 0.0;
    profile->Range.ranges[0].rmax = 100.0;
    profile->Range.ranges[1].rmin = -128.0;
    profile->Range.ranges[1].rmax = 127.0;
    profile->Range.ranges[2].rmin = -128.0;
    profile->Range.ranges[2].rmax = 127.0;
}

#if ICC_DUMP
/* Debug dump of ICC buffer data */
static void
dump_icc_buffer(int buffersize, char filename[],byte *Buffer)
{
    char full_file_name[50];
    FILE *fid;

    sprintf(full_file_name,"%d)%s_debug.icc",global_icc_index,filename);
    fid = fopen(full_file_name,"wb");
    fwrite(Buffer,sizeof(unsigned char),buffersize,fid);
    fclose(fid);
}
#endif

/* The following are for setting the system/user params */
/* No default for the deviceN profile. */
void
gs_currentdevicenicc(const gs_state * pgs, gs_param_string * pval)
{
    static const char *const rfs = "";

    /*FIXME: This should return the entire list !!! */
    /*       Just return the first one for now      */
    if (pgs->icc_manager->device_n == NULL) {
        pval->data = (const byte *) rfs;
        pval->persistent = true;
    } else {
        pval->data = 
            (const byte *) (pgs->icc_manager->device_n->head->iccprofile->name);
        pval->persistent = false;
    }
    pval->size = strlen((const char *)pval->data);
}

int
gs_setdevicenprofileicc(const gs_state * pgs, gs_param_string * pval)
{
    int code = 0;
    char *pname, *pstr, *pstrend;
    int namelen = (pval->size)+1;
    gs_memory_t *mem = pgs->memory;

    /* Check if it was "NULL" */
    if (pval->size != 0) {
        /* The DeviceN name can have multiple files
           in it.  This way we can define all the
           DeviceN color spaces with ICC profiles.
           divide using , and ; delimeters as well as
           remove leading and ending spaces (file names
           can have internal spaces). */
        pname = (char *)gs_alloc_bytes(mem, namelen,
                                     "set_devicen_profile_icc");
        memcpy(pname,pval->data,namelen-1);
        pname[namelen-1] = 0;
        pstr = strtok(pname, ",;");
        while (pstr != NULL) {
            namelen = strlen(pstr);
            /* Remove leading and trailing spaces from the name */
            while ( namelen > 0 && pstr[0] == 0x20) {
                pstr++;
                namelen--;
            }
            namelen = strlen(pstr);
            pstrend = &(pstr[namelen-1]);
            while ( namelen > 0 && pstrend[0] == 0x20) {
                pstrend--;
                namelen--;
            }
            code = gsicc_set_profile(pgs->icc_manager, (const char*) pstr, namelen, DEVICEN_TYPE);
            if (code < 0)
                return gs_rethrow(code, "cannot find devicen icc profile");
            pstr = strtok(NULL, ",;");
        }
        gs_free_object(mem, pname,
        "set_devicen_profile_icc");
        return code;
    }
    return 0;
}

void
gs_currentdefaultgrayicc(const gs_state * pgs, gs_param_string * pval)
{
    static const char *const rfs = DEFAULT_GRAY_ICC;

    if (pgs->icc_manager->default_gray == NULL) {
        pval->data = (const byte *) rfs;
        pval->persistent = true;
    } else {
        pval->data = (const byte *) (pgs->icc_manager->default_gray->name);
        pval->persistent = false;
    }
    pval->size = strlen((const char *)pval->data);
}

int
gs_setdefaultgrayicc(const gs_state * pgs, gs_param_string * pval)
{
    int code;
    char *pname;
    int namelen = (pval->size)+1;
    gs_memory_t *mem = pgs->memory;
    bool not_initialized;

    /* Detect if this is our first time in here.  If so, then we need to
       reset up the default gray color spaces that are in the graphic state
       to be ICC based.  It was not possible to do it until after we get
       the profile */
    not_initialized = (pgs->icc_manager->default_gray == NULL);

    pname = (char *)gs_alloc_bytes(mem, namelen,
                             "set_default_gray_icc");
    memcpy(pname,pval->data,namelen-1);
    pname[namelen-1] = 0;
    code = gsicc_set_profile(pgs->icc_manager,
        (const char*) pname, namelen, DEFAULT_GRAY);
    gs_free_object(mem, pname,
        "set_default_gray_icc");
    if (code < 0)
        return gs_rethrow(code, "cannot find default gray icc profile");
    /* if this is our first time in here then we need to properly install the
       color spaces that were initialized in the graphic state at this time */
    if (not_initialized) {
        code = gsicc_init_gs_colors((gs_state*) pgs);
    }
    if (code < 0)
        return gs_rethrow(code, "error initializing gstate color spaces to icc");
    return code;
}

void
gs_currenticcdirectory(const gs_state * pgs, gs_param_string * pval)
{
    static const char *const rfs = DEFAULT_DIR_ICC;   /* as good as any other */
    const gs_lib_ctx_t *lib_ctx = pgs->memory->gs_lib_ctx;

    if (lib_ctx->profiledir == NULL) {
        pval->data = (const byte *)rfs;
        pval->size = strlen(rfs);
        pval->persistent = true;
    } else {
        pval->data = (const byte *)(lib_ctx->profiledir);
        pval->size = lib_ctx->profiledir_len;
        pval->persistent = false;
    }
}

int
gs_seticcdirectory(const gs_state * pgs, gs_param_string * pval)
{
    char *pname;
    int namelen = (pval->size)+1;
    const gs_memory_t *mem = pgs->memory;

    /* Check if it was "NULL" */
    if (pval->size != 0 ) {
        pname = (char *)gs_alloc_bytes((gs_memory_t *)mem, namelen,
		   		     "set_icc_directory");
        if (pname == NULL)
            return gs_rethrow(-1, "cannot allocate directory name");
        memcpy(pname,pval->data,namelen-1);
        pname[namelen-1] = 0;
        gs_lib_ctx_set_icc_directory(mem, (const char*) pname, namelen);
        gs_free_object((gs_memory_t *)mem, pname, "set_icc_directory");
    }
    return 0;
}

void
gs_currentsrcgtagicc(const gs_state * pgs, gs_param_string * pval)
{
    if (pgs->icc_manager->srcgtag_profile == NULL) {
        pval->data = NULL;
        pval->size = 0;
        pval->persistent = true;
    } else {
        pval->data = (byte *)pgs->icc_manager->srcgtag_profile->name;
        pval->size = strlen((const char *)pval->data);
        pval->persistent = false;
    }
}

int
gs_setsrcgtagicc(const gs_state * pgs, gs_param_string * pval)
{
    int code;
    char *pname;
    int namelen = (pval->size)+1;
    gs_memory_t *mem = pgs->memory;

    if (pval->size == 0) return 0;
    pname = (char *)gs_alloc_bytes(mem, namelen, "set_srcgtag_icc");
    memcpy(pname,pval->data,namelen-1);
    pname[namelen-1] = 0;
    code = gsicc_set_srcgtag_struct(pgs->icc_manager, (const char*) pname, 
                                   namelen);
    gs_free_object(mem, pname, "set_srcgtag_icc");
    if (code < 0)
        return gs_rethrow(code, "cannot find srctag file");
    return code;
}

void
gs_currentdefaultrgbicc(const gs_state * pgs, gs_param_string * pval)
{
    static const char *const rfs = DEFAULT_RGB_ICC;

    if (pgs->icc_manager->default_rgb == NULL) {
        pval->data = (const byte *) rfs;
        pval->persistent = true;
    } else {
        pval->data = (const byte *) (pgs->icc_manager->default_rgb->name);
        pval->persistent = false;
    }
    pval->size = strlen((const char *)pval->data);
}

int
gs_setdefaultrgbicc(const gs_state * pgs, gs_param_string * pval)
{
    int code;
    char *pname;
    int namelen = (pval->size)+1;
    gs_memory_t *mem = pgs->memory;

    pname = (char *)gs_alloc_bytes(mem, namelen,
                             "set_default_rgb_icc");
    memcpy(pname,pval->data,namelen-1);
    pname[namelen-1] = 0;
    code = gsicc_set_profile(pgs->icc_manager,
        (const char*) pname, namelen, DEFAULT_RGB);
    gs_free_object(mem, pname,
        "set_default_rgb_icc");
    if (code < 0)
        return gs_rethrow(code, "cannot find default rgb icc profile");
    return code;
}

void
gs_currentnamedicc(const gs_state * pgs, gs_param_string * pval)
{
    static const char *const rfs = "";

    if (pgs->icc_manager->device_named == NULL) {
        pval->data = (const byte *) rfs;
        pval->persistent = true;
    } else {
        pval->data = (const byte *) (pgs->icc_manager->device_named->name);
        pval->persistent = false;
    }
    pval->size = strlen((const char *)pval->data);
}

int
gs_setnamedprofileicc(const gs_state * pgs, gs_param_string * pval)
{
    int code;
    char* pname;
    int namelen = (pval->size)+1;
    gs_memory_t *mem = pgs->memory;

    /* Check if it was "NULL" */
    if (pval->size != 0) {
        pname = (char *)gs_alloc_bytes(mem, namelen,
                                 "set_named_profile_icc");
        memcpy(pname,pval->data,namelen-1);
        pname[namelen-1] = 0;
        code = gsicc_set_profile(pgs->icc_manager,
            (const char*) pname, namelen, NAMED_TYPE);
        gs_free_object(mem, pname,
                "set_named_profile_icc");
        if (code < 0)
            return gs_rethrow(code, "cannot find named color icc profile");
        return code;
    }
    return 0;
}

void
gs_currentdefaultcmykicc(const gs_state * pgs, gs_param_string * pval)
{
    static const char *const rfs = DEFAULT_CMYK_ICC;

    if (pgs->icc_manager->default_cmyk == NULL) {
        pval->data = (const byte *) rfs;
        pval->persistent = true;
    } else {
        pval->data = (const byte *) (pgs->icc_manager->default_cmyk->name);
        pval->persistent = false;
    }
    pval->size = strlen((const char *)pval->data);
}

int
gs_setdefaultcmykicc(const gs_state * pgs, gs_param_string * pval)
{
    int code;
    char* pname;
    int namelen = (pval->size)+1;
    gs_memory_t *mem = pgs->memory;

    pname = (char *)gs_alloc_bytes(mem, namelen,
                             "set_default_cmyk_icc");
    memcpy(pname,pval->data,namelen-1);
    pname[namelen-1] = 0;
    code = gsicc_set_profile(pgs->icc_manager,
        (const char*) pname, namelen, DEFAULT_CMYK);
    gs_free_object(mem, pname,
                "set_default_cmyk_icc");
    if (code < 0)
        return gs_throw(code, "cannot find default cmyk icc profile");
    return code;
}

void
gs_currentlabicc(const gs_state * pgs, gs_param_string * pval)
{
    static const char *const rfs = LAB_ICC;

    pval->data = (const byte *)( (pgs->icc_manager->lab_profile == NULL) ?
                        rfs : pgs->icc_manager->lab_profile->name);
    pval->size = strlen((const char *)pval->data);
    pval->persistent = true;
}

int
gs_setlabicc(const gs_state * pgs, gs_param_string * pval)
{
    int code;
    char* pname;
    int namelen = (pval->size)+1;
    gs_memory_t *mem = pgs->memory;

    pname = (char *)gs_alloc_bytes(mem, namelen,
                             "set_lab_icc");
    memcpy(pname,pval->data,namelen-1);
    pname[namelen-1] = 0;
    code = gsicc_set_profile(pgs->icc_manager,
        (const char*) pname, namelen, LAB_TYPE);
    gs_free_object(mem, pname,
                "set_lab_icc");
    if (code < 0)
        return gs_throw(code, "cannot find default lab icc profile");
    return code;
}
