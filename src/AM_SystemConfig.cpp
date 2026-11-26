/* AM_SystemConfig.cpp - 逆向重建自 libezxam.so.1.0.0 */
#include <AM_SystemConfig.h>

/* 构造函数: 初始化 uid=2000, gid=233, duid=518 */
AM_SystemConfig::AM_SystemConfig()
{
    uid = 2000;   // ezxUid
    gid = 233;    // ezxGid (0xE9)
    duid = 518;   // DRMUid (0x206)
}

/* 析构函数 */
AM_SystemConfig::~AM_SystemConfig()
{
}

/* 获取 EZX 用户 ID */
int AM_SystemConfig::ezxUid()
{
    return uid;
}

/* 获取 EZX 组 ID */
int AM_SystemConfig::ezxGid()
{
    return gid;
}

/* 获取 DRM 用户 ID */
int AM_SystemConfig::DRMUid()
{
    return duid;
}
