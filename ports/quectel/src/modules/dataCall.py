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


"""
jayceon 2021/03/23
"""


import dial
import ujson
import sim
import net
import uos
import utime


def setAutoActivate(profileID, enable,cur_simid=None):
    if profileID < 1 or profileID > 3:
        raise ValueError("invalid value, profileID should be in [1,3].")
    if enable != 0 and enable != 1:
        raise ValueError("invalid value, enable should be 0 or 1.")
    if cur_simid != None and cur_simid != 0 and cur_simid != 1:
        raise ValueError("invalid value, enable should be 0 or 1.")
    
    if "datacall_config.json" in uos.listdir('/usr'):
        with open("/usr/datacall_config.json", "r", encoding='utf-8') as fd:
            try:
                datacall_config = ujson.load(fd)
                if not isinstance(datacall_config, dict):
                    raise ValueError("The format of the datacall_config.json is incorrect.")

                value_of_key_profileid = datacall_config.get(str(profileID), None)
                if value_of_key_profileid is not None:
                    if not isinstance(value_of_key_profileid, dict):
                        raise ValueError("The format of the datacall_config.json is incorrect.")
                    if cur_simid ==1:
                        datacall_config[str(profileID)]["autoActivate_1"] = enable
                    else:
                        datacall_config[str(profileID)]["autoActivate"] = enable
                    # value_of_key_autoConnect = value_of_key_profileid.get("autoConnect", 0)
                    # value_of_key_autoConnect为0，有两种情况
                    # 1.autoConnect原本的值就是0
                    # 2.字典中没有autoConnect这个配置项，get获取失败，所以返回默认值0
                    # datacall_config[str(profileID)] = {"autoActivate": enable, "autoConnect": value_of_key_autoConnect}
                else:
                    # 这种情况说明用户删除了配置文件中的某些路配置，导致profileID虽然是合法的，但是配置文件中没有这一路配置信息
                    # 这种情况需要在配置文件中重新写入这一路的配置
                    # datacall_config[str(profileID)] = {"autoActivate": enable, "autoConnect": 0}
                    if cur_simid ==1:
                        datacall_config[str(profileID)] = {"autoActivate": 0, "autoConnect": 0,"autoActivate_1": enable}
                    else:
                        datacall_config[str(profileID)] = {"autoActivate": enable,"autoConnect": 0}
            except Exception:
                raise ValueError("The format of the datacall_config.json is incorrect.")
        with open("/usr/datacall_config.json", "w", encoding='utf-8') as fd:
            datacall_config_json = ujson.dumps(datacall_config)
            fd.write(datacall_config_json)
    else:
        # print('[Warning]The datacall_config.json file does not exist, create it now.')
        default_config = {
            "1": {"autoActivate": 0, "autoConnect": 0},
            "2": {"autoActivate": 0, "autoConnect": 0},
            "3": {"autoActivate": 0, "autoConnect": 0}
        }
        if cur_simid ==1:
            default_config[str(profileID)]["autoActivate_1"] = enable
        else:
            default_config[str(profileID)]["autoActivate"] = enable
        with open("/usr/datacall_config.json", "w", encoding='utf-8') as fd:
            default_config_json = ujson.dumps(default_config)
            fd.write(default_config_json)


