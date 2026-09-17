#include <string.h>

#include "py/runtime.h"
#include "py/stream.h"
#include "py/mperrno.h"
#include "py/reader.h"
#include "qosa_virtual_file.h"
#include "qpy_path.h"

typedef struct _qpy_file_obj_t {
    mp_obj_base_t base;
    qosa_int32_t fd;
} qpy_file_obj_t;

static const mp_obj_type_t qpy_type_fileio;
static const mp_obj_type_t qpy_type_textio;

static int qpy_errno(void) {
    return MP_EIO;
}

static void qpy_file_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    qpy_file_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<io.%s %d>", mp_obj_get_type_str(self_in), (int)self->fd);
}

static mp_uint_t qpy_file_read(mp_obj_t self_in, void *buf, mp_uint_t size, int *errcode) {
    qpy_file_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->fd < 0) {
        *errcode = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    qosa_ssize_t ret = qosa_vfs_read(self->fd, buf, size);
    if (ret < 0) {
        *errcode = qpy_errno();
        return MP_STREAM_ERROR;
    }
    return ret;
}

static mp_uint_t qpy_file_write(mp_obj_t self_in, const void *buf, mp_uint_t size, int *errcode) {
    qpy_file_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->fd < 0) {
        *errcode = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    qosa_ssize_t ret = qosa_vfs_write(self->fd, buf, size);
    if (ret < 0) {
        *errcode = qpy_errno();
        return MP_STREAM_ERROR;
    }
    return ret;
}

static mp_uint_t qpy_file_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    qpy_file_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->fd < 0 && request != MP_STREAM_CLOSE) {
        *errcode = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    switch (request) {
        case MP_STREAM_FLUSH:
            if (qosa_vfs_fsync(self->fd) != 0) {
                *errcode = qpy_errno();
                return MP_STREAM_ERROR;
            }
            return 0;
        case MP_STREAM_CLOSE:
            if (self->fd >= 0) {
                qosa_vfs_close(self->fd);
                self->fd = -1;
            }
            return 0;
        case MP_STREAM_SEEK: {
            struct mp_stream_seek_t *s = (struct mp_stream_seek_t *)arg;
            int whence = s->whence == MP_SEEK_SET ? QOSA_VFS_SEEK_SET : (s->whence == MP_SEEK_CUR ? QOSA_VFS_SEEK_CUR : QOSA_VFS_SEEK_END);
            qosa_int64_t pos = qosa_vfs_lseek(self->fd, s->offset, whence);
            if (pos < 0) {
                *errcode = qpy_errno();
                return MP_STREAM_ERROR;
            }
            s->offset = pos;
            return 0;
        }
        default:
            *errcode = MP_EINVAL;
            return MP_STREAM_ERROR;
    }
}

static const mp_rom_map_elem_t qpy_file_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_readlines), MP_ROM_PTR(&mp_stream_unbuffered_readlines_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_seek), MP_ROM_PTR(&mp_stream_seek_obj) },
    { MP_ROM_QSTR(MP_QSTR_tell), MP_ROM_PTR(&mp_stream_tell_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&mp_stream_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&mp_stream___exit___obj) },
};
static MP_DEFINE_CONST_DICT(qpy_file_locals_dict, qpy_file_locals_table);

static const mp_stream_p_t qpy_fileio_stream_p = {
    .read = qpy_file_read,
    .write = qpy_file_write,
    .ioctl = qpy_file_ioctl,
};

static MP_DEFINE_CONST_OBJ_TYPE(
    qpy_type_fileio,
    MP_QSTR_FileIO,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, qpy_file_print,
    protocol, &qpy_fileio_stream_p,
    locals_dict, &qpy_file_locals_dict
    );

static const mp_stream_p_t qpy_textio_stream_p = {
    .read = qpy_file_read,
    .write = qpy_file_write,
    .ioctl = qpy_file_ioctl,
    .is_text = true,
};

