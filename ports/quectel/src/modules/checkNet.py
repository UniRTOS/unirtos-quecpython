import sim
import net
import utime
import modem
import dial
import dataCall
from misc import Power


class CheckNetwork:
    def __init__(self, proj_name, proj_version):
        self.PROJECT_NAME = proj_name
        self.PROJECT_VERSION = proj_version
        self.FIRMWARE_VERSION = modem.getDevFwVersion()
        self.POWERON_REASON = Power.powerOnReason()

    def poweron_print_once(self):
        print("==================================================")
        print("PROJECT_NAME     : {}".format(self.PROJECT_NAME))
        print("PROJECT_VERSION  : {}".format(self.PROJECT_VERSION))
        print("FIRMWARE_VERSION : {}".format(self.FIRMWARE_VERSION))
        print("POWERON_REASON   : {}".format(self.POWERON_REASON))
        print("SIM_CARD_STATUS  : {}".format(sim.getStatus()))
        print("==================================================")

    @staticmethod
    def check_datacall_status():
        retval = dial.getPdpRange()
        min = retval[0]
        max = retval[1]
        for pdp in range(min, max + 1):
            datacall_sta = dataCall.getInfo(pdp, 2)
            if (datacall_sta != -1) and ((datacall_sta[2][0] == 1) or (datacall_sta[3][0] == 1)):
                return 1
            elif (datacall_sta != -1) and (datacall_sta[2][0] == 0) and (datacall_sta[3][0] == 0):
                continue
        return 0

    def wait_network_connected(self, timeout_s=60):
        return wait_network_connected(timeout_s)


def wait_network_connected(timeout_s=60):
    if timeout_s < 1 or timeout_s > 3600:
        raise OSError("timeout_s should be in [1, 3600]s!")
    timeout_ms = timeout_s * 1000
    last = utime.ticks_ms()

    while timeout_ms > 0:
        if sim.getStatus() == 1:
            break
        utime.sleep_ms(100)
        now = utime.ticks_ms()
        timeout_ms -= utime.ticks_diff(now, last)
        last = now
    else:
        return 1, sim.getStatus()

    while timeout_ms > 0:
        state = net.getState()
        if state != -1 and len(state) > 1 and len(state[1]) > 0 and state[1][0] in (1, 5):
            break
        utime.sleep_ms(100)
        now = utime.ticks_ms()
        timeout_ms -= utime.ticks_diff(now, last)
        last = now
    else:
        return 2, -1

    while timeout_ms > 0:
        status = CheckNetwork.check_datacall_status()
        if status == 1:
            return 3, 1
        utime.sleep_ms(100)
        now = utime.ticks_ms()
        timeout_ms -= utime.ticks_diff(now, last)
        last = now
    return 3, 0


def waitNetworkReady(timeout_s=60):
    return wait_network_connected(timeout_s)
