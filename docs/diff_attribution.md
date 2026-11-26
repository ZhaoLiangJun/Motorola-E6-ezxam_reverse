# 85 个外部引用差异归因报告

归因目标: 85 个原库引用但重建库未引用的符号


## 按类/模块分组


### (C函数) (12 个)

- `__cxa_finalize`
  - 调用者: __cxa_finalize
- `chown`
  - 调用者: changeFileAttr, chown
- `fclose`
  - 调用者: fclose, getAllAppList
- `fopen`
  - 调用者: fopen, getAllAppList
- `malloc`
  - 调用者: malloc, parseSignedFile
- `memcpy`
  - 调用者: memcpy, parseSignedFile
- `mktime`
  - 调用者: createUuid, mktime
- `printf`
  - 调用者: printf, slotHotPlugForceUpdate
- `rand`
  - 调用者: createUuid, rand, srand
- `srand`
  - 调用者: createUuid, srand
- `strcpy`
  - 调用者: AM_RegistryManager_getAppParentFolderUid, strcpy
- `sync`
  - 调用者: addAppToRegistry, sync

### (operator/other) (1 个)

- `_ZeqPKcRK7QString`
  - 调用者: (未在perfunc找到直接文本引用)

### AM_ActionData (1 个)

- `_ZN13AM_ActionData8setValueERK7QStringi`
  - 调用者: AM_ActionData, _AM_ActionData, invoke, setAction, setObject

### AM_Global (4 个)

- `_ZN9AM_Global10getDirSizeERK7QString`
  - 调用者: AM_GroupListScrollView, CMCCFlexBitEnable, addAppToRegistry, appInstalled, getAppWriteAmDir
- `_ZN9AM_Global14getInstCardDirEv`
  - 调用者: AM_GroupListScrollView, CMCCFlexBitEnable, addAppToRegistry, appInstalled, getAppWriteAmDir
- `_ZN9AM_Global16getAppWriteAmDirEv`
  - 调用者: AM_GroupListScrollView, CMCCFlexBitEnable, addAppToRegistry, appInstalled, getAppWriteAmDir
- `_ZN9AM_Global17CMCCFlexBitEnableEv`
  - 调用者: AM_GroupListScrollView, CMCCFlexBitEnable, addAppToRegistry, appInstalled, getAppWriteAmDir

### AM_MimeType (3 个)

- `_ZN11AM_MimeType14getMimeIdByExtERK7QString`
  - 调用者: getMimeIdByExt, getMimeTypeDataEx, getOpenWithApps, instance
- `_ZN11AM_MimeType17getMimeTypeDataExERK7QString`
  - 调用者: getMimeIdByExt, getMimeTypeDataEx, getOpenWithApps, instance
- `_ZN11AM_MimeType8instanceEv`
  - 调用者: getMimeIdByExt, getMimeTypeDataEx, getOpenWithApps, instance

### DMString (2 个)

- `_ZN8DMStringD1Ev`
  - 调用者: GetString, _DMString, getAppAutoStartStatusFromDmt, getInvisiblNetLinkFromDmt, operator_
- `_ZN8DMStringaSEPKc`
  - 调用者: GetString, _DMString, getAppAutoStartStatusFromDmt, getInvisiblNetLinkFromDmt, operator_

### DmtData (4 个)

- `_ZN7DmtDataC1Ev`
  - 调用者: DmtData, GetBoolean, GetString, _DmtData, getAppAutoStartStatusFromDmt
- `_ZN7DmtDataD1Ev`
  - 调用者: DmtData, GetBoolean, GetString, _DmtData, getAppAutoStartStatusFromDmt
- `_ZNK7DmtData10GetBooleanERb`
  - 调用者: DmtData, GetBoolean, GetString, _DmtData, getAppAutoStartStatusFromDmt
- `_ZNK7DmtData9GetStringER8DMString`
  - 调用者: DmtData, GetBoolean, GetString, _DmtData, getAppAutoStartStatusFromDmt

### DmtTreeFactory (3 个)

- `_ZN14DmtTreeFactory10GetSubtreeERK12DmtPrincipalPKcR11JemSmartPtrI7DmtTreeE`
  - 调用者: GetSubtree, Initialize, Uninitialize, getAppAutoStartStatusFromDmt, getInvisiblNetLinkFromDmt
- `_ZN14DmtTreeFactory10InitializeEv`
  - 调用者: GetSubtree, Initialize, Uninitialize, getAppAutoStartStatusFromDmt, getInvisiblNetLinkFromDmt
- `_ZN14DmtTreeFactory12UninitializeEv`
  - 调用者: GetSubtree, Initialize, Uninitialize, getAppAutoStartStatusFromDmt, getInvisiblNetLinkFromDmt

### QBoxLayout (1 个)

