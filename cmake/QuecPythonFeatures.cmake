# Feature-to-source mappings.  Keep this file declarative so board profiles
# can be reviewed without changing the build mechanics in CMakeLists.txt.
set(QPY_FROZEN_MODULE_FEATURES
    "_boot.py|MICROPY_QPY_FROZEN_BOOT"
    "app_fota.py|MICROPY_QPY_FROZEN_APP_FOTA"
    "app_fota_boot.py|MICROPY_QPY_FROZEN_APP_FOTA"
    "app_fota_download.py|MICROPY_QPY_FROZEN_APP_FOTA"
    "app_fota_mount.py|MICROPY_QPY_FROZEN_APP_FOTA"
    "app_fota_updater.py|MICROPY_QPY_FROZEN_APP_FOTA"
    "checkNet.py|MICROPY_QPY_FROZEN_CHECKNET"
    "dataCall.py|MICROPY_QPY_FROZEN_DATACALL"
    "log.py|MICROPY_QPY_FROZEN_LOG"
    "ntptime.py|MICROPY_QPY_FROZEN_NTPTIME"
    "ql_fs.py|MICROPY_QPY_FROZEN_QL_FS"
    "queue.py|MICROPY_QPY_FROZEN_QUEUE"
    "request.py|MICROPY_QPY_FROZEN_REQUEST"
    "SIM.py|MICROPY_QPY_FROZEN_SIM"
    "system.py|MICROPY_QPY_FROZEN_SYSTEM"
    "ure.py|MICROPY_QPY_FROZEN_URE"
    "umqtt.py|MICROPY_QPY_FROZEN_UMQTT"
    "ymodem.py|MICROPY_QPY_FROZEN_YMODEM"
)

set(QPY_PORT_FEATURE_SOURCES
    "moduos_qosa.c|MICROPY_QPY_MODULE_UOS"
    "modmachine_qosa.c|MICROPY_QPY_MODULE_MACHINE"
    "modatcmd.c|MICROPY_QPY_MODULE_ATCMD"
    "modexample.c|MICROPY_QPY_MODULE_EXAMPLE"
    "moddev.c|MICROPY_QPY_MODULE_MODEM"
    "moddial.c|MICROPY_QPY_MODULE_DATACALL"
    "modmisc.c|MICROPY_QPY_MODULE_MISC"
    "modnet.c|MICROPY_QPY_MODULE_NET"
    "modsim.c|MICROPY_QPY_MODULE_SIM"
    "modsms.c|MICROPY_QPY_MODULE_SMS"
    "modusocket.c|MICROPY_QPY_MODULE_USOCKET"
    "modostimer_qosa.c|MICROPY_QPY_MODULE_OSTIMER"
    "modfota_qosa.c|MICROPY_QPY_MODULE_FOTA"
)

set(QPY_MACHINE_FEATURE_SOURCES
    "machine_pin_qosa.c|MICROPY_QPY_MACHINE_PIN"
    "machine_timer_qosa.c|MICROPY_QPY_MACHINE_TIMER"
    "machine_i2c_qosa.c|MICROPY_QPY_MACHINE_I2C"
    "machine_spi_qosa.c|MICROPY_QPY_MACHINE_SPI"
    "machine_rtc_qosa.c|MICROPY_QPY_MACHINE_RTC"
    "machine_wdt_qosa.c|MICROPY_QPY_MACHINE_WDT"
    "machine_extint_qosa.c|MICROPY_QPY_MACHINE_EXTINT"
    "machine_key_qosa.c|MICROPY_QPY_MACHINE_KEY"
)

set(QPY_MISC_FEATURE_SOURCES
    "misc_powerkey_qosa.c|MICROPY_QPY_MISC_POWERKEY"
    "misc_temperature_qosa.c|MICROPY_QPY_MISC_TEMPERATURE"
    "misc_usb_qosa.c|MICROPY_QPY_MISC_USB"
    "misc_usbnet_qosa.c|MICROPY_QPY_MISC_USBNET"
    "misc_adc_qosa.c|MICROPY_QPY_MISC_ADC"
    "misc_pwm_qosa.c|MICROPY_QPY_MISC_PWM"
)

set(QPY_UPSTREAM_FEATURE_SOURCES
    "shared/netutils/netutils.c|MICROPY_QPY_MODULE_USOCKET"
    "extmod/modbinascii.c|MICROPY_QPY_MODULE_UBINASCII"
    "extmod/modjson.c|MICROPY_QPY_MODULE_UJSON"
    "extmod/modrandom.c|MICROPY_QPY_MODULE_URANDOM"
    "extmod/modselect.c|MICROPY_QPY_MODULE_USELECT"
    "extmod/modtime.c|MICROPY_QPY_MODULE_UTIME"
)
