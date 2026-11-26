/* qt_preinclude.h - unified precompiled header
 *
 * genheader.sh generated EZX headers are missing Qt base includes
 * (e.g. AM_LauncherInterface inherits QObject without including qobject.h).
 * The original project used a unified .pro/precompiled header.
 * This file is force-injected via -include to fix all headers with zero
 * modifications to them.
 *
 * NOTE: Qt 2.3.8 provides QByteArray in qarray.h. There is no
 *       qvaluevector.h / qptr*.h (use qvaluelist.h / qlist.h instead).
 */
#ifndef QT_PREINCLUDE_H
#define QT_PREINCLUDE_H

#include <qglobal.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qcstring.h>
#include <qarray.h>
#include <qobject.h>
#include <qlist.h>
#include <qmap.h>
#include <qvaluelist.h>
#include <quuid.h>
#include <qpixmap.h>
#include <qimage.h>
#include <qbitmap.h>
#include <qmovie.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qdialog.h>
#include <qlayout.h>
#include <qlabel.h>
#include <qpushbutton.h>
#include <qcheckbox.h>
#include <qlistview.h>
#include <qlistbox.h>
#include <qwidget.h>
#include <qscrollview.h>
#include <qpopupmenu.h>
#include <qcursor.h>
#include <qfont.h>
#include <qcolor.h>
#include <qpoint.h>
#include <qsize.h>
#include <qrect.h>
#include <qevent.h>
#include <qapplication.h>
#include <qcopchannel_qws.h>
#include <qthread.h>
#include <qdatetime.h>
#include <qregexp.h>
#include <qtextstream.h>
#include <qdatastream.h>
#include <qbuffer.h>
#include <qfile.h>
#include <qprocess.h>
#include <qconfig.h>
#include <qnamespace.h>
#include <qcache.h>
#include <qglist.h>
#include <qgvector.h>

#endif /* QT_PREINCLUDE_H */
