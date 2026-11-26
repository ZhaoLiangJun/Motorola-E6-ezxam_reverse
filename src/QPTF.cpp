/* QPTF.cpp - PT/F 组件 getPacketId() 虚函数覆盖实现
 * Qt 2.3.8 已在 <qptf.h> 定义 QPTF 类（Q_EXPORT，getPacketId 为虚函数）。
 * 原版 libezxam.so 覆盖了 QPTF::getPacketId()（符号 _ZN4QPTF11getPacketIdEv）。
 * 其余 QPTF 方法（ptf_getState/sendPackage 等）由外部 libqte/libezxptf 提供。
 */
#include <qptf.h>

/* ============ getPacketId（覆盖 Qt 虚函数）============
 * ★v52 修正：旧实现写的是"每次调用自增、首包 id=1"，那是猜的。
 * 原库 @0x41b178f4 只有 8 字节，逐指令就是：
 *     41b178f4  mov r0, #0
 *     41b178f8  bx  lr
 * 即 **恒返回 0**。调用方是 SoftManage 的 PT/F 传输，拿它当协议包序号；
 * 我们原先每调一次就变（1,2,3…），与对端预期不一致 —— 正是那种
 * "看起来很像对的、实际错位"的偏差。这里改回恒 0。
 */
int QPTF::getPacketId()
{
    return 0;
}
