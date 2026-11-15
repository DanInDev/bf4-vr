"""Read-only walk of bf4.exe's committed private read/write memory, for object censuses by vtable.

    python -E -P tools\\heap.py <hex qword>      every 8-aligned address holding that value

The pipe's `scan` stops at 64 matches per memory region (CLAUDE.md "Tool pitfalls"); this reads every
region with VirtualQueryEx + ReadProcessMemory instead (query and read rights only, nothing is written).
"""
import ctypes
import ctypes.wintypes as W
import struct
import subprocess
import sys

_k = ctypes.WinDLL('kernel32', use_last_error=True)


class _MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_uint64), ('AllocationBase', ctypes.c_uint64), ('AllocationProtect', W.DWORD),
                ('_a', W.DWORD), ('RegionSize', ctypes.c_uint64), ('State', W.DWORD), ('Protect', W.DWORD),
                ('Type', W.DWORD), ('_b', W.DWORD)]


_k.VirtualQueryEx.argtypes = [W.HANDLE, ctypes.c_uint64, ctypes.POINTER(_MBI), ctypes.c_size_t]
_k.ReadProcessMemory.argtypes = [W.HANDLE, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
_k.OpenProcess.restype = W.HANDLE

MEM_COMMIT, MEM_PRIVATE = 0x1000, 0x20000
WRITABLE = 0x04 | 0x08 | 0x40  # PAGE_READWRITE, PAGE_WRITECOPY, PAGE_EXECUTE_READWRITE
PAGE_GUARD = 0x100


def _pid():
    out = subprocess.check_output(['powershell', '-NoProfile', '-Command', '(Get-Process bf4).Id'])
    return int(out.strip())


def regions(handle):
    at, info = 0x10000, _MBI()
    while at < 0x7FFFFFFF0000 and _k.VirtualQueryEx(handle, at, ctypes.byref(info), ctypes.sizeof(info)):
        if info.State == MEM_COMMIT and info.Type == MEM_PRIVATE and info.Protect & WRITABLE and not info.Protect & PAGE_GUARD:
            yield info.BaseAddress, info.RegionSize
        at = info.BaseAddress + info.RegionSize


def find_qword(value):
    pattern = struct.pack('<Q', value)
    handle = _k.OpenProcess(0x0400 | 0x0010, False, _pid())  # PROCESS_QUERY_INFORMATION | PROCESS_VM_READ
    if not handle:
        raise OSError(ctypes.get_last_error(), 'OpenProcess(bf4) failed')
    hits = []
    try:
        for base, size in regions(handle):
            if size >= 1 << 31:
                continue
            buffer, got = ctypes.create_string_buffer(size), ctypes.c_size_t()
            if not _k.ReadProcessMemory(handle, base, buffer, size, ctypes.byref(got)):
                continue
            data = buffer.raw[:got.value]
            i = data.find(pattern)
            while i >= 0:
                if i % 8 == 0:
                    hits.append(base + i)
                i = data.find(pattern, i + 1)
    finally:
        _k.CloseHandle(handle)
    return hits


if __name__ == '__main__':
    for hit in find_qword(int(sys.argv[1], 16)):
        print(hex(hit))
