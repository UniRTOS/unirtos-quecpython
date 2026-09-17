#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lfs.h"
#include "qosa_built_in_flash.h"
#include "qosa_sys.h"
#include "qosa_virtual_file.h"
#include "qosa_virtual_file_ops.h"
#include "quec_mem_map_718pm.h"
/* Compile-time guard: CUST layout must match the packaged customer_app2 image. */
#include "qpy_partition_layout.h"
#include "qpy_usrfs.h"

#define QPY_USRFS_MOUNT_POINT "/usr"
#define QPY_USRFS_READ_SIZE (256U)
#define QPY_USRFS_PROG_SIZE (256U)
#define QPY_USRFS_BLOCK_SIZE (4096U)
#define QPY_USRFS_CACHE_SIZE (256U)
#define QPY_USRFS_LOOKAHEAD_SIZE (16U)
#define QPY_USRFS_MAX_FILES (16)

typedef struct {
    int used;
    lfs_file_t file;
} qpy_usrfs_file_t;

typedef struct {
    QOSA_VFS_DIR base;
    lfs_dir_t dir;
    struct qosa_vfs_dirent_t entry;
} qpy_usrfs_dir_t;

static lfs_t qpy_usrfs_lfs;
static struct lfs_config qpy_usrfs_config;
static qosa_mutex_t qpy_usrfs_mutex = QOSA_NULL;
static qpy_usrfs_file_t qpy_usrfs_files[QPY_USRFS_MAX_FILES];
static uint8_t qpy_usrfs_read_buffer[QPY_USRFS_CACHE_SIZE] __attribute__((aligned(8)));
static uint8_t qpy_usrfs_prog_buffer[QPY_USRFS_CACHE_SIZE] __attribute__((aligned(8)));
static uint8_t qpy_usrfs_lookahead_buffer[QPY_USRFS_LOOKAHEAD_SIZE] __attribute__((aligned(8)));
static int qpy_usrfs_mounted;

static const char *qpy_usrfs_path(const char *path) {
    return path == QOSA_NULL || path[0] == '\0' ? "/" : path;
}

static int qpy_usrfs_range(lfs_block_t block, lfs_off_t off, lfs_size_t size, uint32_t *address) {
    uint64_t offset = (uint64_t)block * QPY_USRFS_BLOCK_SIZE + off;
    uint64_t end = offset + size;
    if (block >= qpy_usrfs_config.block_count || end < offset || end > UNIRTOS_CUST_FLASH_SIZE) {
        return LFS_ERR_INVAL;
    }
    *address = (uint32_t)(UNIRTOS_CUST_FLASH_ADDR + offset);
    return 0;
}

static int qpy_usrfs_read(const struct lfs_config *cfg, lfs_block_t block, lfs_off_t off,
    void *buffer, lfs_size_t size) {
    (void)cfg;
    uint32_t address;
    if (qpy_usrfs_range(block, off, size, &address) != 0) {
        return LFS_ERR_INVAL;
    }
    return qosa_builtin_flash_read(address, buffer, size) == QOSA_BUILT_NOR_SUCESS ? 0 : LFS_ERR_IO;
}

static int qpy_usrfs_prog(const struct lfs_config *cfg, lfs_block_t block, lfs_off_t off,
    const void *buffer, lfs_size_t size) {
    (void)cfg;
    uint32_t address;
    if (qpy_usrfs_range(block, off, size, &address) != 0) {
        return LFS_ERR_INVAL;
    }
    return qosa_builtin_flash_write(address, (qosa_uint8_t *)buffer, size) == QOSA_BUILT_NOR_SUCESS
        ? 0 : LFS_ERR_IO;
}

static int qpy_usrfs_erase(const struct lfs_config *cfg, lfs_block_t block) {
    (void)cfg;
    uint32_t address;
    if (qpy_usrfs_range(block, 0, QPY_USRFS_BLOCK_SIZE, &address) != 0) {
        return LFS_ERR_INVAL;
    }
    return qosa_builtin_flash_erase(address, QPY_USRFS_BLOCK_SIZE) == QOSA_BUILT_NOR_SUCESS
        ? 0 : LFS_ERR_IO;
}