def setAutoConnect(profileID, enable, cur_simid=None):
    if profileID < 1 or profileID > 3:
        raise ValueError("invalid value, profileID should be in [1,3].")
    if enable != 0 and enable != 1:
        raise ValueError("invalid value, enable should be 0 or 1.")
    if cur_simid != None and cur_simid != 0 and cur_simid != 1:
        raise ValueError("invalid value, enable should be 0 or 1.")
    
    if "datacall_config.json" in uos.listdir('/usr'):
        with open("/usr/datacall_config.json", "r", encoding='utf-8') as fd:
            try:
                datacall_config = ujson.load(fd)
                if not isinstance(datacall_config, dict):
                    raise ValueError("The format of the datacall_config.json is incorrect.")
                value_of_key_profileid = datacall_config.get(str(profileID), None)
                if value_of_key_profileid is not None:
                    if not isinstance(value_of_key_profileid, dict):
                        raise ValueError("The format of the datacall_config.json is incorrect.")
                    if cur_simid ==1:
                        datacall_config[str(profileID)]["autoConnect_1"] = enable
                    else:
                        datacall_config[str(profileID)]["autoConnect"] = enable
                else:
                    # 这种情况说明用户删除了配置文件中的某些路配置，导致profileID虽然是合法的，但是配置文件中没有这一路配置信息
                    # 这种情况需要在配置文件中重新写入这一路的配置
                    if cur_simid ==1:
                        datacall_config[str(profileID)] = {"autoActivate": 0, "autoConnect": 0,"autoConnect_1": enable}
                    else:
                        datacall_config[str(profileID)] = {"autoActivate": 0,"autoConnect": enable}
            except Exception:
                raise ValueError("The format of the datacall_config.json is incorrect.")
        with open("/usr/datacall_config.json", "w", encoding='utf-8') as fd:
            datacall_config_json = ujson.dumps(datacall_config)
            fd.write(datacall_config_json)
    else:
        # print('[Warning]The datacall_config.json file does not exist, create it now.')
        default_config = {
            "1": {"autoActivate": 0, "autoConnect": 0},
            "2": {"autoActivate": 0, "autoConnect": 0},
            "3": {"autoActivate": 0, "autoConnect": 0}
        }
        if cur_simid ==1:
            default_config[str(profileID)]["autoConnect_1"] = enable
        else:
            default_config[str(profileID)]["autoConnect"] = enable
        with open("/usr/datacall_config.json", "w", encoding='utf-8') as fd:
            default_config_json = ujson.dumps(default_config)
            fd.write(default_config_json)
    if cur_simid != None:
        dial.setAutoConnect(profileID, enable,cur_simid)
    else:
        dial.setAutoConnect(profileID, enable)



def setPDPContext(profileID, ipType, apn, username, password, authType, cur_simid=None):
    if profileID < 1 or profileID > 3:
        raise ValueError("invalid value, profileID should be in [1,3].")
    if authType < 0 or authType >3:
        raise ValueError("invalid value.")
    # Users can set apn, username, and password to None. 
    # When it is set to None, it means that the original values will not be changed and the original values will be used instead.
    if apn is None or username is None or password is None:
        pdp_context = dial.getPDPContext(profileID)
        if not isinstance(pdp_context,tuple) or len(pdp_context) < 3:
            raise ValueError("getPDPContext error.")
        if apn is None:
            apn = pdp_context[1]
        if username is None:
            username = pdp_context[2]
        if password is None:
            password = pdp_context[3]
    # if len(username) == 0 and len(password) == 0 and authType != 0:
    #     authType = 0
    if (len(username) != 0 or len(password) != 0) and authType == 0:
        authType = 2
    if cur_simid == None:
        return dial.setPDPContext(profileID, ipType, apn, username, password, authType)
    else:
         return dial.setPDPContext(profileID, ipType, apn, username, password, authType, cur_simid)


def getPDPContext(profileID, cur_simid=None):
    if profileID < 1 or profileID > 3:
        raise ValueError("invalid value, profileID should be in [1,3].")
    if cur_simid == None:
        return dial.getPDPContext(profileID)
    else:
        return dial.getPDPContext(profileID, cur_simid)
    


def setDNSServer(profileID, simID, priDNS, secDNS):
    if profileID < 1 or profileID > 3:
        raise ValueError("invalid value, profileID should be in [1,3].")
    return dial.setDnsserver(profileID, simID, priDNS, secDNS)


