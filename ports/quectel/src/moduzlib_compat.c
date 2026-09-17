// Legacy QuecPython uzlib compatibility layer.  The public surface mirrors
// the EG800Z firmware: decompress(data[, wbits[, _]]) and DecompIO(stream
// [, wbits[, use_internal_buf]]).
#include <string.h>

#include "py/runtime.h"
#include "py/stream.h"
#include "py/mperrno.h"

#include "uzlib_legacy/tinf.h"

typedef struct _mp_obj_decompio_t {
    mp_obj_base_t base;
    mp_obj_t src_stream;
    TINF_DATA decomp;
    bool eof;
} mp_obj_decompio_t;

static int qpy_uzlib_read_source(TINF_DATA *data) {
    byte *p = (byte *)data - offsetof(mp_obj_decompio_t, decomp);
    mp_obj_decompio_t *self = (mp_obj_decompio_t *)p;
    const mp_stream_p_t *stream = mp_get_stream(self->src_stream);
    int err;
    byte c;
    mp_uint_t out_sz = stream->read(self->src_stream, &c, 1, &err);
    if (out_sz == MP_STREAM_ERROR) {
        mp_raise_OSError(err);
    }
    if (out_sz == 0) {
        mp_raise_type(&mp_type_EOFError);
    }
    return c;
}

static mp_obj_t qpy_uzlib_decompio_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 3, false);
    mp_get_stream_raise(args[0], MP_STREAM_OP_READ);
    if (n_args > 2) {
        int use_internal_buf = mp_obj_get_int(args[2]);
        if (use_internal_buf != 0 && use_internal_buf != 1) {
            mp_raise_ValueError(MP_ERROR_TEXT("Invalid parameter"));
        }
    }

    mp_obj_decompio_t *self = mp_obj_malloc(mp_obj_decompio_t, type);
    memset(&self->decomp, 0, sizeof(self->decomp));
    self->decomp.readSource = qpy_uzlib_read_source;
    self->src_stream = args[0];
    self->eof = false;

    mp_int_t dict_opt = n_args > 1 ? mp_obj_get_int(args[1]) : 0;
    int dict_sz;
    if (dict_opt >= 16) {
        if (uzlib_gzip_parse_header(&self->decomp) != TINF_OK) {
            mp_raise_ValueError(MP_ERROR_TEXT("compression header"));
        }
        dict_sz = 1 << (dict_opt - 16);
    } else if (dict_opt >= 0) {
        dict_opt = uzlib_zlib_parse_header(&self->decomp);
        if (dict_opt < 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("compression header"));
        }
        dict_sz = 1 << dict_opt;
    } else {
        dict_sz = 1 << -dict_opt;
    }
    uzlib_uncompress_init(&self->decomp, m_new(byte, dict_sz), dict_sz);
    return MP_OBJ_FROM_PTR(self);
}

static mp_uint_t qpy_uzlib_decompio_read(mp_obj_t self_in, void *buf, mp_uint_t size, int *errcode) {
    mp_obj_decompio_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->eof) {
        return 0;
    }
    self->decomp.dest = buf;
    self->decomp.dest_limit = (byte *)buf + size;
    int st = uzlib_uncompress_chksum(&self->decomp);
    if (st == TINF_DONE) {
        self->eof = true;
    }
    if (st < 0) {
        *errcode = MP_EINVAL;
        return MP_STREAM_ERROR;
    }
    return self->decomp.dest - (byte *)buf;
}

static const mp_stream_p_t qpy_uzlib_decompio_stream_p = {
    .read = qpy_uzlib_decompio_read,
};

static const mp_rom_map_elem_t qpy_uzlib_decompio_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_uzlib_decompio_locals, qpy_uzlib_decompio_locals_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    qpy_uzlib_decompio_type,
    MP_QSTR_DecompIO,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    make_new, qpy_uzlib_decompio_make_new,
    protocol, &qpy_uzlib_decompio_stream_p,
    locals_dict, &qpy_uzlib_decompio_locals
    );

static mp_obj_t qpy_uzlib_decompress(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
    TINF_DATA *decomp = m_new_obj(TINF_DATA);
    memset(decomp, 0, sizeof(*decomp));
    uzlib_uncompress_init(decomp, NULL, 0);
    mp_uint_t dest_buf_size = (bufinfo.len + 15) & ~15;
    byte *dest_buf = m_new(byte, dest_buf_size);
    decomp->dest = dest_buf;
    decomp->dest_limit = dest_buf + dest_buf_size;
    decomp->source = bufinfo.buf;
    decomp->source_limit = (byte *)bufinfo.buf + bufinfo.len;

    bool is_zlib = n_args <= 1 || mp_obj_get_int(args[1]) >= 0;
    if (is_zlib && uzlib_zlib_parse_header(decomp) < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("compression header"));
    }
    for (;;) {
        int st = uzlib_uncompress_chksum(decomp);
        if (st < 0) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d"), st);
        }
        if (st == TINF_DONE) {
            break;
        }
        size_t offset = decomp->dest - dest_buf;
        dest_buf = m_renew(byte, dest_buf, dest_buf_size, dest_buf_size + 256);
        dest_buf_size += 256;
        decomp->dest = dest_buf + offset;
        decomp->dest_limit = decomp->dest + 256;
    }
    size_t final_sz = decomp->dest - dest_buf;
    dest_buf = m_renew(byte, dest_buf, dest_buf_size, final_sz);
    return mp_obj_new_bytearray_by_ref(final_sz, dest_buf);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_uzlib_decompress_obj, 1, 3, qpy_uzlib_decompress);

static const mp_rom_map_elem_t qpy_uzlib_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_uzlib) },
    { MP_ROM_QSTR(MP_QSTR_decompress), MP_ROM_PTR(&qpy_uzlib_decompress_obj) },
    { MP_ROM_QSTR(MP_QSTR_DecompIO), MP_ROM_PTR(&qpy_uzlib_decompio_type) },
};
static MP_DEFINE_CONST_DICT(qpy_uzlib_globals, qpy_uzlib_globals_table);

const mp_obj_module_t mp_module_uzlib = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&qpy_uzlib_globals,
};
MP_REGISTER_MODULE(MP_QSTR_uzlib, mp_module_uzlib);

#include "uzlib_legacy/tinflate.c"
#include "uzlib_legacy/tinfzlib.c"
#include "uzlib_legacy/tinfgzip.c"
#include "uzlib_legacy/adler32.c"
#include "uzlib_legacy/crc32.c"
