/*√Œ÷–µ˚ŒË
  libezxam.so.1.0.0
*/

#ifndef AM_SYSTEMCONFIG_H
#define AM_SYSTEMCONFIG_H

class AM_SystemConfig
{
public :
  AM_SystemConfig();
  ~AM_SystemConfig();

  int ezxUid();
  int ezxGid();
  int DRMUid();
private :
  int uid,gid,duid;
};

#endif //AM_SYSTEMCONFIG_H