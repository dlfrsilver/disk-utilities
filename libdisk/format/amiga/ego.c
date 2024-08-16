/*
 * disk/ego.c
 *
 * Custom format as used by Behind the Iron Gate, ABC Chem II,
 * Inferior, and Za Zelazna Brama
 *
 * Written in 2015/2024 by Keith Krellwitz
 *
 * RAW TRACK LAYOUT:
 *  u32 0x8951 ::  Sync (0x4489 for TRKTYP_abc_chemi_b)
 *  u32 dat[ti->len/4]
 *  u32 checksum
 *  Check sum is calculated EOR.L D1,D0 ROR.L #1,D0 over all data
 *
 * TRKTYP_za_zelazna_brama has specific track bit lengths used for
 * protection. The data used for the protection check is on track
 * 0.0 on disk 2.
 *
 * TRKTYP_abc_chemi_a and TRKTYP_abc_chemi_b have specific track bit
 * lengths used for protection (only b checks the protection). The data
 * used for the protection check is on track 67.0. Currenty only supports
 * two versions, will need to do some updates to support the loading of
 * track 67.0 in order to support all possible version.  Not a high
 * prioritys
 * 
 * TRKTYP_behind_the_iron_gate layout:
 *  u8 sector_data[6144]
 *
 * TRKTYP_za_zelazna_brama layout:
 *  u8 sector_data[6144]
 *
 * TRKTYP_abc_chemi_a layout:
 *  u8 sector_data[5632]
 * 
 * TRKTYP_abc_chemi_b layout:
 *  u8 sector_data[5632]
 * 
 * TRKTYP_abc_chemi_timsoft_a layout:
 *  u8 sector_data[5632]
 * 
 * TRKTYP_abc_chemi_timsoft_b layout:
 *  u8 sector_data[5632]
 * 
  * TRKTYP_inferior layout:
 *  u8 sector_data[5632]
 * 

 * TODO: Need to add the ability to read a specific track at the start of
 * the decoding in order to get the protection offsets for the entire disk.
 */

#include <libdisk/util.h>
#include <private/disk.h>

struct ego_info {
    uint16_t sync;
};

static uint32_t ego_sum(uint32_t w, uint32_t s)
{
    s ^= be32toh(w);
    return (s>>1) | (s<<31);
}

static const uint16_t abc_chem_protection[];
static const uint16_t abc_chem_timsoft_protection[];
static const uint16_t inferior_protection[];

static void *ego_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct ego_info *info = handlers[ti->type]->extra_data;
    while (stream_next_bit(s) != -1) {

        uint32_t raw[2], dat[ti->len/4], sum, csum;
        unsigned int i;
        char *block;

        if ((uint16_t)s->word != info->sync)
            continue;

        ti->data_bitoff = s->index_offset_bc - 15;

        for (i = sum = 0; i < ti->len/4; i++) {
            if (stream_next_bytes(s, raw, 8) == -1)
                goto fail;
            mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &dat[i]);
            sum = ego_sum(dat[i], sum);
        }

        if (stream_next_bytes(s, raw, 8) == -1)
            goto fail;
        mfm_decode_bytes(bc_mfm_odd_even, 4, raw, &csum);

        if (sum != be32toh(csum))
            continue;

        /* If it is the Za Zelazna Brama version then we need to set the
           total bit length of each track based on the data from track 
           0.0 of disk 2
        */
        if (ti->type == TRKTYP_za_zelazna_brama) {
            struct disktag_za_zelazna_brama_protection *protectiontag =
                (struct disktag_za_zelazna_brama_protection *)
                disk_get_tag_by_id(d, DSKTAG_za_zelazna_brama_protection);
            if ( protectiontag != NULL)
                ti->total_bits = 100900+((protectiontag->protection[tracknr]-0x720)+46);
        } else if (ti->type == TRKTYP_abc_chemii_a || ti->type == TRKTYP_abc_chemii_b) {
            ti->total_bits = 100900+((abc_chem_protection[tracknr]-0xa15));
            ti->data_bitoff=100900;
        } else if (ti->type == TRKTYP_abc_chemii_timsoft_a || ti->type == TRKTYP_abc_chemii_timsoft_b) {
            ti->total_bits = 100900+((abc_chem_timsoft_protection[tracknr]-0xa15));
            // Need to set the data_bitoff to 100900 or the protection checks seem to randomly fail
            ti->data_bitoff=100900;
        } else if (ti->type == TRKTYP_inferior) {
            ti->total_bits = 100900+((inferior_protection[tracknr]-0xa15));
            // Need to set the data_bitoff to 100900 or the protection checks seem to randomly fail
            ti->data_bitoff=100900;
        }

        block = memalloc(ti->len);
        memcpy(block, dat, ti->len);
        set_all_sectors_valid(ti);
        return block;
    }

