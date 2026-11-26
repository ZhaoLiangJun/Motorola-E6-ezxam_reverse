/*
  foxe6
*/
#ifndef _AMLAUNCHERINTERFACE_H_
#define _AMLAUNCHERINTERFACE_H_

#include <quuid.h>
#include <qcstring.h>
#include <qstring.h>
#include <qvaluelist.h>
#include <AM_ActionData.h>

/*********************************************************************************************
  AM_LauncherInterface
*********************************************************************************************/

class AM_LauncherInterface: public QObject
{
    Q_OBJECT
public:
    AM_LauncherInterface(QObject*, const char*);
	virtual ~AM_LauncherInterface();

    static int autoOpenWithSave(const QString&, bool); // ����ֵ����1
    static int launchAppByVoice(const QUuid&, const QString&, bool); // ����1
    static int launchAppWithUid(const QString&, const QString&, bool); // ����1
    static int sendBridgeMessage(const QUuid&, const QByteArray&, bool); // ����1
    static int sendBridgeMessage(const QUuid&, const QCString&, const QByteArray&, bool); // ����1
    static QCString getLauncherChannel(void);
    static void launchAppWithUidGid(const QString&, unsigned, unsigned);
    static void doLaunchAppWithUidGid(const QString&, unsigned, unsigned, int, int);
    static int autoOpenWithAnotherApp(const QUuid&, const QString&, bool); // ����1
    static int launchAppIgnorePhoneLock(const QUuid&, const QString&, bool); // ����1
    static int sendBridgeMessageWithoutRaise(QUuid, const QCString&, const QByteArray&, bool); // ����1
    static int autoOpen(const QString&, bool); // ����1
    static int launchApp(const QUuid&, const QString&, bool); // ����1
    static int launchApp(const QUuid&, bool); // ����1

    virtual int launchAppWithEnv(const QUuid&, const QValueList<QCString>&) const; // ֱ�ӷ���-1
    virtual int active(const AM_ActionData&) const; // ����AM_ActionData.invoke
protected:
	
private:
};
#endif