static int qpy_usrfs_sync(const struct lfs_config *cfg) {
    (void)cfg;
    return 0;
}

static void qpy_usrfs_lock(void) {
    qosa_mutex_lock(qpy_usrfs_mutex, QOSA_WAIT_FOREVER);
}

static void qpy_usrfs_unlock(void) {
    qosa_mutex_unlock(qpy_usrfs_mutex);
}

static int qpy_usrfs_mode(int flags) {
    int mode;
    if ((flags & QOSA_VFS_O_RDWR) == QOSA_VFS_O_RDWR) {
        mode = LFS_O_RDWR;
    } else if ((flags & QOSA_VFS_O_WRONLY) == QOSA_VFS_O_WRONLY) {
        mode = LFS_O_WRONLY;
    } else {
        mode = LFS_O_RDONLY;
    }
    if (flags & QOSA_VFS_O_CREAT) {
        mode |= LFS_O_CREAT;
    }
    if (flags & QOSA_VFS_O_EXCL) {
        mode |= LFS_O_EXCL;
    }
    if (flags & QOSA_VFS_O_TRUNC) {
        mode |= LFS_O_TRUNC;
    }
    if (flags & QOSA_VFS_O_APPEND) {
        mode |= LFS_O_APPEND;
    }
    return mode;
}

static qpy_usrfs_file_t *qpy_usrfs_file(int fd) {
    int index = fd - 3;
    if (index < 0 || index >= QPY_USRFS_MAX_FILES || !qpy_usrfs_files[index].used) {
        return QOSA_NULL;
    }
    return &qpy_usrfs_files[index];
}

static int qpy_usrfs_umount(void *ctx) {
    (void)ctx;
    qpy_usrfs_lock();
    for (int i = 0; i < QPY_USRFS_MAX_FILES; ++i) {
        if (qpy_usrfs_files[i].used) {
            qpy_usrfs_unlock();
            return -EBUSY;
        }
    }
    int rc = lfs_unmount(&qpy_usrfs_lfs);
    if (rc == 0) {
        qpy_usrfs_mounted = 0;
    }
    qpy_usrfs_unlock();
    return rc;
}

static int qpy_usrfs_remount(void *ctx, unsigned flags) {
    (void)ctx;
    return flags == 0 ? 0 : -EROFS;
}

static int qpy_usrfs_open(void *ctx, const char *path, int flags, int mode) {
    (void)ctx;
    (void)mode;
    qpy_usrfs_lock();
    int index;
    for (index = 0; index < QPY_USRFS_MAX_FILES; ++index) {
        if (!qpy_usrfs_files[index].used) {
            break;
        }
    }
    if (index == QPY_USRFS_MAX_FILES) {
        qpy_usrfs_unlock();
        return -EMFILE;
    }
    memset(&qpy_usrfs_files[index].file, 0, sizeof(lfs_file_t));
    int rc = lfs_file_open(&qpy_usrfs_lfs, &qpy_usrfs_files[index].file,
        qpy_usrfs_path(path), qpy_usrfs_mode(flags));
    if (rc == 0) {
        qpy_usrfs_files[index].used = 1;
        rc = index + 3;
    }
    qpy_usrfs_unlock();
    return rc;
}

static int qpy_usrfs_close(void *ctx, int fd) {
    (void)ctx;
    qpy_usrfs_lock();
    qpy_usrfs_file_t *slot = qpy_usrfs_file(fd);
    if (slot == QOSA_NULL) {
        qpy_usrfs_unlock();
        return -EBADF;
    }
    int rc = lfs_file_close(&qpy_usrfs_lfs, &slot->file);
    memset(slot, 0, sizeof(*slot));
    qpy_usrfs_unlock();
    return rc;
}

static qosa_ssize_t qpy_usrfs_vfs_read(void *ctx, int fd, void *dst, qosa_size_t size) {
    (void)ctx;
    if (dst == QOSA_NULL && size != 0) {
        return -EINVAL;
    }
    qpy_usrfs_lock();
    qpy_usrfs_file_t *slot = qpy_usrfs_file(fd);
    lfs_ssize_t rc = slot == QOSA_NULL ? LFS_ERR_BADF
        : lfs_file_read(&qpy_usrfs_lfs, &slot->file, dst, size);
    qpy_usrfs_unlock();
    return rc;
}

