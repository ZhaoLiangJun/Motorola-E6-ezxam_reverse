/*√Œ÷–µ˚ŒË
libezxam.so.1.0.0
*/

#ifndef AM_ACTIONDATA_H
#define AM_ACTIONDATA_H

#include <quuid.h>
#include <qstring.h>

class AM_ActionData
{
public :
  AM_ActionData();
  ~AM_ActionData();

  void setOpenWithSystemApp();
  void setAction(QString const&);
  void setObject(QString const&);
  void setValue(QString const&, QString const&);
  void setValue(QString const&, int);
  void setOpenApp(QUuid);
  void invoke() const;
private :
  char buffer[128];
};

#endif //AM_ACTIONDATA_H