def activate(profileID, cur_simid=None):
    # retval = dial.getPdpRange()
    # min_profile = retval[0]
    # max_profile = retval[1]
    if profileID < 1 or profileID > 3:
        raise ValueError("invalid value, profileID should be in [1,3].")

    if "datacall_config.json" in uos.listdir('/usr'):
        with open("/usr/datacall_config.json", "r", encoding='utf-8') as fd:
            try:
                datacall_config = ujson.load(fd)
                if not isinstance(datacall_config, dict):
                    raise ValueError("The format of the datacall_config.json is incorrect.")
                value_of_key_profileid = datacall_config.get(str(profileID), None)
                if value_of_key_profileid is not None:
                    if not isinstance(value_of_key_profileid, dict):
                        raise ValueError("The format of the datacall_config.json is incorrect.")
                    auto_connect = value_of_key_profileid.get("autoConnect", 0)
                    dial.setAutoConnect(profileID, auto_connect)
                else:
                    raise ValueError("No configuration information for profileID {}.".format(profileID))
            except Exception:
                raise ValueError("The format of the datacall_config.json is incorrect.")
    if cur_simid == None:
        return dial.start(profileID, 0, "", "", "", 0)
    else:
        return dial.start(profileID, 0, "", "", "", 0,cur_simid)


def deactivate(profileID, cur_simid=None):
    # retval = dial.getPdpRange()
    # min_profile = retval[0]
    # max_profile = retval[1]
    if profileID < 1 or profileID > 3:
        raise ValueError("invalid value, profileID should be in [1,3].")

    pdpctx = dial.getPDPContext(profileID)
    if pdpctx != -1:
        iptype = pdpctx[0]
        if cur_simid == None:
            return dial.stop(profileID, iptype)
        else:
            return dial.stop(profileID, iptype,cur_simid)
    else:
        return -1

######################################################################################

def start(profileidx, iptype=0, apn="", username="", password="", authtype=0, cur_simid=None):
    ret = dial.setPDPContext(profileidx, iptype, apn, username, password, authtype)
    if ret == 0:
        if cur_simid == None:
            return dial.start(profileidx, iptype, apn, username, password, authtype)
        else:
            return dial.start(profileidx, iptype, apn, username, password, authtype, cur_simid)
    else:
        return -1


def stop(profileidx, iptype,cur_simid=None):
    if cur_simid == None:
        return dial.stop(profileidx, iptype)
    else:
        return dial.stop(profileidx, iptype, cur_simid)


def getInfo(profileidx, iptype, cur_simid=None):
    if cur_simid == None:
        ret = dial.getInfo(profileidx, iptype)
    else:
        ret = dial.getInfo(profileidx, iptype, cur_simid)
    if ret == -1:
        ipv4 = [0, 0, '0.0.0.0', '0.0.0.0', '0.0.0.0']
        ipv6 = [0, 0, '::', '::', '::']
        if iptype ==2:
            return (profileidx, iptype, ipv4, ipv6)
        elif iptype == 1:
            return (profileidx, iptype, ipv6)
        elif iptype == 0:
            return (profileidx, iptype, ipv4)
    return ret 

def getAddressinfo(profileidx, iptype):
    ret = dial.getAddressinfo(profileidx, iptype)
    if ret == -1:
        ipv4 = ['00-00-00-00-00-00', '0.0.0.0', '0.0.0.0']
        ipv6 = ['00-00-00-00-00-00', '::', '::']
        if iptype ==2:
            return (ipv4, ipv6)
        elif iptype == 1:
            return (ipv6)
        elif iptype == 0:
            return (ipv4)
    return ret

def getSiminfo(simID):
    ret = dial.getSiminfo(simID)
    if ret == -1:
        return (0, 0, 0, 0)
    return ret