fail:
    return NULL;

}

static void ego_read_raw(
    struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    const struct ego_info *info = handlers[ti->type]->extra_data;
    uint32_t *dat = (uint32_t *)ti->dat, sum;
    unsigned int i;

    tbuf_bits(tbuf, SPEED_AVG, bc_raw, 16, info->sync);

    for (i = sum = 0; i < ti->len/4; i++){
        tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, be32toh(dat[i]));
        sum = ego_sum(dat[i], sum);
    }

    tbuf_bits(tbuf, SPEED_AVG, bc_mfm_odd_even, 32, sum);
}

struct track_handler behind_the_iron_gate_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = ego_write_raw,
    .read_raw = ego_read_raw,
    .extra_data = & (struct ego_info) {
        .sync = 0x8951
    }
};

struct track_handler za_zelazna_brama_handler = {
    .bytes_per_sector = 6144,
    .nr_sectors = 1,
    .write_raw = ego_write_raw,
    .read_raw = ego_read_raw,
    .extra_data = & (struct ego_info) {
        .sync = 0x8951
    }
};


struct track_handler abc_chemii_a_handler = {
    .bytes_per_sector = 5632,
    .nr_sectors = 1,
    .write_raw = ego_write_raw,
    .read_raw = ego_read_raw,
    .extra_data = & (struct ego_info) {
        .sync = 0x8951
    }
};

struct track_handler abc_chemii_b_handler = {
    .bytes_per_sector = 5632,
    .nr_sectors = 1,
    .write_raw = ego_write_raw,
    .read_raw = ego_read_raw,
    .extra_data = & (struct ego_info) {
        .sync = 0x4489
    }
};

struct track_handler abc_chemii_timsoft_a_handler = {
    .bytes_per_sector = 5632,
    .nr_sectors = 1,
    .write_raw = ego_write_raw,
    .read_raw = ego_read_raw,
    .extra_data = & (struct ego_info) {
        .sync = 0x8951
    }
};

struct track_handler abc_chemii_timsoft_b_handler = {
    .bytes_per_sector = 5632,
    .nr_sectors = 1,
    .write_raw = ego_write_raw,
    .read_raw = ego_read_raw,
    .extra_data = & (struct ego_info) {
        .sync = 0x4489
    }
};

struct track_handler inferior_handler = {
    .bytes_per_sector = 5632,
    .nr_sectors = 1,
    .write_raw = ego_write_raw,
    .read_raw = ego_read_raw,
    .extra_data = & (struct ego_info) {
        .sync = 0x8951
    }
};

/*
 * TRKTYP_za_zelazna_brama_boot
 *
 * AmigaDOS-based track contains the data required to calculate
 * the total bit length of each track of disk 2.
 *
 * Written in 2023 by Keith Krellwitz
 *
 * Use the standard amigados handler to read and write the track,
 * but custom write method used in order to get the data for the
 * protection and make it available for all tracks being decoded
 * using a disktag.
 *
 * TRKTYP_za_zelazna_brama_boot data layout:
 *  u8 amigados[11][512]
 */

static void *za_zelazna_brama_boot_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    char *ablk;
    uint16_t dat[152];
    unsigned int i, j;
    struct disktag_za_zelazna_brama_protection *protectiontag =
        (struct disktag_za_zelazna_brama_protection *)
        disk_get_tag_by_id(d, DSKTAG_za_zelazna_brama_protection);

    init_track_info(ti, TRKTYP_amigados);
    ablk = handlers[TRKTYP_amigados]->write_raw(d, tracknr, s);
    if ((ablk == NULL) || (ti->type != TRKTYP_amigados))
        goto fail;

    stream_reset(s);

    j = 0;
    for (i = 4; i < 308; i+=2) {
        dat[j] = (uint16_t)(((uint8_t)ablk[i] << 8) | (uint8_t)ablk[i+1]);
        j++;
    }

     if (protectiontag == NULL) {
        protectiontag = (struct disktag_za_zelazna_brama_protection *)
            disk_set_tag(d, DSKTAG_za_zelazna_brama_protection, 308, &dat);
     }

    return ablk;

