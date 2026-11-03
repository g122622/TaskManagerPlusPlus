"""Inspect Windows CPU counters and NVIDIA sensors without installing drivers.

Windows x64, Python 3.10+, standard library only. The optional private NVAPI query
uses the version-1 layout documented by LibreHardwareMonitor's NvidiaGpu/NvApi
implementation; it is a compatibility experiment, not a stable vendor contract.
See docs/HARDWARE_MONITORING.md for sources and interpretation.
"""

import argparse
import ctypes as c
import datetime
import json
import os
import re
import sys
import time
import winreg


U32 = c.c_uint32
STATUS = c.c_int32
HANDLE = c.c_void_p
PDH_MORE_DATA = c.c_int32(0x800007D2).value


def system_dll(name):
    kernel = c.WinDLL("kernel32", use_last_error=True)
    directory = c.create_unicode_buffer(32768)
    kernel.GetSystemDirectoryW.argtypes = [c.c_wchar_p, U32]
    kernel.GetSystemDirectoryW.restype = U32
    length = kernel.GetSystemDirectoryW(directory, len(directory))
    if not 0 < length < len(directory):
        raise OSError("GetSystemDirectoryW failed")
    return c.CDLL(os.path.join(directory.value, name), winmode=0x800)


def bind(library, name, arguments):
    function = getattr(library, name)
    function.argtypes = arguments
    function.restype = STATUS
    return function


def scalar(function, arguments, unit, factor=1):
    if function is None:
        return {"available": False, "reason": "function_missing"}
    value = U32()
    status = function(*arguments, c.byref(value))
    result = {"available": status == 0, "status": status}
    if status == 0:
        result.update(value=value.value / factor, unit=unit)
    return result


def optional(library, name, arguments):
    try:
        return bind(library, name, arguments)
    except AttributeError:
        return None


class PciInfo(c.Structure):
    _fields_ = [("legacy", c.c_char * 16), ("domain", U32), ("bus", U32),
                ("device", U32), ("device_id", U32), ("subsystem_id", U32),
                ("bus_id", c.c_char * 32)]


class Temperature(c.Structure):
    _fields_ = [("version", U32), ("sensor", U32), ("temperature", STATUS)]


class CounterData(c.Structure):
    # PDH_FMT_COUNTERVALUE's anonymous union contains a double or an 8-byte integer.
    _fields_ = [("status", U32), ("value", c.c_double)]


class CounterItem(c.Structure):
    _fields_ = [("name", c.c_wchar_p), ("data", CounterData)]


def processor_counters(interval):
    library = system_dll("pdh.dll")
    open_query = bind(library, "PdhOpenQueryW", [c.c_wchar_p, c.c_size_t, c.POINTER(HANDLE)])
    add = bind(library, "PdhAddEnglishCounterW", [HANDLE, c.c_wchar_p, c.c_size_t, c.POINTER(HANDLE)])
    collect = bind(library, "PdhCollectQueryData", [HANDLE])
    close = bind(library, "PdhCloseQuery", [HANDLE])
    values = bind(library, "PdhGetFormattedCounterArrayW", [HANDLE, U32, c.POINTER(U32),
                                                            c.POINTER(U32), HANDLE])
    query = HANDLE()
    status = open_query(None, 0, c.byref(query))
    if status != 0:
        return {"available": False, "open_status": status}
    try:
        counters = {}
        for name in ["% Processor Performance", "Processor Frequency"]:
            counter = HANDLE()
            status = add(query, "\\Processor Information(*)\\" + name, 0, c.byref(counter))
            counters[name] = (counter, status)
        collect(query)
        time.sleep(interval)
        status = collect(query)
        if status != 0:
            return {"available": False, "collection_status": status}
        result = {}
        for name, (counter, status) in counters.items():
            if status != 0:
                result[name] = {"available": False, "add_status": status}
                continue
            size, count = U32(), U32()
            status = values(counter, 0x8200, c.byref(size), c.byref(count), None)
            if status not in (0, PDH_MORE_DATA) or not 0 < size.value <= 1_048_576:
                result[name] = {"available": False, "size_status": status}
                continue
            buffer = c.create_string_buffer(size.value)
            status = values(counter, 0x8200, c.byref(size), c.byref(count), buffer)
            if status != 0 or count.value * c.sizeof(CounterItem) > len(buffer):
                result[name] = {"available": False, "read_status": status}
                continue
            items = c.cast(buffer, c.POINTER(CounterItem))
            readings = {}
            for index in range(count.value):
                item = items[index]
                if item.name and re.fullmatch(r"\d+,\d+", item.name):
                    readings[item.name] = {"status": item.data.status,
                                          "value": item.data.value if item.data.status in (0, 1) else None}
            result[name] = {"available": bool(readings), "logical_processors": readings}
        return result
    finally:
        close(query)


