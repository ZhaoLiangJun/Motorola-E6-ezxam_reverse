# -*- coding: utf-8 -*-
"""chk_v63.py —— 校验 v63 四项修复是否进入产物（产物侧，不看源码）

build() 返回的调用名是**修饰名**，所以这里用修饰名片段做匹配。
"""
import sys
sys.path.insert(0, '.')
from audit_all import build

OURS = 'libezxam_reconstructed.so.1.0.0'
d = open(OURS, 'rb').read()

print('=== 1. 关键字符串 ===')
for s in (b'AppLockValue\x00', b'AppLockValueChanged()\x00', b'AM/RegistryiMgr/Channel\x00'):
    print('  %-26s %d 处' % (s[:-1].decode(), d.count(s)))

F = build(OURS)

SEND = '_ZN11QCopChannel4sendERK8QCStringS2_'
TARGETS = [
    ('setAppLockValue(QUuid,int)', '_ZN18AM_RegistryManager15setAppLockValueERK5QUuidi',
     [('QCopChannel::send(2参)', SEND),
      ('QUuid::isNull', '_ZNK5QUuid6isNullEv'),
      ('ZConfig::writeEntry(int)', '_ZN7ZConfig10writeEntryERK7QStringS2_i'),
      ('ZConfig::flush', '_ZN7ZConfig5flushEv')],
     []),
    ('slotReloadAppLockValue()', '_ZN18AM_RegistryManager22slotReloadAppLockValueEv',
     [('ZConfig::readNumEntry', '_ZN7ZConfig12readNumEntryERK7QStringS2_i')],
     [('QCopChannel::send(2参)', SEND)]),
    ('AppLockManager ctor', '_ZN17AM_AppLockManagerC2EP7QObjectPKc',
     [('QCopChannel::isRegistered', '_ZN11QCopChannel12isRegisteredERK8QCString'),
      ('QCopChannel::QCopChannel', '_ZN11QCopChannelC1ERK8QCStringP7QObjectPKc'),
      ('QObject::connect', '_ZN7QObject7connectEPKS_PKcS1_S3_')],
     []),
    ('AppLockManager::getAppLockValue', '_ZN17AM_AppLockManager15getAppLockValueERK5QUuid',
     [('getConfigFileName(静态)', '_ZN18AM_RegistryManager17getConfigFileNameENS_11RegFileTypeE'),
      ('ZConfig::readEntry', '_ZN7ZConfig9readEntryERK7QStringS2_S2_'),
      ('operator new(不应再构造RegistryManager)', '_Znwj')],
     []),
]

print('\n=== 2. 逐函数核对「必须有 / 必须没有」的调用 ===')
ok = True
for label, sym, need, forbid in TARGETS:
    if sym not in F:
        print('  [缺失符号] %s' % sym)
        ok = False
        continue
    calls = F[sym][1]
    for human, frag in need:
        got = any(frag == c or frag in c for c in calls)
        note = 'OK' if got else '**缺失**'
        if human.startswith('operator new'):
            note = '**仍在 new**' if got else 'OK(已去掉)'
            got = not got
        print('  %-32s %-38s %s' % (label[:32], human[:38], note))
        ok = ok and got
    for human, frag in forbid:
        got = any(frag == c or frag in c for c in calls)
        print('  %-32s 禁止 %-33s %s' % (label[:32], human[:33], '**出现了**' if got else 'OK'))
        ok = ok and not got

print('\n=== 3. #1「状态跟随」的修复是否在产物里（AM_Carrier_Info 构造） ===')
CTOR = '_ZN15AM_Carrier_InfoC2EP7QObject'
CONNECT = '_ZN7QObject7connectEPKS_PKcS1_S3_'
SLOTUP = '_ZN15AM_Carrier_Info20slotUpdateInfoStringEv'
if CTOR not in F:
    print('  [缺失符号] %s' % CTOR)
    ok = False
else:
    eff, calls, thisw, news = F[CTOR]
    nconn = sum(1 for c in calls if c == CONNECT)
    nslot = sum(1 for c in calls if c == SLOTUP)
    for human, want, got in (
            ('connect 条数', 5, nconn),
            ('末尾 slotUpdateInfoString', 1, nslot),
            ('operator new 尺寸', 40, max(news) if news else 0)):
        good = (got == want)
        print('  %-28s 期望 %-4d 实际 %-4d %s' % (human, want, got, 'OK' if good else '**不符**'))
        ok = ok and good
    # 5 条 connect 必须挂在同一个 sender 上（m_pMsgClient），否则等于没接
    print('  %-28s this 写入偏移 %s' % ('空壳成员布局', sorted(thisw)))

print('\n=== 4. connect 的 5 个信号名是否都在（字符串级） ===')
for s in (b'2signalServiceName(QString)\x00',
          b'2signalSpnNameExtend(SERVICE_PROVIDER_DATA_S)\x00',
          b'2signalNetworkStatus(TAPI_NETWORK_STATUS_E)\x00',
          b'2signalServiceAvailability(TAPI_BOOL_STATUS_E)\x00',
          b'2signalSetRFModResult(bool)\x00',
          b'1slotUpdateInfoString()\x00'):
    n = d.count(s)
    print('  %-50s %d 处 %s' % (s[:-1].decode()[:50], n, 'OK' if n >= 1 else '**缺失**'))
    ok = ok and n >= 1

print('\n结论: %s' % ('PASS' if ok else 'FAIL'))
sys.exit(0 if ok else 1)