static qosa_ssize_t qpy_usrfs_vfs_write(void *ctx, int fd, const void *data, qosa_size_t size) {
    (void)ctx;
    if (data == QOSA_NULL && size != 0) {
        return -EINVAL;
    }
    qpy_usrfs_lock();
    qpy_usrfs_file_t *slot = qpy_usrfs_file(fd);
    lfs_ssize_t rc = slot == QOSA_NULL ? LFS_ERR_BADF
        : lfs_file_write(&qpy_usrfs_lfs, &slot->file, data, size);
    qpy_usrfs_unlock();
    return rc;
}

static long qpy_usrfs_lseek(void *ctx, int fd, long offset, int mode) {
    (void)ctx;
    qpy_usrfs_lock();
    qpy_usrfs_file_t *slot = qpy_usrfs_file(fd);
    lfs_soff_t rc = slot == QOSA_NULL ? LFS_ERR_BADF
        : lfs_file_seek(&qpy_usrfs_lfs, &slot->file, offset, mode);
    qpy_usrfs_unlock();
    return rc;
}

static void qpy_usrfs_fill_stat(struct qosa_vfs_stat_t *st, uint8_t type, lfs_size_t size) {
    memset(st, 0, sizeof(*st));
    st->st_size = size;
    st->st_blksize = QPY_USRFS_BLOCK_SIZE;
    st->st_blocks = (size + QPY_USRFS_BLOCK_SIZE - 1) / QPY_USRFS_BLOCK_SIZE;
    st->st_mode = QOSA_VFS_S_IRWXU | QOSA_VFS_S_IRWXG | QOSA_VFS_S_IRWXO;
    st->st_mode |= type == LFS_TYPE_DIR ? QOSA_VFS_S_IFDIR : QOSA_VFS_S_IFREG;
}

static int qpy_usrfs_stat(void *ctx, const char *path, struct qosa_vfs_stat_t *st) {
    (void)ctx;
    if (st == QOSA_NULL) {
        return -EINVAL;
    }
    qpy_usrfs_lock();
    struct lfs_info info;
    int rc = lfs_stat(&qpy_usrfs_lfs, qpy_usrfs_path(path), &info);
    if (rc == 0) {
        qpy_usrfs_fill_stat(st, info.type, info.size);
    }
    qpy_usrfs_unlock();
    return rc;
}

static int qpy_usrfs_fstat(void *ctx, int fd, struct qosa_vfs_stat_t *st) {
    (void)ctx;
    if (st == QOSA_NULL) {
        return -EINVAL;
    }
    qpy_usrfs_lock();
    qpy_usrfs_file_t *slot = qpy_usrfs_file(fd);
    lfs_soff_t size = slot == QOSA_NULL ? LFS_ERR_BADF
        : lfs_file_size(&qpy_usrfs_lfs, &slot->file);
    if (size >= 0) {
        qpy_usrfs_fill_stat(st, LFS_TYPE_REG, size);
    }
    qpy_usrfs_unlock();
    return size < 0 ? (int)size : 0;
}

static int qpy_usrfs_ftruncate(void *ctx, int fd, long length) {
    (void)ctx;
    qpy_usrfs_lock();
    if (length < 0) {
        qpy_usrfs_unlock();
        return -EINVAL;
    }
    qpy_usrfs_file_t *slot = qpy_usrfs_file(fd);
    int rc = slot == QOSA_NULL ? LFS_ERR_BADF
        : lfs_file_truncate(&qpy_usrfs_lfs, &slot->file, length);
    qpy_usrfs_unlock();
    return rc;
}

static int qpy_usrfs_truncate(void *ctx, const char *path, long length) {
    int fd = qpy_usrfs_open(ctx, path, QOSA_VFS_O_RDWR, 0);
    if (fd < 0) {
        return fd;
    }
    int rc = qpy_usrfs_ftruncate(ctx, fd, length);
    int close_rc = qpy_usrfs_close(ctx, fd);
    return rc != 0 ? rc : close_rc;
}

