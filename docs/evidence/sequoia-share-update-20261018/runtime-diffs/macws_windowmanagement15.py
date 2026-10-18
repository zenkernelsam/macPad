"""Publish the stock 24G90 WindowManager in macPad's existing GUI domain."""
from pathlib import Path
import argparse, ctypes, json, os, plistlib, subprocess, time

ROOT=Path('/var/mnt/rootfs')
GUI=Path('/var/jb/usr/macOS/gui-launchd')
LABEL='com.macwsguide.windowmanager15'
PLIST=GUI/(LABEL+'.plist')
DOMAIN='user/foreground'
LAUNCHCTL='/var/jb/bin/launchctl'
TARGET='/System/Library/CoreServices/WindowManager.app/Contents/MacOS/WindowManager'
ADAPTER='/usr/local/lib/AppRBSWire15.dylib'

def applicable():
    version=plistlib.load(open(ROOT/'System/Library/CoreServices/SystemVersion.plist','rb'))
    return version.get('ProductVersion')=='15.6.1' and version.get('ProductBuildVersion')=='24G90'

def prepare():
    if not applicable():return False
    image=ROOT/TARGET.lstrip('/')
    if image.is_symlink() or not image.is_file() or image.stat().st_uid!=0:
        raise ValueError('WindowManager main must be the admitted root-owned regular file')
    subprocess.run(['/var/jb/usr/bin/python3','/var/jb/usr/macOS/bin/macws_system_app_prepare.py',TARGET],
                   check=True,timeout=90)
    source=plistlib.load(open(GUI/'com.macwsguide.dock.plist','rb'))
    env=dict(source.get('EnvironmentVariables',{}))
    env['MACWS_EXTRA_INSERT_LIBRARIES']=ADAPTER
    env['MACPAD_APP_RBS15']='1'
    env['MACPAD_WINDOWMANAGEMENT15']='1'
    env['XPC_SERVICE_NAME']=LABEL
    # These are the same private-cache/exec and graphics contracts as Dock.
    # The stock WindowManager owns the transaction; no replacement server.
    job=dict(Label=LABEL,ProgramArguments=[
        '/var/jb/usr/macOS/bin/launchdchrootexec-sequoia-main-20261005',
        '0','0',str(ROOT),TARGET],EnvironmentVariables=env,RunAtLoad=True,
        KeepAlive={'SuccessfulExit':False},ThrottleInterval=10,
        EnablePressuredExit=False,POSIXSpawnType='Interactive',
        MachServices={name:True for name in ('com.apple.windowmanager.server',
            'com.apple.windowmanager.external','com.apple.windowmanager.dragserver')},
        StandardOutPath='/var/jb/var/mobile/windowmanager15.log',
        StandardErrorPath='/var/jb/var/mobile/windowmanager15.log')
    temporary=PLIST.with_name(PLIST.name+'.new-'+str(os.getpid()))
    temporary.write_bytes(plistlib.dumps(job));temporary.chmod(0o644)
    os.replace(temporary,PLIST)
    return True

def loaded_pid():
    r=subprocess.run([LAUNCHCTL,'print',DOMAIN+'/'+LABEL],text=True,capture_output=True,timeout=10)
    if r.returncode:return None
    for line in r.stdout.splitlines():
        if line.strip().startswith('pid = '):
            pid=int(line.split('=',1)[1].strip())
            lib=ctypes.CDLL('/usr/lib/libproc.dylib')
            lib.proc_pidpath.argtypes=[ctypes.c_int,ctypes.c_void_p,ctypes.c_uint32]
            buf=ctypes.create_string_buffer(4096)
            if lib.proc_pidpath(pid,buf,len(buf))<=0:raise ValueError('WindowManager PID path unavailable')
            actual=os.path.realpath(os.fsdecode(buf.value))
            helper=os.path.realpath('/var/jb/usr/macOS/bin/launchdchrootexec-sequoia-main-20261005')
            if actual==helper:return 0  # observed launchd pre-exec interval
            if actual!=os.path.realpath(str(ROOT)+TARGET):
                # launchd may report the shell/interpreter during the short
                # chroot helper to target exec transition. Recheck until the
                # bounded startup deadline rather than failing this generation.
                return 0
            return pid
    return 0

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=('prepare','start','stop'))
    args=parser.parse_args()
    if os.geteuid()!=0:raise ValueError('root required')
    if args.action=='stop':
        subprocess.run([LAUNCHCTL,'bootout',DOMAIN+'/'+LABEL],capture_output=True,timeout=15)
        return
    if not prepare():return
    if args.action=='prepare':return
    pid=loaded_pid()
    if pid is None:
        subprocess.run([LAUNCHCTL,'bootstrap',DOMAIN,str(PLIST)],check=True,timeout=15)
    elif not pid:
        subprocess.run([LAUNCHCTL,'kickstart',DOMAIN+'/'+LABEL],check=True,timeout=15)
    deadline=time.monotonic()+10
    while time.monotonic()<deadline:
        pid=loaded_pid()
        if pid:
            print('WINDOWMANAGEMENT15 '+json.dumps(dict(pid=pid,domain='user/501',
                executable=TARGET,client_route='88->501 for this service only')),flush=True)
            return
        time.sleep(.2)
    raise ValueError('WindowManager did not publish its original process')

if __name__=='__main__':main()
