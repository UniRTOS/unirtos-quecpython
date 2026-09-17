import uos
import ujson
import ql_fs
from app_fota_mount import AppFotaPkgMount

app_fota_pkg_mount = AppFotaPkgMount()
download_stat_file_max_size = 16384

def get_updater_dir():
    return app_fota_pkg_mount.fota_dir + '/usr/.updater'

def get_download_stat_file():
    return get_updater_dir() + '/download.stat'

def get_update_flag_file():
    return get_updater_dir() + '/update.flag'

def _check_update_flag():
    try:
        return 1 if ql_fs.path_exists(get_update_flag_file()) else 0
    except Exception:
        return 0

def _write_remaining(download_stat):
    fp = open(get_download_stat_file(), 'wt')
    fp.write(ujson.dumps(download_stat))
    fp.close()

def update():
    if not _check_update_flag():
        app_fota_pkg_mount.umount_disk()
        return -1
    try:
        if not ql_fs.path_exists(get_download_stat_file()):
            return -1
        fp = open(get_download_stat_file(), 'rt')
        if not fp:
            return -1
        content = fp.read(download_stat_file_max_size)
        fp.close()
        if not content:
            return -1
        download_stat = ujson.loads(content)
        remaining = download_stat[:]
        for item in download_stat:
            file_name = item['name']
            download_file_name = item.get('dl_location')
            if not download_file_name:
                download_file_name = get_updater_dir() + '/' + file_name
            if ql_fs.path_exists(download_file_name):
                ql_fs.mkdirs(ql_fs.path_dirname(file_name))
                if not ql_fs.file_copy(file_name, download_file_name):
                    return -1
            remaining.remove(item)
            _write_remaining(remaining)
            try:
                uos.remove(download_file_name)
            except Exception:
                pass
        uos.remove(get_update_flag_file())
        uos.remove(get_download_stat_file())
        ql_fs.rmdirs(get_updater_dir())
        app_fota_pkg_mount.umount_disk()
        return 0
    except Exception:
        return -1