static MP_DEFINE_CONST_OBJ_TYPE(
    qpy_type_textio,
    MP_QSTR_TextIOWrapper,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, qpy_file_print,
    protocol, &qpy_textio_stream_p,
    locals_dict, &qpy_file_locals_dict
    );

static mp_obj_t qpy_builtin_open(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    (void)kw_args;
    const char *path = mp_obj_str_get_str(pos_args[0]);
    char native_path[256];
    const char *mode = n_args > 1 ? mp_obj_str_get_str(pos_args[1]) : "r";
    const mp_obj_type_t *type = &qpy_type_textio;
    int flags = QOSA_VFS_O_RDONLY;

    for (const char *p = mode; *p != '\0'; ++p) {
        switch (*p) {
            case 'r':
                flags = QOSA_VFS_O_RDONLY;
                break;
            case 'w':
                flags = QOSA_VFS_O_WRONLY | QOSA_VFS_O_CREAT | QOSA_VFS_O_TRUNC;
                break;
            case 'a':
                flags = QOSA_VFS_O_WRONLY | QOSA_VFS_O_CREAT | QOSA_VFS_O_APPEND;
                break;
            case '+':
                flags = (flags & ~(QOSA_VFS_O_RDONLY | QOSA_VFS_O_WRONLY)) | QOSA_VFS_O_RDWR;
                break;
            case 'b':
                type = &qpy_type_fileio;
                break;
            case 't':
                type = &qpy_type_textio;
                break;
        }
    }

    const char *open_path = qpy_path_to_native(path, native_path, sizeof(native_path));
    if (open_path == QOSA_NULL) {
        mp_raise_OSError_with_filename(MP_EINVAL, path);
    }

    qosa_int32_t fd = qosa_vfs_open(open_path, flags);
    if (fd < 0) {
        mp_raise_OSError_with_filename(qpy_errno(), path);
    }

    qpy_file_obj_t *o = mp_obj_malloc_with_finaliser(qpy_file_obj_t, type);
    o->fd = fd;
    return MP_OBJ_FROM_PTR(o);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, qpy_builtin_open);

typedef struct _qpy_reader_file_t {
    qosa_int32_t fd;
    size_t len;
    size_t pos;
    byte buf[32];
} qpy_reader_file_t;

static mp_uint_t qpy_reader_file_readbyte(void *data) {
    qpy_reader_file_t *reader = (qpy_reader_file_t *)data;
    if (reader->pos >= reader->len) {
        qosa_ssize_t n = qosa_vfs_read(reader->fd, reader->buf, sizeof(reader->buf));
        if (n <= 0) {
            reader->len = 0;
            return MP_READER_EOF;
        }
        reader->len = n;
        reader->pos = 0;
    }
    return reader->buf[reader->pos++];
}

static void qpy_reader_file_close(void *data) {
    qpy_reader_file_t *reader = (qpy_reader_file_t *)data;
    if (reader->fd >= 0) {
        qosa_vfs_close(reader->fd);
    }
    m_del_obj(qpy_reader_file_t, reader);
}

void mp_reader_new_file(mp_reader_t *reader, qstr filename) {
    const char *path = qstr_str(filename);
    char native_path[256];
    const char *open_path = qpy_path_to_native(path, native_path, sizeof(native_path));
    if (open_path == QOSA_NULL) {
        mp_raise_OSError_with_filename(MP_ENOENT, path);
    }

    qosa_int32_t fd = qosa_vfs_open(open_path, QOSA_VFS_O_RDONLY);
    if (fd < 0) {
        mp_raise_OSError_with_filename(MP_ENOENT, path);
    }

    qpy_reader_file_t *rf = m_new_obj(qpy_reader_file_t);
    rf->fd = fd;
    rf->len = 0;
    rf->pos = 0;
    reader->data = rf;
    reader->readbyte = qpy_reader_file_readbyte;
    reader->close = qpy_reader_file_close;
}