fail:
    memfree(ablk);
    return NULL;
}

struct track_handler za_zelazna_brama_boot_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = za_zelazna_brama_boot_write_raw
};

static const uint16_t abc_chem_protection[] = {
    0x2001, 0x7401, 0x0A57, 0x0A58, 0x0A56, 0x0A59, 0x0A58, 0x0A58,
    0x0A57, 0x0A59, 0x0A57, 0x0A58, 0x0A57, 0x0A57, 0x0A58, 0x0A58,
    0x0A57, 0x0A57, 0x0A58, 0x0A58, 0x0A57, 0x0A58, 0x0A57, 0x0A58,
    0x0A56, 0x0A58, 0x0A57, 0x0A58, 0x0A55, 0x0A58, 0x0A57, 0x0A59,
    0x0A55, 0x0A58, 0x0A56, 0x0A58, 0x0A56, 0x0A58, 0x0A56, 0x0A59,
    0x0A57, 0x0A58, 0x0A58, 0x0A59, 0x0A57, 0x0A58, 0x0A58, 0x0A59,
    0x0A57, 0x0A58, 0x0A57, 0x0A58, 0x0A56, 0x0A57, 0x0A56, 0x0A59,
    0x0A57, 0x0A56, 0x0A57, 0x0A58, 0x0A58, 0x0A57, 0x0A58, 0x0A58,
    0x0A58, 0x0A58, 0x0A57, 0x0A57, 0x0A59, 0x0A58, 0x0A57, 0x0A58,
    0x0A58, 0x0A58, 0x0A58, 0x0A58, 0x0A57, 0x0A58, 0x0A55, 0x0A57,
    0x0A57, 0x0A58, 0x0A56, 0x0A57, 0x0A57, 0x0A58, 0x0A57, 0x0A58,
    0x0A57, 0x0A59, 0x0A58, 0x0A58, 0x0A57, 0x0A59, 0x0A57, 0x0A59,
    0x0A56, 0x0A59, 0x0A57, 0x0A59, 0x0A58, 0x0A58, 0x0A58, 0x0A58,
    0x0A58, 0x0A58, 0x0A59, 0x0A58, 0x0A58, 0x0A58, 0x0A59, 0x0A57,
    0x0A58, 0x0A58, 0x0A57, 0x0A58, 0x0A57, 0x0A58, 0x0A59, 0x0A57,
    0x0A55, 0x0A56, 0x0A55, 0x0A56, 0x0A55, 0x0A56, 0x0A55, 0x0A57,
    0x0A55, 0x0A56, 0x0A55, 0x0A55, 0x0A55, 0x0A55, 0x0A56, 0x0A55,
    0x0A56, 0x0A56, 0x0A56, 0x0A56, 0x0A55, 0x0A56, 0x0A55, 0x0A56,
    0x0A54, 0x0A56, 0x0A55, 0x0A56, 0x0A56, 0x0A56, 0x0A56, 0x0A56,
    0x0A55, 0x0A56, 0x0A57, 0x0A56, 0x0A57, 0x0A56, 0x0A57, 0x0A56
};

