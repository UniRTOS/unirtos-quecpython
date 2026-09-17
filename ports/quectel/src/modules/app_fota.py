import app_fota_download
import app_fota_updater
from app_fota_mount import AppFotaPkgMount

class new(object):
    def __init__(self):
        self.app_fota_pkg_mount = AppFotaPkgMount()
        self.app_fota_pkg_mount.mount_disk()

    def download(self, url, file_name, headers=None, ipvtype=0, ext_enable=False, spi_port=None, spi_clk=None, dl_location=None, username=None, password=None):
        return app_fota_download.download(url, file_name, headers, ipvtype=ipvtype, ext_enable=ext_enable, spi_port=spi_port, spi_clk=spi_clk, dl_location=dl_location, username=username, password=password)

    def bulk_download(self, info=[], headers=None):
        return app_fota_download.bulk_download(info, headers)

    def set_update_flag(self):
        app_fota_download.set_update_flag()

    def update(self):
        return app_fota_updater.update()
