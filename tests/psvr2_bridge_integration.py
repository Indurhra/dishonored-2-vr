"""Uses a fake Toolkit DLL in an isolated test directory. No hardware calls."""
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

folder = Path(sys.argv[1]).resolve()
k = C.WinDLL('kernel32',use_last_error=True)
u = C.WinDLL('user32',use_last_error=True)
k.CreateFileMappingW.argtypes=[W.HANDLE,C.c_void_p,W.DWORD,W.DWORD,W.DWORD,W.LPCWSTR]
k.CreateFileMappingW.restype=W.HANDLE
k.MapViewOfFile.argtypes=[W.HANDLE,W.DWORD,W.DWORD,W.DWORD,C.c_size_t]
k.MapViewOfFile.restype=C.c_void_p
k.UnmapViewOfFile.argtypes=[C.c_void_p]
k.CloseHandle.argtypes=[W.HANDLE]
k.GetTickCount64.restype=C.c_uint64
fake=C.CDLL(str(folder/'psvr2_toolkit_capi_loader.dll'))
fake.test_exchange64.argtypes=[C.POINTER(C.c_int64),C.c_int64]
fake.test_exchange64.restype=None
u.GetForegroundWindow.restype=W.HWND
u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]

class Shared(C.Structure):
    _fields_=[('magic',W.LONG),('version',W.LONG),('desired',C.c_int64),
              ('stop',W.LONG),('status',W.LONG),('result',W.LONG),('count',W.LONG)]

def publish(s,profile,hand=0):
    word=((k.GetTickCount64() & 0xffffffff)<<32) | (hand<<8) | profile
    ptr=C.cast(C.byref(s,Shared.desired.offset),C.POINTER(C.c_int64))
    fake.test_exchange64(ptr,C.c_int64(word).value)

def wait_for(fn,timeout=3):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        if fn(): return
        time.sleep(.025)
    raise AssertionError('Timed out waiting for expected fake-helper behavior')

def run_case(fail=False,parent_exit=False):
    name='Local\\DVRPsvr2_'+uuid.uuid4().hex
    mapping=k.CreateFileMappingW(W.HANDLE(-1),None,4,0,C.sizeof(Shared),name)
    assert mapping
    view=k.MapViewOfFile(mapping,0xf001f,0,0,C.sizeof(Shared))
    assert view
    s=Shared.from_address(view);s.magic=0x32525644;s.version=1
    log=folder/('parent-exit.log' if parent_exit else 'failure.log' if fail else 'normal.log')
    log.write_text('')
    env=dict(os.environ,DVRPSVR2_FAKE_LOG=str(log))
    if fail: env['DVRPSVR2_FAKE_FAIL']='1'
    else: env.pop('DVRPSVR2_FAKE_FAIL',None)
    # The test helper is separately compiled with a simulated focus check.
    # The shipped helper always uses the real foreground-process check.
    parent = subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)'],creationflags=subprocess.CREATE_NO_WINDOW) if parent_exit else None
    process=subprocess.Popen([str(folder/'dvr_psvr2_bridge_test.exe'),name,str(parent.pid if parent else os.getpid())],env=env,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    def lines(): return log.read_text().splitlines()
    try:
        wait_for(lambda:s.status==2)
        publish(s,1)
        wait_for(lambda:'set 0 2' in lines())
        if parent_exit:
            parent.terminate();parent.wait(timeout=3)
            process.wait(timeout=3)
            assert process.returncode==0
            assert lines()[-2:]==['set 0 0','deinit -1 -1']
        elif fail:
            process.wait(timeout=3)
            assert process.returncode==5 and s.status==8
            assert lines()[-2:]==['set 0 0','deinit -1 -1']
        else:
            before=len(lines())
            # Fresh heartbeats must not repeatedly actuate unchanged effects.
            for _ in range(6): publish(s,1);time.sleep(.04)
            assert len(lines())==before
            # Publishing stops: the separate process expires the effect.
            wait_for(lambda:lines()[-1]=='set 0 0')
            publish(s,2)
            wait_for(lambda:lines()[-1]=='set 0 3')
            publish(s,1,1) # invalid/right-hand request must fail closed
            wait_for(lambda:lines()[-1]=='set 0 0')
            assert all(not x.startswith('set 1 ') and not x.startswith('set 2 ') for x in lines())
            s.stop=1
            process.wait(timeout=3)
            assert process.returncode==0
            assert lines()[-2:]==['set 0 0','deinit -1 -1']
    finally:
        if process.poll() is None:
            s.stop=1
            try: process.wait(timeout=3)
            except subprocess.TimeoutExpired: process.kill();process.wait()
        del s
        k.UnmapViewOfFile(view);k.CloseHandle(mapping)
        if parent is not None and parent.poll() is None: parent.terminate();parent.wait(timeout=3)

run_case()
run_case(True)
run_case(parent_exit=True)
print('Helper integration passed: real IPC, deduplication, expiry, hand reset, stop, parent exit, apply-failure reset (fake Toolkit only)')
