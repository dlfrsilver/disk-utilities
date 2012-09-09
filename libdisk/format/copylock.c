/*
 * disk/copylock.c
 * 
 * Rob Northen CopyLock protection track (Amiga).
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  518 decoded bytes per sector (excluding sector gap)
 *  Inter-sector gap of ~44 decoded zero bytes (44 MFM words).
 * Decoded Sector:
 *  u8 0xa0+index,0,0 :: First byte is MFM-illegal for TRKTYP_copylock_old
 *  <sync word>   :: Per-sector sync marker, see code for list
 *  u8 index      :: 0-11, must correspond to appropriate sync marker
 *  u8 data[512]
 *  u8 0
 * Data Bytes:
 *  Generated by a 23-bit LFSR, with taps at positions 1 and 23.
 *  The data bytes are an arbitrary fixed 8-bit window on the LFSR state.
 *  The generated byte stream carries across sector boundaries.
 * Sector 6:
 *  First 16 bytes interrupt random stream with signature "Rob Northen Comp"
 *  The LFSR-generated data then continues uninterrupted at 17th byte.
 *  (NB. Old-style Copylock with sifferent sync marks does interrupt the
 *       LFSR stream for the "Rob Northen Comp" signature").
 * MFM encoding:
 *  In place, no even/odd split.
 * Timings:
 *  Sync 0x8912 is 5% faster; Sync 0x8914 is 5% slower. All other bit cells
 *  are 2us, and total track length is exactly as usual (the short sector
 *  precisely balances the long sector). Speed changes occur at the start of
 *  the preceding sector's gap, presumably to allow the disk controller to
 *  lock on.
 * 
 * TRKTYP_copylock data layout:
 *  u32 lfsr_seed; (Only lfsr_seed[22:0] is used and non-zero)
 *  First data byte of sector 0 is lfsr_seed[22:15].
 */

#include <libdisk/util.h>
#include "../private.h"

static const uint16_t sync_list[] = {
    0x8a91, 0x8a44, 0x8a45, 0x8a51, 0x8912, 0x8911,
    0x8914, 0x8915, 0x8944, 0x8945, 0x8951 };
static const uint8_t sec6_sig[] = {
    0x52, 0x6f, 0x62, 0x20, 0x4e, 0x6f, 0x72, 0x74, /* "Rob Northen Comp" */
    0x68, 0x65, 0x6e, 0x20, 0x43, 0x6f, 0x6d, 0x70 };

static uint32_t lfsr_prev_state(uint32_t x)
{
    return (x >> 1) | ((((x >> 1) ^ x) & 1) << 22);
}

static uint32_t lfsr_next_state(uint32_t x)
{
    return ((x << 1) & ((1u << 23) - 1)) | (((x >> 22) ^ x) & 1);
}

static uint8_t lfsr_state_byte(uint32_t x)
{
    return (uint8_t)(x >> 15);
}

/* Take LFSR state from start of one sector, to another. */
static uint32_t lfsr_seek(
    struct track_info *ti, uint32_t x, unsigned int from, unsigned int to)
{
    unsigned int sz;

    while (from != to) {
        if (from > to)
            from--;
        sz = 512;
        if (from == 6)
            sz -= sizeof(sec6_sig);
        if ((ti->type == TRKTYP_copylock_old) && (from == 5))
            sz += sizeof(sec6_sig);
        while (sz--)
            x = (from < to) ? lfsr_next_state(x) : lfsr_prev_state(x);
        if (from < to)
            from++;
    }

    return x;
}

