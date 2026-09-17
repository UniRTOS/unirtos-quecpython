def _check_module(module, names):
    missing = []
    for name in names:
        if not hasattr(module, name):
            missing.append(name)
    return missing


def run():
    import atcmd
    import checkNet
    import dataCall
    import log
    import machine
    import misc
    import modem
    import net
    import ql_fs
    import queue
    import sim
    import sms
    import system

    checks = {
        "atcmd": _check_module(atcmd, ("sendSync",)),
        "checkNet": _check_module(checkNet, ("CheckNetwork", "wait_network_connected", "waitNetworkReady")),
        "dataCall": _check_module(dataCall, (
            "setAutoActivate", "setAutoConnect", "setPDPContext", "getPDPContext",
            "setDNSServer", "setDnsserver", "activate", "deactivate", "start", "stop",
            "getInfo", "getAddressinfo", "getSiminfo", "setApn", "getApn",
            "setAsynMode", "setCallback", "getPdpRange", "getSpeed", "getTraffic",
            "startByUserApns", "poweronAutoDatacall",
        )),
        "log": _check_module(log, ("getLogger", "basicConfig", "set_output", "DEBUG", "INFO", "ERROR")),
        "machine": _check_module(machine, ("UART", "Pin", "I2C", "SPI", "Timer", "WDT")),
        "misc": _check_module(misc, ("Power", "PowerKey", "PWM", "ADC", "USB")),
        "modem": _check_module(modem, ("getDevFwVersion", "getDevImei", "getDevModel", "getDevSN")),
        "net": _check_module(net, ("getState", "csqQueryPoll", "getSignal", "operatorName", "getCellInfo")),
        "ql_fs": _check_module(ql_fs, (
            "path_exists", "file_exists", "dir_exists", "file_copy", "file_read",
            "file_write", "file_remove", "file_rename", "path_dirname",
            "path_getsize", "file_size", "mkdirs", "makedirs", "rmdirs",
            "removedirs", "touch", "write_json", "read_json",
        )),
        "queue": _check_module(queue, ("Queue",)),
        "sim": _check_module(sim, ("getStatus", "getImsi", "getIccid", "getPhoneNumber")),
        "sms": _check_module(sms, ("sendTextMsg", "sendPduMsg", "deleteMsg", "setCallback")),
        "system": _check_module(system, (
            "getDevFwVersion", "getDevImei", "reboot", "powerRestart",
            "powerDown", "files", "replSetEnable", "replChangPswd",
        )),
    }
    failed = {}
    for name, missing in checks.items():
        if missing:
            failed[name] = missing
    return failed


if __name__ == "__main__":
    print(run())