static int qpy_usrfs_unlink(void *ctx, const char *path) {
    (void)ctx;
    qpy_usrfs_lock();
    int rc = lfs_remove(&qpy_usrfs_lfs, qpy_usrfs_path(path));
    qpy_usrfs_unlock();
    return rc;
}

static int qpy_usrfs_rename(void *ctx, const char *src, const char *dst) {
    (void)ctx;
    qpy_usrfs_lock();
    int rc = lfs_rename(&qpy_usrfs_lfs, qpy_usrfs_path(src), qpy_usrfs_path(dst));
    qpy_usrfs_unlock();
    return rc;
}

static QOSA_VFS_DIR *qpy_usrfs_opendir(void *ctx, const char *name) {
    (void)ctx;
    qpy_usrfs_dir_t *dir = qosa_malloc(sizeof(*dir));
    if (dir == QOSA_NULL) {
        return QOSA_NULL;
    }
    memset(dir, 0, sizeof(*dir));
    qpy_usrfs_lock();
    int rc = lfs_dir_open(&qpy_usrfs_lfs, &dir->dir, qpy_usrfs_path(name));
    qpy_usrfs_unlock();
    if (rc != 0) {
        qosa_free(dir);
        return QOSA_NULL;
    }
    return &dir->base;
}

static int qpy_usrfs_readdir_r(void *ctx, QOSA_VFS_DIR *base,
    struct qosa_vfs_dirent_t *entry, struct qosa_vfs_dirent_t **result) {
    (void)ctx;
    if (base == QOSA_NULL || entry == QOSA_NULL) {
        return -EINVAL;
    }
    qpy_usrfs_dir_t *dir = (qpy_usrfs_dir_t *)base;
    struct lfs_info info;
    int rc;
    do {
        qpy_usrfs_lock();
        rc = lfs_dir_read(&qpy_usrfs_lfs, &dir->dir, &info);
        qpy_usrfs_unlock();
    } while (rc > 0 && (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0));
    if (rc <= 0) {
        if (result != QOSA_NULL) {
            *result = QOSA_NULL;
        }
        return rc < 0 ? rc : 0;
    }
    memset(entry, 0, sizeof(*entry));
    entry->d_type = info.type == LFS_TYPE_DIR ? QOSA_VFS_DT_DIR : QOSA_VFS_DT_REG;
    snprintf(entry->d_name, sizeof(entry->d_name), "%s", info.name);
    if (result != QOSA_NULL) {
        *result = entry;
    }
    return 0;
}

static struct qosa_vfs_dirent_t *qpy_usrfs_readdir(void *ctx, QOSA_VFS_DIR *base) {
    qpy_usrfs_dir_t *dir = (qpy_usrfs_dir_t *)base;
    struct qosa_vfs_dirent_t *result = QOSA_NULL;
    if (dir == QOSA_NULL || qpy_usrfs_readdir_r(ctx, base, &dir->entry, &result) != 0) {
        return QOSA_NULL;
    }
    return result;
}

static long qpy_usrfs_telldir(void *ctx, QOSA_VFS_DIR *base) {
    (void)ctx;
    if (base == QOSA_NULL) {
        return -EINVAL;
    }
    qpy_usrfs_lock();
    lfs_soff_t rc = lfs_dir_tell(&qpy_usrfs_lfs, &((qpy_usrfs_dir_t *)base)->dir);
    qpy_usrfs_unlock();
    return rc;
}

static void qpy_usrfs_seekdir(void *ctx, QOSA_VFS_DIR *base, long loc) {
    (void)ctx;
    if (base != QOSA_NULL) {
        qpy_usrfs_lock();
        (void)lfs_dir_seek(&qpy_usrfs_lfs, &((qpy_usrfs_dir_t *)base)->dir, loc);
        qpy_usrfs_unlock();
    }
}

static int qpy_usrfs_closedir(void *ctx, QOSA_VFS_DIR *base) {
    (void)ctx;
    if (base == QOSA_NULL) {
        return -EINVAL;
    }
    qpy_usrfs_dir_t *dir = (qpy_usrfs_dir_t *)base;
    qpy_usrfs_lock();
    int rc = lfs_dir_close(&qpy_usrfs_lfs, &dir->dir);
    qpy_usrfs_unlock();
    qosa_free(dir);
    return rc;
}

