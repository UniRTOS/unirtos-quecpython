# Copyright (c) Quectel Wireless Solution, Co., Ltd.All Rights Reserved.
#  
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#  
#     http://www.apache.org/licenses/LICENSE-2.0
#  
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

try:
    import usocket as socket
except:
    import socket
try:
    import ustruct as struct
except:
    import struct

# (date(2000, 1, 1) - date(1900, 1, 1)).days * 24*60*60
NTP_DELTA = 3155673600

# The NTP host can be configured at runtime by doing: ntptime.host = 'myhost.org'
host = "ntp.aliyun.com"
r_host = ["pool.ntp.org", "asia.pool.ntp.org", "cn.ntp.org.cn", "cn.pool.ntp.org"]
ntp_sethost = None

def sethost(ntp_host=None):
    global host
    global ntp_sethost
    if ntp_host != None:
        host = ntp_host
        ntp_sethost = ntp_host
        return 0
    else:
        return -1
        

def time(use_rhost = 1, timeout = 10):
    NTP_QUERY = bytearray(48)
    NTP_QUERY[0] = 0x1B
    global host
    global ntp_sethost
    val = None

    for host_m in r_host:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            addr = socket.getaddrinfo(host, 123,socket.AF_INET)[0][-1]
            s.settimeout(timeout)
            res = s.sendto(NTP_QUERY, addr)
            msg = s.recv(48)
            s.close()
            flags = msg[0]
            stratum = msg[1]
            if (flags == 0x1c) and (stratum > 0) and (stratum < 16):
                val = struct.unpack("!I", msg[40:44])[0]
                if val == 0:
                    print("NTP resp FAIL val:%s "%(val))
                    raise OSError(-1)
                break
            else:
                print("NTP resp FAIL flags:%s stratum:%s "%(flags,stratum))
                raise OSError(-1)
        except:
            s.close()
            if use_rhost != 1:
                break
            host = host_m
            continue

    if ntp_sethost is not None:
        host = ntp_sethost
    else:
        host = "ntp.aliyun.com"

    if val == None:
        raise OSError("Server connection failed!")
    return val - NTP_DELTA


# There's currently no timezone support in MicroPython, so
# utime.localtime() will return UTC time (as if it was .gmtime())
def settime(timezone = 0, use_rhost = 1, timeout = 10):
    import machine
    import utime

    if timezone < -12 or timezone > 12:
        return -1

    if use_rhost != 0 and use_rhost != 1:
        return -1

    if timeout <= 0:
        return -1

    try:
        t = time(use_rhost, timeout)
        t = t + 946656000 + timezone * 3600
        tm = utime.localtime(t)
        machine.RTC().datetime((tm[0], tm[1], tm[2], tm[6], tm[3], tm[4], tm[5], 0))
    except:
        return -1
    return 0