static void *copylock_write_raw(
    struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t *block, lfsr_seed = 0, latency[11], nr_valid_blocks = 0;
    unsigned int i, sec, least_block = ~0u;

    while ((stream_next_bit(s) != -1) &&
           (nr_valid_blocks != ti->nr_sectors)) {

        uint8_t dat[2*512];
        uint32_t lfsr, lfsr_sec, idx_off = s->index_offset - 15;

        /* Are we at the start of a sector we have not yet analysed? */
        if (ti->type == TRKTYP_copylock) {
            for (sec = 0; sec < ti->nr_sectors; sec++)
                if ((uint16_t)s->word == sync_list[sec])
                    break;
        } else /* TRKTYP_copylock_old */ {
            if (((uint16_t)s->word & 0xff00u) != 0x6500u)
                continue;
            sec = mfm_decode_bits(MFM_all, s->word) & 0xfu;
            if (s->word != (mfm_encode_word(0xb0+sec) | (1u<<13)))
                continue;
        }
        if ((sec >= ti->nr_sectors) || is_valid_sector(ti, sec))
            continue;

        /* Check the sector header. */
        if (stream_next_bits(s, 16) == -1)
            break;
        if (mfm_decode_bits(MFM_all, (uint16_t)s->word) != sec)
            continue;

        /* Read and decode the sector data. */
        s->latency = 0;
        if (stream_next_bytes(s, dat, sizeof(dat)) == -1)
            break;
        mfm_decode_bytes(MFM_all, sizeof(dat)/2, dat, dat);

        /* Deal with sector 6 preamble. */
        i = 0;
        if (sec == 6) {
            if (memcmp(dat, sec6_sig, sizeof(sec6_sig)))
                continue;
            i = sizeof(sec6_sig);
        }

        /*
         * Get the LFSR start value for this sector. If we know the track LFSR
         * seed then we work it out from that, else we get it from sector data.
         */
        lfsr = lfsr_sec =
            (lfsr_seed != 0)
            ? lfsr_seek(ti, lfsr_seed, 0, sec)
            : (dat[i] << 15) | (dat[i+8] << 7) | (dat[i+16] >> 1);

        /* Check that the data matches the LFSR-generated stream. */
        for (; i < 512; i++) {
            if (dat[i] != lfsr_state_byte(lfsr))
                break;
            lfsr = lfsr_next_state(lfsr);
        }
        if (i != 512)
            continue;

        /* All good. Finally, stash the LFSR seed if we didn't know it. */
        if (lfsr_seed == 0) {
            lfsr_seed = lfsr_seek(ti, lfsr_sec, sec, 0);
            /* Paranoia: Reject the degenerate case of endless zero bytes. */
            if (lfsr_seed == 0)
                continue;
        }

        /* Good sector: remember its details. */
        latency[sec] = s->latency;
        set_sector_valid(ti, sec);
        nr_valid_blocks++;
        if (least_block > sec) {
            ti->data_bitoff = idx_off;
            least_block = sec;
        }
    }

    if (nr_valid_blocks == 0)
        return NULL;

    /* Check validity of the non-uniform track timings. */
    if (!is_valid_sector(ti, 5))
        latency[5] = 514*8*2*2000; /* bodge it */
    for (sec = 0; sec < ARRAY_SIZE(latency); sec++) {
        float d = (100.0 * ((int)latency[sec] - (int)latency[5]))
            / (int)latency[5];
        if (!is_valid_sector(ti, sec))
            continue;
        switch (sec) {
        case 4:
            if (d > -4.0)
                trk_warn(ti, tracknr, "Short sector is only "
                         "%.2f%% different", d);
            break;
        case 6:
            if (d < 4.0)
                trk_warn(ti, tracknr, "Long sector is only "
                         "%.2f%% different", d);
            break;
        default:
            if ((d < -2.0) || (d > 2.0))
                trk_warn(ti, tracknr, "Normal sector is "
                         "%.2f%% different", d);
            break;
        }
    }

    /* Adjust track offset for any missing initial sectors. */
    for (sec = 0; sec < ti->nr_sectors; sec++)
        if (is_valid_sector(ti, sec))
            break;
    ti->data_bitoff -= sec * (514+48)*8*2;

    /* Adjust track offset for first sector's sync-mark offset. */
    ti->data_bitoff -= 3*8*2;

    /* Magic: We can reconstruct the entire track from the LFSR seed! */
    if (nr_valid_blocks != ti->nr_sectors) {
        trk_warn(ti, tracknr, "Reconstructed damaged track (%u)",
                 nr_valid_blocks);
        set_all_sectors_valid(ti);
    }

    ti->len = 4;
    block = memalloc(ti->len);
    *block = htobe32(lfsr_seed);
    return block;
}

static void copylock_read_raw(
    struct disk *d, unsigned int tracknr, struct track_buffer *tbuf)
{
    struct track_info *ti = &d->di->track[tracknr];
    uint32_t lfsr, lfsr_seed = be32toh(*(uint32_t *)ti->dat);
    unsigned int i, sec = 0;
    uint16_t speed = SPEED_AVG;

    tbuf_disable_auto_sector_split(tbuf);

    while (sec < ti->nr_sectors) {
        /* Header */
        if (ti->type == TRKTYP_copylock) {
            tbuf_bits(tbuf, speed, MFM_all, 8, 0xa0 + sec);
            tbuf_bits(tbuf, speed, MFM_all, 16, 0);
            tbuf_bits(tbuf, speed, MFM_raw, 16, sync_list[sec]);
        } else /* TRKTYP_copylock_old */ {
            tbuf_bits(tbuf, speed, MFM_raw, 16,
                      mfm_encode_word(0xa0 + sec) | (1u<<13));
            tbuf_bits(tbuf, speed, MFM_all, 16, 0);
            tbuf_bits(tbuf, speed, MFM_raw, 16,
                      mfm_encode_word(0xb0 + sec) | (1u<<13));
        }
        tbuf_bits(tbuf, speed, MFM_all, 8, sec);
        /* Data */
        lfsr = lfsr_seek(ti, lfsr_seed, 0, sec);
        for (i = 0; i < 512; i++) {
            if ((sec == 6) && (i == 0))
                for (i = 0; i < sizeof(sec6_sig); i++)
                    tbuf_bits(tbuf, speed, MFM_all, 8, sec6_sig[i]);
            tbuf_bits(tbuf, speed, MFM_all, 8, lfsr_state_byte(lfsr));
            lfsr = lfsr_next_state(lfsr);
        }
        /* Footer */
        tbuf_bits(tbuf, speed, MFM_all, 8, 0);

        /* Move to next sector's speed to encode track gap. */
        sec++;
        speed = (sec == 4 ? (SPEED_AVG * 95) / 100 :
                 sec == 6 ? (SPEED_AVG * 105) / 100 :
                 SPEED_AVG);
        tbuf_gap(tbuf, speed, 44*8);
    }
}

struct track_handler copylock_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = copylock_write_raw,
    .read_raw = copylock_read_raw
};

struct track_handler copylock_old_handler = {
    .bytes_per_sector = 512,
    .nr_sectors = 11,
    .write_raw = copylock_write_raw,
    .read_raw = copylock_read_raw
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