static int qpy_usrfs_mkdir(void *ctx, const char *name, int mode) {
    (void)ctx;
    (void)mode;
    qpy_usrfs_lock();
    int rc = lfs_mkdir(&qpy_usrfs_lfs, qpy_usrfs_path(name));
    qpy_usrfs_unlock();
    return rc;
}

static int qpy_usrfs_rmdir(void *ctx, const char *name) {
    return qpy_usrfs_unlink(ctx, name);
}

static int qpy_usrfs_fsync(void *ctx, int fd) {
    (void)ctx;
    qpy_usrfs_lock();
    qpy_usrfs_file_t *slot = qpy_usrfs_file(fd);
    int rc = slot == QOSA_NULL ? LFS_ERR_BADF : lfs_file_sync(&qpy_usrfs_lfs, &slot->file);
    qpy_usrfs_unlock();
    return rc;
}

static int qpy_usrfs_statvfs(void *ctx, const char *path, struct qosa_vfs_statvfs_t *buf) {
    (void)ctx;
    (void)path;
    if (buf == QOSA_NULL) {
        return -EINVAL;
    }
    qpy_usrfs_lock();
    lfs_ssize_t used = lfs_fs_size(&qpy_usrfs_lfs);
    qpy_usrfs_unlock();
    if (used < 0) {
        return used;
    }
    memset(buf, 0, sizeof(*buf));
    buf->f_bsize = QPY_USRFS_BLOCK_SIZE;
    buf->f_frsize = QPY_USRFS_BLOCK_SIZE;
    buf->f_blocks = qpy_usrfs_config.block_count;
    buf->f_bfree = qpy_usrfs_config.block_count > (lfs_size_t)used
        ? qpy_usrfs_config.block_count - used : 0;
    buf->f_bavail = buf->f_bfree;
    buf->f_namemax = LFS_NAME_MAX;
    return 0;
}

static int qpy_usrfs_fstatvfs(void *ctx, int fd, struct qosa_vfs_statvfs_t *buf) {
    qpy_usrfs_lock();
    int valid = qpy_usrfs_file(fd) != QOSA_NULL;
    qpy_usrfs_unlock();
    if (!valid) {
        return -EBADF;
    }
    return qpy_usrfs_statvfs(ctx, QOSA_NULL, buf);
}

static qosa_ssize_t qpy_usrfs_file_write(void *ctx, const char *path,
    const void *data, qosa_size_t size) {
    if (path == QOSA_NULL || (data == QOSA_NULL && size != 0)) {
        return -EINVAL;
    }
    int fd = qpy_usrfs_open(ctx, path, QOSA_VFS_O_WRONLY | QOSA_VFS_O_CREAT | QOSA_VFS_O_TRUNC, 0);
    if (fd < 0) {
        return fd;
    }
    qosa_ssize_t rc = qpy_usrfs_vfs_write(ctx, fd, data, size);
    int close_rc = qpy_usrfs_close(ctx, fd);
    return rc < 0 ? rc : (close_rc == 0 ? rc : close_rc);
}

static const qosa_vfs_ops_t qpy_usrfs_ops = {
    .flags = 0,
    .umount = qpy_usrfs_umount,
    .remount = qpy_usrfs_remount,
    .open = qpy_usrfs_open,
    .close = qpy_usrfs_close,
    .read = qpy_usrfs_vfs_read,
    .write = qpy_usrfs_vfs_write,
    .lseek = qpy_usrfs_lseek,
    .fstat = qpy_usrfs_fstat,
    .stat = qpy_usrfs_stat,
    .truncate = qpy_usrfs_truncate,
    .ftruncate = qpy_usrfs_ftruncate,
    .unlink = qpy_usrfs_unlink,
    .rename = qpy_usrfs_rename,
    .opendir = qpy_usrfs_opendir,
    .readdir = qpy_usrfs_readdir,
    .readdir_r = qpy_usrfs_readdir_r,
    .telldir = qpy_usrfs_telldir,
    .seekdir = qpy_usrfs_seekdir,
    .closedir = qpy_usrfs_closedir,
    .mkdir = qpy_usrfs_mkdir,
    .rmdir = qpy_usrfs_rmdir,
    .fsync = qpy_usrfs_fsync,
    .statvfs = qpy_usrfs_statvfs,
    .fstatvfs = qpy_usrfs_fstatvfs,
    .file_write = qpy_usrfs_file_write,
};