- `_ZN10QBoxLayout9addWidgetEP7QWidgetii`
  - 调用者: addWidget, postInit

### QCString (1 个)

- `_ZN8QCStringpLEPKc`
  - 调用者: AM_AppLockManager, AM_RegistryManager, QCString, QCopChannel, ShowMainMenu

### QDate (3 个)

- `_ZNK5QDate3dayEv`
  - 调用者: createUuid, currentDateTime, day, month, year
- `_ZNK5QDate4yearEv`
  - 调用者: createUuid, currentDateTime, day, month, year
- `_ZNK5QDate5monthEv`
  - 调用者: createUuid, currentDateTime, day, month, year

### QDateTime (1 个)

- `_ZN9QDateTime15currentDateTimeEv`
  - 调用者: createUuid, currentDateTime

### QDir (1 个)

- `_ZNK4QDir9entryListERK7QStringii`
  - 调用者: QDir, _QDir, absPath, cleanDirPath, count

### QFile (2 个)

- `_ZN5QFile10encodeNameERK7QString`
  - 调用者: QFile, QFileInfo, _QFile, _QFileInfo, addAppToMainMenu
- `_ZN5QFile6existsERK7QString`
  - 调用者: QFile, QFileInfo, _QFile, _QFileInfo, addAppToMainMenu

### QFileInfo (1 个)

- `_ZNK9QFileInfo8filePathEv`
  - 调用者: QFileInfo, _QFileInfo, copyTextFile, createConfigFile, dirPath

### QGCache (1 个)

- `_ZN7QGCache13remove_stringERK7QString`
  - 调用者: AM_AppLockManager, QGCache, _AM_AppLockManager, _QCache, _QGCache

### QGList (1 个)

- `_ZN6QGListaSERKS_`
  - 调用者: AM_GroupListScrollView, QGList, QGListIterator, _QGList, _QGListIterator

### QListView (1 个)

- `_ZNK9QListView6headerEv`
  - 调用者: AM_GroupListScrollView, AM_GroupListScrollViewItem, QListView, QListViewItemIterator, _AM_GroupListScrollView

### QListViewItem (2 个)

- `_ZN13QListViewItem8moveItemEPS_`
  - 调用者: AM_GroupListScrollView, QListViewItemIterator, _QListViewItemIterator, current, enforceSortOrder
- `_ZN13QListViewItem9itemBelowEv`
  - 调用者: AM_GroupListScrollView, QListViewItemIterator, _QListViewItemIterator, current, enforceSortOrder

### QObject (1 个)

- `_ZN7QObject2trEPKcS1_`
  - 调用者: AM_AppDataInterface, AM_AppLockManager, AM_Carrier_Info, AM_GroupListScrollView, AM_RegistryManager

### QPixmap (3 个)

- `_ZN7QPixmapC1ERK7QStringPKcNS_9ColorModeE`
  - 调用者: QPixmap, _QPixmap, backgroundPixmapChange, getAnimationIcon, getBigIcon
- `_ZN7QPixmapC1ERKS_`
  - 调用者: QPixmap, _QPixmap, backgroundPixmapChange, getAnimationIcon, getBigIcon
- `_ZN7QPixmapaSERKS_`
  - 调用者: QPixmap, _QPixmap, backgroundPixmapChange, getAnimationIcon, getBigIcon

### QRegExp (2 个)

- `_ZN7QRegExpC1ERK7QStringbb`
  - 调用者: QRegExp, _QRegExp, getAppNameByFullPath, replace
- `_ZN7QRegExpD1Ev`
  - 调用者: QRegExp, _QRegExp, getAppNameByFullPath, replace

### QString (5 个)

- `_ZN7QString7replaceERK7QRegExpRKS_`
  - 调用者: AM_AppLnk, AM_AppObject, AM_Carrier_Info, AM_EmbeddedLink, AM_GroupListScrollView
- `_ZN7QStringaSEPKc`
  - 调用者: AM_AppLnk, AM_AppObject, AM_Carrier_Info, AM_EmbeddedLink, AM_GroupListScrollView
- `_ZNK7QString3argERKS_i`
  - 调用者: AM_AppLnk, AM_AppObject, AM_Carrier_Info, AM_EmbeddedLink, AM_GroupListScrollView
- `_ZNK7QString3midEjj`
  - 调用者: AM_AppLnk, AM_AppObject, AM_Carrier_Info, AM_EmbeddedLink, AM_GroupListScrollView
- `_ZNK7QString4findERKS_ib`
  - 调用者: AM_AppLnk, AM_AppObject, AM_Carrier_Info, AM_EmbeddedLink, AM_GroupListScrollView

### QStringList (2 个)

- `_ZN11QStringList5splitERK5QCharRK7QStringb`
  - 调用者: addAppToMainMenu, changeFolder, changeOrder, createObject, deleteFolder