def nvml_probe(samples, interval):
    library = system_dll("nvml.dll")
    init = bind(library, "nvmlInit_v2", [])
    shutdown = bind(library, "nvmlShutdown", [])
    status = init()
    if status != 0:
        return {"available": False, "init_status": status}
    try:
        count = U32()
        status = bind(library, "nvmlDeviceGetCount_v2", [c.POINTER(U32)])(c.byref(count))
        if status != 0 or count.value > 64:
            return {"available": False, "enumeration_status": status}
        get_handle = bind(library, "nvmlDeviceGetHandleByIndex_v2", [U32, c.POINTER(HANDLE)])
        get_name = bind(library, "nvmlDeviceGetName", [HANDLE, c.c_char_p, U32])
        get_pci = optional(library, "nvmlDeviceGetPciInfo_v3", [HANDLE, c.POINTER(PciInfo)])
        clock = optional(library, "nvmlDeviceGetClockInfo", [HANDLE, U32, c.POINTER(U32)])
        power = optional(library, "nvmlDeviceGetPowerUsage", [HANDLE, c.POINTER(U32)])
        temp_v = optional(library, "nvmlDeviceGetTemperatureV", [HANDLE, c.POINTER(Temperature)])
        temp_old = optional(library, "nvmlDeviceGetTemperature", [HANDLE, U32, c.POINTER(U32)])
        devices = []
        for index in range(count.value):
            handle = HANDLE()
            status = get_handle(index, c.byref(handle))
            if status != 0:
                devices.append({"index": index, "handle_status": status})
                continue
            name = c.create_string_buffer(96)
            name_status = get_name(handle, name, len(name))
            device = {"index": index, "name_status": name_status, "samples": []}
            if name_status == 0:
                device["name"] = name.value.decode("utf-8", "replace")
            if get_pci:
                pci = PciInfo()
                if get_pci(handle, c.byref(pci)) == 0:
                    device["pci_bus_id"] = pci.bus_id.decode("ascii")
            for sample_index in range(samples):
                if sample_index:
                    time.sleep(interval)
                temperature = {"available": False, "reason": "function_missing"}
                if temp_v:
                    reading = Temperature(c.sizeof(Temperature) | (1 << 24), 0, 0)
                    status = temp_v(handle, c.byref(reading))
                    temperature = {"available": status == 0, "status": status, "api": "TemperatureV"}
                    if status == 0:
                        temperature.update(value=reading.temperature, unit="C")
                if not temperature["available"] and temp_old:
                    temperature = scalar(temp_old, [handle, 0], "C")
                    temperature["api"] = "Temperature_legacy"
                device["samples"].append({
                    "temperature": temperature,
                    "graphics_clock": scalar(clock, [handle, 0], "MHz"),
                    "memory_clock": scalar(clock, [handle, 2], "MHz"),
                    "power": scalar(power, [handle], "W", 1000),
                })
            devices.append(device)
        return {"available": True, "devices": devices}
    finally:
        shutdown()


def nvapi_probe(samples, interval, include_private):
    library = system_dll("nvapi64.dll")
    query = library.nvapi_QueryInterface
    query.argtypes = [U32]
    query.restype = HANDLE

    def interface(identifier, arguments):
        address = query(identifier)
        return c.CFUNCTYPE(STATUS, *arguments)(address) if address else None

    init = interface(0x0150E828, [])
    if init is None:
        return {"available": False, "reason": "initialize_missing"}
    status = init()
    if status != 0:
        return {"available": False, "init_status": status}
    unload = interface(0xD22BDD7E, [])
    try:
        enum = interface(0xE5AC921F, [c.POINTER(HANDLE), c.POINTER(U32)])
        if enum is None:
            return {"available": False, "reason": "enumeration_missing"}
        handles = (HANDLE * 64)()
        count = U32()
        status = enum(handles, c.byref(count))
        if status != 0 or count.value > len(handles):
            return {"available": False, "enumeration_status": status}
        bus = interface(0x1BE0B8E5, [HANDLE, c.POINTER(U32)])
        # Private read-only API, pinned to LibreHardwareMonitor's 76-byte V1 layout.
        voltage = interface(0x465F9BCF, [HANDLE, HANDLE]) if include_private else None
        devices = []
        for index in range(count.value):
            device = {"index": index, "pci_bus": scalar(bus, [handles[index]], "bus"), "samples": []}
            for sample_index in range(samples):
                if sample_index:
                    time.sleep(interval)
                value = {"available": False, "reason": "private_query_disabled"}
                if include_private:
                    value = {"available": False, "reason": "function_missing", "private_api": True}
                if voltage:
                    buffer = c.create_string_buffer(0x4C)
                    U32.from_buffer(buffer).value = len(buffer) | (1 << 16)
                    status = voltage(handles[index], buffer)
                    value = {"available": False, "status": status, "private_api": True}
                    if status == 0:
                        microvolts = U32.from_buffer(buffer, 0x28).value | (U32.from_buffer(buffer, 0x2C).value << 32)
                        value["raw_microvolts"] = microvolts
                        if 0 < microvolts <= 3_000_000:
                            value.update(available=True, value=microvolts / 1_000_000, unit="V")
                        else:
                            value["reason"] = "zero_or_implausible_reading"
                device["samples"].append({"core_voltage": value})
            devices.append(device)
        return {"available": True, "devices": devices}
    finally:
        if unload:
            unload()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--samples", type=int, choices=range(1, 11), default=3)
    parser.add_argument("--interval", type=float, default=0.5)
    parser.add_argument("--include-private-nvapi", action="store_true")
    args = parser.parse_args()
    if sys.platform != "win32" or c.sizeof(HANDLE) != 8:
        parser.error("Windows x64 Python is required")
    if not 0.05 <= args.interval <= 5:
        parser.error("interval must be between 0.05 and 5 seconds")
    report = {"captured_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()}
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Services\PawnIO"):
            report["pawnio_service_registered"] = True
    except FileNotFoundError:
        report["pawnio_service_registered"] = False
    except OSError as error:
        report["pawnio_service_check_error"] = str(error)
    for name, probe in [("processor_counters", lambda: processor_counters(args.interval)),
                        ("nvml", lambda: nvml_probe(args.samples, args.interval)),
                        ("nvapi", lambda: nvapi_probe(args.samples, args.interval, args.include_private_nvapi))]:
        try:
            report[name] = probe()
        except (OSError, AttributeError) as error:
            report[name] = {"available": False, "error": str(error)}
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