int qpy_usrfs_init(void) {
    if (qpy_usrfs_mounted) {
        return 0;
    }
    if (UNIRTOS_CUST_FLASH_SIZE == 0 ||
        (UNIRTOS_CUST_FLASH_SIZE % QPY_USRFS_BLOCK_SIZE) != 0) {
        printf("[qpy_usrfs] invalid CUST partition: addr=0x%08x size=0x%08x\r\n",
            (unsigned)UNIRTOS_CUST_FLASH_ADDR, (unsigned)UNIRTOS_CUST_FLASH_SIZE);
        return -EINVAL;
    }
    if (qpy_usrfs_mutex == QOSA_NULL && qosa_mutex_create(&qpy_usrfs_mutex) != 0) {
        printf("[qpy_usrfs] mutex creation failed\r\n");
        return -ENOMEM;
    }

    memset(&qpy_usrfs_lfs, 0, sizeof(qpy_usrfs_lfs));
    memset(&qpy_usrfs_config, 0, sizeof(qpy_usrfs_config));
    memset(qpy_usrfs_files, 0, sizeof(qpy_usrfs_files));
    qpy_usrfs_config.read = qpy_usrfs_read;
    qpy_usrfs_config.prog = qpy_usrfs_prog;
    qpy_usrfs_config.erase = qpy_usrfs_erase;
    qpy_usrfs_config.sync = qpy_usrfs_sync;
    qpy_usrfs_config.read_size = QPY_USRFS_READ_SIZE;
    qpy_usrfs_config.prog_size = QPY_USRFS_PROG_SIZE;
    qpy_usrfs_config.block_size = QPY_USRFS_BLOCK_SIZE;
    qpy_usrfs_config.block_count = UNIRTOS_CUST_FLASH_SIZE / QPY_USRFS_BLOCK_SIZE;
    qpy_usrfs_config.block_cycles = 200;
    qpy_usrfs_config.cache_size = QPY_USRFS_CACHE_SIZE;
    qpy_usrfs_config.lookahead_size = QPY_USRFS_LOOKAHEAD_SIZE;
    qpy_usrfs_config.read_buffer = qpy_usrfs_read_buffer;
    qpy_usrfs_config.prog_buffer = qpy_usrfs_prog_buffer;
    qpy_usrfs_config.lookahead_buffer = qpy_usrfs_lookahead_buffer;

    int rc = lfs_mount(&qpy_usrfs_lfs, &qpy_usrfs_config);
    if (rc != 0) {
#if CONFIG_QPY_USRFS_AUTO_FORMAT
        printf("[qpy_usrfs] mount failed (%d), formatting CUST partition\r\n", rc);
        rc = lfs_format(&qpy_usrfs_lfs, &qpy_usrfs_config);
        if (rc == 0) {
            rc = lfs_mount(&qpy_usrfs_lfs, &qpy_usrfs_config);
        }
#else
        printf("[qpy_usrfs] mount failed (%d), preserving CUST partition\r\n", rc);
        return rc;
#endif
    }
    if (rc != 0) {
        printf("[qpy_usrfs] CUST LittleFS initialization failed: %d\r\n", rc);
        return rc;
    }
    rc = qosa_vfs_register(QPY_USRFS_MOUNT_POINT, &qpy_usrfs_ops, &qpy_usrfs_lfs);
    if (rc != 0) {
        lfs_unmount(&qpy_usrfs_lfs);
        printf("[qpy_usrfs] VFS registration failed: %d\r\n", rc);
        return rc;
    }
    qpy_usrfs_mounted = 1;
    printf("[qpy_usrfs] mounted /usr at CUST 0x%08x, size 0x%08x\r\n",
        (unsigned)UNIRTOS_CUST_FLASH_ADDR, (unsigned)UNIRTOS_CUST_FLASH_SIZE);
    return 0;
}

int qpy_usrfs_is_mounted(void) {
    return qpy_usrfs_mounted;
}
