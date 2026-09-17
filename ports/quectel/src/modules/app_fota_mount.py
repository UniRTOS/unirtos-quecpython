class AppFotaPkgMount(object):
    __instance = None
    __mount_state = False
    __can_mount = False

    def __new__(cls, *args, **kwargs):
        if cls.__instance is None:
            cls.__instance = object.__new__(cls)
        return cls.__instance

    def __init__(self):
        self.__fota_dir = ""

    @property
    def mount_state(self):
        return self.__mount_state

    @property
    def can_mount(self):
        return self.__can_mount

    @property
    def fota_dir(self):
        if not self.mount_state or not self.can_mount:
            return ""
        return self.__fota_dir

    def mount_disk(self):
        self.__mount_state = True
        self.__can_mount = True

    def umount_disk(self):
        if self.mount_state:
            self.__mount_state = False

    def get_fota_file_name(self, path):
        return self.fota_dir + path