def setApn(profileidx, iptype, apn, username, password, authtype, flag=0):
    retval = dial.getPdpRange()
    iptype_max = dial.getRange(4)  # iptype
    iptype_max = iptype_max - 1
    authtype_max = dial.getRange(5)  # authtype
    authtype_max = authtype_max - 1
    min = retval[0]
    max = retval[1]
    if profileidx < min or profileidx > max:
        raise ValueError("invalid value, profileIdx should be in [{},{}].".format(min, max))
    if iptype < 0 or iptype > iptype_max:
        raise ValueError("invalid value, iptype should be in [0,{}].".format(iptype_max))

    apn_len = dial.getRange(0)
    if len(apn) > apn_len:
        raise ValueError("invalid value, the length of apn should be no more than [{}] bytes.".format(apn_len))
    username_len = dial.getRange(2)
    if len(username) > username_len:
        raise ValueError(
            "invalid value, the length of username should be no more than [{}] bytes.".format(username_len))
    password_len = dial.getRange(1)
    if len(password) > password_len:
        raise ValueError(
            "invalid value, the length of password should be no more than [{}] bytes.".format(password_len))
    if authtype < 0 or authtype > authtype_max:
        raise ValueError("invalid value, authtype should be in [0,{}].".format(authtype_max))
    if flag != 0 and flag != 1:
        raise ValueError("invalid value, flag should be in [0,1].")

    with open("/usr/user_apn.json", "w+", encoding='utf-8') as fd:
        apn_dict = \
            {
                "pdp": str(profileidx),
                "iptype": str(iptype),
                "apn": apn,
                "user": username,
                "password": password,
                "authtype": str(authtype)
            }
        apn_data = ujson.dumps(apn_dict)
        fd.write(apn_data)
    return dial.setPDPContext(profileidx, iptype, apn, username, password, authtype)


def setAsynMode(mode):
    return dial.setAsynMode(mode)


def setCallback(usrfun):
    return dial.setCallback(usrfun)


def getApn(*args):
    return dial.getApn(*args)

def useAttachApn(*args):
    return dial.useAttachApn(*args)

def setDnsserver(profileidx, simid, new_pri, new_sec):
    return dial.setDnsserver(profileidx, simid, new_pri, new_sec)

def getSpeed():
    return dial.getSpeed()

def getTraffic():
    return dial.getTraffic()

