import uos
import request
import ujson
import ql_fs
from app_fota_mount import AppFotaPkgMount

app_fota_pkg_mount = AppFotaPkgMount()

def get_updater_dir():
    return app_fota_pkg_mount.fota_dir + "/usr/.updater"

def get_download_stat_file():
    return get_updater_dir() + "/download.stat"

def get_update_flag_file():
    return get_updater_dir() + "/update.flag"

updater_dir = get_updater_dir()
download_stat_file = get_download_stat_file()
update_flag_file = get_update_flag_file()

def _get_download_stat_by_file(file_name):
    try:
        download_stat = _get_download_stat()
        if download_stat:
            for item in download_stat:
                if item['name'].lower() == file_name.lower():
                    return item
    except Exception:
        pass
    return None

def _get_download_stat():
    try:
        if not ql_fs.path_exists(get_download_stat_file()):
            return None
        fp = open(get_download_stat_file(), 'rt')
        if not fp:
            return None
        content = fp.read()
        fp.close()
        if not content:
            return None
        return ujson.loads(content)
    except Exception as e:
        print("get download stat error: " + str(e))
        return None

def get_download_stat():
    return _get_download_stat()

def _fetch(url, fetched_size, headers=None, ipvtype=0, username=None, password=None):
    request_headers = headers if isinstance(headers, dict) else {}
    if fetched_size > 0:
        request_headers['Range'] = 'bytes={}-'.format(fetched_size)
    return request.get(url, headers=request_headers, ipvtype=ipvtype, username=username, password=password)

def _update_download_stat(url, file_name, total_size, download_file_name=None):
    download_stat = _get_download_stat() or []
    for item in download_stat:
        if item['name'].lower() == file_name.lower():
            item['url'] = url
            item['total_size'] = total_size
            item['dl_location'] = download_file_name
            break
    else:
        download_stat.append({'url': url, 'name': file_name, 'total_size': total_size, 'dl_location': download_file_name})
    fp = open(get_download_stat_file(), 'wt')
    fp.write(ujson.dumps(download_stat))
    fp.close()

def update_download_stat(url, file_name, total_size):
    _update_download_stat(url, file_name, total_size)

def delete_update_file(file_name):
    download_stat = _get_download_stat()
    if download_stat:
        for item in download_stat[:]:
            if item['name'].lower() == file_name.lower():
                download_stat.remove(item)
    fp = open(get_download_stat_file(), 'wt')
    fp.write(ujson.dumps(download_stat))
    fp.close()

def get_root_dir(path):
    if path[0] == '/':
        return path.split('/')[1]
    return path.split('/')[0]

def download(url, file_name, headers=None, ipvtype=0, username=None, password=None, ext_enable=False, spi_port=None, spi_clk=None, dl_location='/usr/'):
    target_root = get_root_dir(file_name)
    location_root = get_root_dir(dl_location) if dl_location else 'usr'
    if target_root != 'usr' or location_root != 'usr':
        if not ext_enable:
            raise ValueError("Parameter error")
        return -1
    download_file_name = get_updater_dir() + '/' + file_name
    ql_fs.mkdirs(ql_fs.path_dirname(download_file_name))
    single_download_stat = _get_download_stat_by_file(file_name)
    if single_download_stat and ql_fs.path_exists(download_file_name):
        fetched_size = ql_fs.path_getsize(download_file_name)
        if fetched_size == single_download_stat['total_size']:
            return 0
    else:
        fetched_size = 0
    response = _fetch(url, fetched_size, headers, ipvtype, username, password)
    if response.status_code != 200 and response.status_code != 206:
        response.close()
        return -1
    if single_download_stat:
        mode = 'ab+'
    else:
        _update_download_stat(url, file_name, 'unknown', download_file_name)
        mode = 'wb+'
    fp = open(download_file_name, mode)
    try:
        while True:
            chunk = next(response.content)
            for offset in range(0, len(chunk), 4096):
                fp.write(chunk[offset:offset + 4096])
    except StopIteration:
        fp.close()
        response.close()
    except Exception:
        fp.close()
        response.close()
        try:
            uos.remove(download_file_name)
        except Exception:
            pass
        return -1
    total_size = ql_fs.path_getsize(download_file_name)
    _update_download_stat(url, file_name, total_size, download_file_name)
    if response.sizeof == 2048 and response.flag == 0:
        return 0
    if total_size == response.sizeof + fetched_size:
        return 0
    return -1

def bulk_download(info=None, headers=None):
    if info is None:
        info = []
    fail_result = []
    for item in info:
        if download(item['url'], item['file_name'], headers, ipvtype=0, ext_enable=item.get('ext_enable'), spi_port=item.get('spi_port'), spi_clk=item.get('spi_clk'), dl_location=item.get('dl_location')) == -1:
            fail_result.append(item)
    if len(fail_result):
        return fail_result
    return None

def set_update_flag():
    ql_fs.mkdirs(ql_fs.path_dirname(get_update_flag_file()))
    fp = open(get_update_flag_file(), 'wb')
    fp.close()
