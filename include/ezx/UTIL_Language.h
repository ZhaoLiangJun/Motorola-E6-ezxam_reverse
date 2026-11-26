#ifndef __UTIL_LANGUAGE_H__
#define __UTIL_LANGUAGE_H__

#ifndef __cplusplus
#error "This is a C++ header file; it requires C++ to compile."
#endif

//--------------------------------------------------------------------------------------------------
//
//   Header Name: UTIL_Language.h
//
//   General Description: system language api
//
//--------------------------------------------------------------------------------------------------
//
//                            Motorola Confidential Proprietary
//                   Template ID and version: IL93-TMP-01-0107  Version 1.10
//                    (c) Copyright Motorola 2002-2003, All Rights Reserved
//
//
// Revision History:
//                             Modification     Tracking
// Author (core ID)                Date          Number     Description of Changes
// -------------------------   ------------    ----------   ----------------------------------------
// Wang Mingju/w20128            03/20/2003     LIBbb88079  Created
//
//
//
//--------------------------------------------------------------------------------------------------
//                                       INCLUDE FILES
//--------------------------------------------------------------------------------------------------

#include <unistd.h>
#include <qstring.h>

class UTIL_Language
{
public:
    // language id define value
    typedef enum LanguageId
    {
	TD_LNGID_ENGLISH = 0,    // English
	TD_LNGID_GERMAN,         // German
	TD_LNGID_DANISH,         // Danish
	TD_LNGID_FRENCH,         // French
	TD_LNGID_ITALIAN,        // Italian
	TD_LNGID_SPANISH,        // Spanish
	TD_LNGID_SWEDISH,        // Swedish
	TD_LNGID_DUTCH,          // Dutch
	TD_LNGID_PORTUGUESE,     // Portuguese
	TD_LNGID_FINNISH,        // Finnish
	TD_LNGID_NORWEGIAN,      // Norwegian
	TD_LNGID_GREEK,          // Greek
	TD_LNGID_HUNGARIAN,      // Hungarian
	TD_LNGID_TURKISH,        // Turkish
	TD_LNGID_POLISH,         // Polish
	TD_LNGID_THAI,           // Thai
	TD_LNGID_VIETNAMESE,     // Vietnamese
	TD_LNGID_MALAY,          // Malay
	TD_LNGID_INDONESIAN,     // Indonesian
	TD_LNGID_TAGALOG,        // Tagalog
	TD_LNGID_CHINESE,        // Simplified Chinese
	TD_LNGID_CZECH,          // Czech
	TD_LNGID_TRAD_CHINESE,   // Traditional Chinese
	TD_LNGID_BULGARIAN,      // Bulgarian
	TD_LNGID_CROATIAN,       // Croatian
	TD_LNGID_ESTONIAN,       // Estonian
	TD_LNGID_LATVIAN,        // Latvian
	TD_LNGID_LITHUANIAN,     // Lithuanian
	TD_LNGID_ROMANIAN,       // Romanian
	TD_LNGID_RUSSIAN,        // Russian
	TD_LNGID_SERBIAN,        // Serbian
	TD_LNGID_SLOVAK,         // Slovak
	TD_LNGID_SLOVENIAN,      // Slovenian
	TD_LNGID_UKRAINIAN,      // Ukrainian
	TD_LNGID_HEBREW,         // Hebrew
	TD_LNGID_ARABIC,         // Arabic

	// Add new language ID values above this line
	TD_LNGID_NUM_OF_LANGUAGES, // number of languages with HSC language ID values
	TD_LNGID_INVALID = TD_LNGID_NUM_OF_LANGUAGES,  // invalid language id value

	TD_LNGID_AUTOMATIC = 0xF0,    // reserved for automatic language selection setting

	TD_LNGID_CURRENT_LANG = 0xFE  // equivalent to 'current language'
    };

    typedef struct
    {
	int		mccNumber;
	QString		mccText;
	QString 	languageCode;
	LanguageId	languageId;
    } MCC;

    static LanguageId getSystemLanguageId();
    static QString  getSystemLanguageCode();
    static QString  getSystemLanguageString();

    /* v67：原库 `_ZN13UTIL_Language22getLanguageIdByLanCodeE7QString`（UND，运行期由
       libezxutil 解析）。判据（tools: dis4.py 于 0x41aeba7c）：调用点只设了 r0
       （`mov r0,r6` 后直接 bl），r1 未设 ⇒ **static**，r0 就是那个 QString 参数。
       返回 TD_LNGID_*：20=简体中文、22=繁体中文、0=英文（见下方枚举）。 */
    static LanguageId getLanguageIdByLanCode( QString code );

    static LanguageId getLanguageIdByMcc( int mcc );
    static QString  getLanguageCodeByMcc(int mcc);

protected:

private:
    static const MCC mccMapTable[];

};

#endif // __UTIL_Language_H__