def startByUserApns(apn_dict=None, filename=None):
    apn_info_dict = {}
    source_of_apn = 0
    apn_file_path = ''
    if apn_dict is None and filename is None:
        raise ValueError('You must choose one way to get APN information, through a dict or a json file.')
    elif apn_dict is not None and filename is None:
        if not isinstance(apn_dict, dict):
            raise ValueError('The args apn_dict must be dict')
        source_of_apn = 1  # Get APN information from dict
    elif apn_dict is None and filename is not None:
        source_of_apn = 2  # Get APN information from a JSON file
        apn_file_path = filename
        # apn_file_path = '/usr/' + filename
    else:
        raise ValueError('You can only get APN information in one of two ways, through a dict or json file.')

    stage_code = 1
    retry_cnt = 0
    print('Checking the SIM card status...')
    while True:
        sim_sta = sim.getStatus()
        if sim_sta == 1:
            print('The SIM status is normal, status is 1.')
            break
        else:
            if sim_sta == 0:
                print('Check whether SIM card is inserted or whether the card slot is loose.')
                return stage_code, 0
            else:
                retry_cnt += 1
                if retry_cnt > 1000:
                    print('The SIM card status is abnormal, status is {}'.format(sim_sta))
                    return stage_code, sim_sta
                else:
                    utime.sleep_ms(20)
                    continue

    stage_code = 2
    retry_cnt1 = 0
    retry_cnt2 = 0
    print('Checking the network register status...')
    while True:
        net_register_sta = net.getState()
        if net_register_sta == -1:
            retry_cnt1 += 1
            if retry_cnt1 > 400:
                print('Get the register status failed.')
                return stage_code, -1
            else:
                utime.sleep_ms(50)
                continue
        else:
            if net_register_sta[1][0] == 1 or net_register_sta[1][0] == 5:
                print('Network register status is normal, status is {}.'.format(net_register_sta[1][0]))
                break
            else:
                retry_cnt2 += 1
                if retry_cnt2 > 500:
                    csq = net.csqQueryPoll()
                    str1 = 'The device could not register to the network, status is {}'.format(net_register_sta[1][0])
                    str2 = ''
                    if csq == 99 or csq < 18:
                        str2 = 'maybe because the signal is weak, CSQ is {}.'.format(csq)
                    else:
                        str2 = 'please confirm whether the SIM card is in arrears.'
                    print(str1+str2)
                    return stage_code, net_register_sta[1][0]
                else:
                    utime.sleep_ms(20)
                    continue

    stage_code = 3
    retval = dial.getPdpRange()
    min_pdp = retval[0]
    max_pdp = retval[1]
    print('Checking the datacall status...')
    for pdp in range(min_pdp, max_pdp + 1):
        dial_sta = dial.getInfo(pdp, 2)
        if (dial_sta != -1) and (dial_sta[2][0] == 0) and (dial_sta[3][0] == 0):
            continue
        elif (dial_sta != -1) and ((dial_sta[2][0] == 1) or (dial_sta[3][0] == 1)):
            # The user does not disable the default startup data call function
            print('It has already datacall, repeat datacall is not allowed.[profileIdx={}]'.format(pdp))
            return stage_code, 0

    print('The datacall status is 0, start to datacall... ')
    if source_of_apn == 1:  # Get APN information from dict
        apn_info_dict = apn_dict
        print('Get APN information from dict.')
    elif source_of_apn == 2:  # Get APN information from a JSON file
        print('Get APN information from a json file {}'.format(apn_file_path))
        try:
            with open(apn_file_path, "r", encoding='utf-8') as fd:
                apn_info_dict = ujson.load(fd)
        except OSError as e:
            if e.args[0] == 2:
                raise ValueError('{} not found.'.format(apn_file_path))
        except ValueError:
            raise ValueError('Please make sure the contents of your json file are valid.')

    print('Trying to datacall...')
    for value in apn_info_dict.values():
        pdp = value.get('profileIdx')
        ipv = value.get('ipType')
        apn = value.get('apn')
        usr = value.get('username')
        pwd = value.get('password')
        ath = value.get('authType')

        if pdp is None:
            raise ValueError('profileIdx can not be None.')
        if ipv is None:
            raise ValueError('ipType can not be None.')
        if apn is None:
            raise ValueError('apn can not be None.')
        if usr is None:
            raise ValueError('username can not be None.')
        if pwd is None:
            raise ValueError('password can not be None.')
        if ath is None:
            raise ValueError('authType can not be None.')

        pdp = int(pdp)
        ipv = int(ipv)
        ath = int(ath)
        ret = dial.start(pdp, ipv, apn, usr, pwd, ath)
        if ret == -1:
            utime.sleep_ms(20)
            print('data call error, profileIdx:{}, apn:{}, ipType:{}'.format(pdp, apn, ipv))
            continue

        retry_cnt3 = 0
        while True:
            retry_cnt3 += 1
            dial_sta = dial.getInfo(pdp, 2)
            if (dial_sta[2][0] == 1) or (dial_sta[3][0] == 1):
                print('datacall success, profileIdx:{}, apn:{}, ipType:{}[{}]'.format(pdp, apn, ipv, retry_cnt3))
                dial.setAutoConnect(pdp, 1)
                return stage_code, 1
            else:
                utime.sleep_ms(10)
                if retry_cnt3 > 500:
                    print('get data call info successfully, but data call status is 0, retry {}'.format(retry_cnt3))
                    print('Trying to datacall with next apn...')
                    break
    print('Tried to datacall with all apns of user, but datacall failed.')
    return stage_code, -1


def poweronAutoDatacall(enable):
    if enable != 0 and enable != 1:
        raise ValueError('the value of enable should be 0 or 1.')
    if "system_config.json" in uos.listdir("/usr"):
        json_data = {}
        with open("/usr/system_config.json", "r", encoding='utf-8') as fd:
            try:
                json_data = ujson.load(fd)
                repl_flag = json_data.get("replFlag", 0)
            except ValueError:
                with open("/usr/system_config.json", "w", encoding='utf-8') as fd:
                    new_json_data = ujson.dumps({"replFlag": 0, "datacallFlag": enable})
                    fd.write(new_json_data)
        with open("/usr/system_config.json", "w", encoding='utf-8') as fd:
            new_json_data = ujson.dumps({"replFlag": repl_flag, "datacallFlag": enable})
            fd.write(new_json_data)
    else:
        with open("/usr/system_config.json", "w", encoding='utf-8') as fd:
            new_json_data = ujson.dumps({"replFlag": 0, "datacallFlag": enable})
            fd.write(new_json_data)