static const uint16_t abc_chem_timsoft_protection[] = {
    0x0200, 0xFFFF, 0x0BB3, 0x0B98, 0x0BC8, 0x0B9A, 0x0BBA, 0x0B9B,
    0x0BC7, 0x0BA8, 0x0BBE, 0x0BA0, 0x0BA8, 0x0BA1, 0x0BB1, 0x0BA6,
    0x0BC1, 0x0B9D, 0x0BBE, 0x0BB3, 0x0BAF, 0x0BA8, 0x0BA1, 0x0B9D,
    0x0BAF, 0x0B95, 0x0BAD, 0x0BA8, 0x0BBF, 0x0BB0, 0x0B9F, 0x0BA7,
    0x0BC0, 0x0B9C, 0x0BB1, 0x0B9A, 0x0BB7, 0x0B98, 0x0BAB, 0x0B9E,
    0x0BB7, 0x0BA6, 0x0BB8, 0x0B9B, 0x0BB3, 0x0BA4, 0x0BA5, 0x0B95,
    0x0BB7, 0x0BA5, 0x0BB2, 0x0BA3, 0x0BA8, 0x0BA0, 0x0B9F, 0x0BA8,
    0x0BAA, 0x0BA8, 0x0BB9, 0x0BA3, 0x0BA6, 0x0B9C, 0x0BA9, 0x0BA7,
    0x0BA3, 0x0B9A, 0x0BBD, 0x0B9D, 0x0BB0, 0x0B97, 0x0BAE, 0x0BA1,
    0x0B9E, 0x0BA5, 0x0BA1, 0x0BA2, 0x0B9C, 0x0B9B, 0x0BA8, 0x0BB4,
    0x0BAF, 0x0B9A, 0x0BA1, 0x0B9A, 0x0BA4, 0x0B9F, 0x0BAF, 0x0BAC,
    0x0BA3, 0x0BA1, 0x0BA4, 0x0BA2, 0x0BAF, 0x0BA6, 0x0BA0, 0x0BAE,
    0x0BB7, 0x0BA7, 0x0BA4, 0x0BAA, 0x0BA1, 0x0B9E, 0x0BAD, 0x0BA1,
    0x0BA7, 0x0BAC, 0x0BB2, 0x0BA7, 0x0BB7, 0x0BA5, 0x0BB3, 0x0BAA,
    0x0BAA, 0x0BA4, 0x0BA3, 0x0B9D, 0x0BB0, 0x0BA7, 0x0BA6, 0x0BA3,
    0x0BB6, 0x0BAB, 0x0BAE, 0x0BA9, 0x0BA0, 0x0BAE, 0x0BA8, 0x0BA1,
    0x0BAD, 0x0BA6, 0x0B87, 0x0B8B, 0x0BA8, 0x0B9B, 0x0BA3, 0x0BAD,
    0x0BBB, 0x0B9C, 0x0BAB, 0x0BA8, 0x0BBC, 0x0B9C, 0x0BA4, 0x0BAA,
    0x0BB0, 0x0B9F, 0x0B9C, 0x0BA0, 0x0BB0, 0x0BA7, 0x0BA3, 0x0B9A,
    0x0BB2, 0x0BAC, 0x0B8D, 0x0BA8, 0x0BB5, 0x0BA2, 0x0BAE, 0x0BA7
};

/*

// This array is the offsets for the eadf - The offset are found on track 67.0
// leaving in for future update
static const uint16_t abc_chem_protection[] = {
    0x0000, 0x0000, 0x0A6D, 0x0A6D, 0x0A69, 0x0A6D, 0x0A68, 0x0A6D,
    0x0A6A, 0x0A6B, 0x0A69, 0x0A6D, 0x0A6B, 0x0A6D, 0x0A6D, 0x0A6C,
    0x0A6D, 0x0A6D, 0x0A6A, 0x0A6D, 0x0A68, 0x0A6C, 0x0A6B, 0x0A6C,
    0x0A69, 0x0A6C, 0x0A69, 0x0A6D, 0x0A6A, 0x0A6C, 0x0A6B, 0x0A6C,
    0x0A6A, 0x0A6C, 0x0A6A, 0x0A6D, 0x0A69, 0x0A6C, 0x0A6A, 0x0A6B,
    0x0A6A, 0x0A6C, 0x0A6B, 0x0A6C, 0x0A6D, 0x0A6C, 0x0A6D, 0x0A6C,
    0x0A6C, 0x0A6B, 0x0A6D, 0x0A6D, 0x0A6A, 0x0A6C, 0x0A6C, 0x0A6C,
    0x0A6C, 0x0A6B, 0x0A6D, 0x0A6C, 0x0A6B, 0x0A6C, 0x0A6A, 0x0A6C,
    0x0A6B, 0x0A6C, 0x0A6D, 0x0A6C, 0x0A6A, 0x0A6C, 0x0A6C, 0x0A6B,
    0x0A6A, 0x0A6C, 0x0A6C, 0x0A6C, 0x0A6C, 0x0A6B, 0x0A6B, 0x0A6C,
    0x0A6B, 0x0A6C, 0x0A74, 0x0A6B, 0x0A70, 0x0A6C, 0x0A74, 0x0A6B,
    0x0A6E, 0x0A6B, 0x0A71, 0x0A6B, 0x0A6F, 0x0A6B, 0x0A6F, 0x0A6C,
    0x0A6D, 0x0A6C, 0x0A70, 0x0A6C, 0x0A6F, 0x0A6B, 0x0A6F, 0x0A6B,
    0x0A71, 0x0A6B, 0x0A71, 0x0A6B, 0x0A75, 0x0A6C, 0x0A74, 0x0A6B,
    0x0A73, 0x0A6C, 0x0A73, 0x0A6C, 0x0A71, 0x0A6D, 0x0A71, 0x0A6B,
    0x0A70, 0x0A69, 0x0A6D, 0x0A69, 0x0A6E, 0x0A6A, 0x0A70, 0x0A6A,
    0x0A6F, 0x0A69, 0x0A6F, 0x0A6B, 0x0A6F, 0x0A6A, 0x0A6F, 0x0A6A,
    0x0A6F, 0x0A69, 0x0A6F, 0x0A6A, 0x0A71, 0x0A6A, 0x0A72, 0x0A6A,
    0x0A71, 0x0A6A, 0x0A6E, 0x0A69, 0x0A6F, 0x0A6A, 0x0A6B, 0x0A6A,
    0x0A6A, 0x0A6A, 0x0A6F, 0x0A6A, 0x0A6E, 0x0A6A, 0x0A70, 0x0A69
};
*/