- `_ZN11QStringList5splitERK7QStringS2_b`
  - 调用者: addAppToMainMenu, changeFolder, changeOrder, createObject, deleteFolder

### QTextStream (5 个)

- `_ZN11QTextStream8readLineEv`
  - 调用者: QTextStream, _QTextStream, copyTextFile, operator__, readLine
- `_ZN11QTextStreamC1EP9QIODevice`
  - 调用者: QTextStream, _QTextStream, copyTextFile, operator__, readLine
- `_ZN11QTextStreamD1Ev`
  - 调用者: QTextStream, _QTextStream, copyTextFile, operator__, readLine
- `_ZN11QTextStreamlsERK7QString`
  - 调用者: QTextStream, _QTextStream, copyTextFile, operator__, readLine
- `_ZN11QTextStreamlsEc`
  - 调用者: QTextStream, _QTextStream, copyTextFile, operator__, readLine

### QTime (3 个)

- `_ZNK5QTime4hourEv`
  - 调用者: createUuid, hour, minute, second, timerEvent
- `_ZNK5QTime6minuteEv`
  - 调用者: createUuid, hour, minute, second, timerEvent
- `_ZNK5QTime6secondEv`
  - 调用者: createUuid, hour, minute, second, timerEvent

### QUuid (1 个)

- `_ZN5QUuidC1ERK7QString`
  - 调用者: QUuid, addAppToAutoRun, addAppToRegistry, autoOpenWithAnotherApp, createFolder

### QWidget (5 个)

- `_ZN7QWidget11setGeometryERK5QRect`
  - 调用者: AM_GroupListScrollView, AM_StandardDlg, QDialog, QListView, QVBoxLayout
- `_ZN7QWidget13setFixedWidthEi`
  - 调用者: AM_GroupListScrollView, AM_StandardDlg, QDialog, QListView, QVBoxLayout
- `_ZN7QWidget14setFixedHeightEi`
  - 调用者: AM_GroupListScrollView, AM_StandardDlg, QDialog, QListView, QVBoxLayout
- `_ZN7QWidget9setWFlagsEj`
  - 调用者: AM_GroupListScrollView, AM_StandardDlg, QDialog, QListView, QVBoxLayout
- `_ZN7QWidget9setWStateEj`
  - 调用者: AM_GroupListScrollView, AM_StandardDlg, QDialog, QListView, QVBoxLayout

### SETUP_Utility (4 个)

- `_ZN13SETUP_Utility10getFlexBitE15SETUP_FLEX_ID_TRb`
  - 调用者: SETUP_Utility, _SETUP_Utility, doAutoSelection, getDefaultName, getFlexBit
- `_ZN13SETUP_Utility18getImsiChangedFlagEv`
  - 调用者: SETUP_Utility, _SETUP_Utility, doAutoSelection, getDefaultName, getFlexBit
- `_ZN13SETUP_UtilityC1Ev`
  - 调用者: SETUP_Utility, _SETUP_Utility, doAutoSelection, getDefaultName, getFlexBit
- `_ZN13SETUP_UtilityD1Ev`
  - 调用者: SETUP_Utility, _SETUP_Utility, doAutoSelection, getDefaultName, getFlexBit

### UTIL_Language (1 个)

- `_ZN13UTIL_Language22getLanguageIdByLanCodeE7QString`
  - 调用者: getLanguageIdByLanCode, getName

### UTIL_PushButton (1 个)

- `_ZN15UTIL_PushButtonC1EP7QWidgetiii`
  - 调用者: UTIL_PushButton, postInit

### ZConfig (4 个)

- `_ZN7ZConfig10clearGroupERK7QString`
  - 调用者: ZConfig, _ZConfig, addAppToAutoRun, addAppToMainMenu, addAppToRegistry
- `_ZN7ZConfig10writeEntryERK7QStringS2_Pc`
  - 调用者: ZConfig, _ZConfig, addAppToAutoRun, addAppToMainMenu, addAppToRegistry
- `_ZN7ZConfig10writeEntryERK7QStringS2_b`
  - 调用者: ZConfig, _ZConfig, addAppToAutoRun, addAppToMainMenu, addAppToRegistry
- `_ZN7ZConfig22getGroupEntriesKeyListERK7QString`
  - 调用者: ZConfig, _ZConfig, addAppToAutoRun, addAppToMainMenu, addAppToRegistry

### ZGlobal (2 个)

- `_ZN7ZGlobal10getGNBRectEv`
  - 调用者: getDesktopRect, getGNBRect, postInit
- `_ZN7ZGlobal14getDesktopRectEv`
  - 调用者: getDesktopRect, getGNBRect, postInit

### ZLanguage (1 个)

- `_ZN9ZLanguage21getSystemLanguageCodeEv`
  - 调用者: getName, getSystemLanguageCode