static const uint16_t inferior_protection[] = {
    0x0A32, 0x0A34, 0x0A30, 0x0A31, 0x0A2E, 0x0A36, 0x0A2D, 0x0A33,
    0x0A25, 0x0A34, 0x0A2D, 0x0A33, 0x0A2F, 0x0A32, 0x0A32, 0x0A32,
    0x0A35, 0x0A32, 0x0A31, 0x0A32, 0x0A2F, 0x0A34, 0x0A2D, 0x0A30,
    0x0A2E, 0x0A33, 0x0A2B, 0x0A31, 0x0A2F, 0x0A33, 0x0A2E, 0x0A33,
    0x0A2F, 0x0A33, 0x0A2E, 0x0A32, 0x0A2E, 0x0A31, 0x0A2E, 0x0A32,
    0x0A2D, 0x0A33, 0x0A2E, 0x0A34, 0x0A2D, 0x0A34, 0x0A2D, 0x0A31,
    0x0A2F, 0x0A31, 0x0A2F, 0x0A30, 0x0A2F, 0x0A32, 0x0A30, 0x0A31,
    0x0A2D, 0x0A34, 0x0A30, 0x0A31, 0x0A2E, 0x0A32, 0x0A2B, 0x0A30,
    0x0A2E, 0x0A32, 0x0A2E, 0x0A31, 0x0A30, 0x0A33, 0x0A2D, 0x0A32,
    0x0A2E, 0x0A32, 0x0A2E, 0x0A32, 0x0A2C, 0x0A33, 0x0A2F, 0x0A31,
    0x0A2E, 0x0A34, 0x0A2D, 0x0A33, 0x0A2E, 0x0A32, 0x0A2D, 0x0A31,
    0x0A31, 0x0A31, 0x0A30, 0x0A32, 0x0A2F, 0x0A31, 0x0A31, 0x0A32,
    0x0A2F, 0x0A32, 0x0A34, 0x0A2E, 0x0A30, 0x0A32, 0x0A2E, 0x0A31,
    0x0A2F, 0x0A33, 0x0A2F, 0x0A33, 0x0A30, 0x0A33, 0x0A2D, 0x0A34,
    0x0A2F, 0x0A31, 0x0A2E, 0x0A32, 0x0A31, 0x0A32, 0x0A2F, 0x0A32,
    0x0A30, 0x0A34, 0x0A2D, 0x0A31, 0x0A2E, 0x0A32, 0x0A2E, 0x0A30,
    0x0A2F, 0x0A30, 0x0A30, 0x0A31, 0x0A2D, 0x0A30, 0x0A32, 0x0A31,
    0x0A2D, 0x0A32, 0x0A2E, 0x0A32, 0x0A2B, 0x0A33, 0x0A2C, 0x0A33,
    0x0A2B, 0x0A34, 0x0A2A, 0x0A34, 0x0A2E, 0x0A31, 0x0A2C, 0x0A31,
    0x0A28, 0x0A33, 0x0A2A, 0x0A32, 0x0A26, 0x0A35, 0x0A29, 0x0000
};


/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
