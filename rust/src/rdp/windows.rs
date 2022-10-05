/* Copyright (C) 2019 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

// Author: Zach Kelly <zach.kelly@lmco.com>

/// converts a locale identifier into a locale name
/// <https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-lcid/70feba9f-294e-491e-b6eb-56532684c37f>
pub fn lcid_to_string<'a>(lcid: u32, default: &'a str) -> String {
    let s = match lcid {
        0x0001 => "ar",
        0x0002 => "bg",
        0x0003 => "ca",
        0x0004 => "zh-Hans",
        0x0005 => "cs",
        0x0006 => "da",
        0x0007 => "de",
        0x0008 => "el",
        0x0009 => "en",
        0x000A => "es",
        0x000B => "fi",
        0x000C => "fr",
        0x000D => "he",
        0x000E => "hu",
        0x000F => "is",
        0x0010 => "it",
        0x0011 => "ja",
        0x0012 => "ko",
        0x0013 => "nl",
        0x0014 => "no",
        0x0015 => "pl",
        0x0016 => "pt",
        0x0017 => "rm",
        0x0018 => "ro",
        0x0019 => "ru",
        0x001A => "hr",
        0x001B => "sk",
        0x001C => "sq",
        0x001D => "sv",
        0x001E => "th",
        0x001F => "tr",
        0x0020 => "ur",
        0x0021 => "id",
        0x0022 => "uk",
        0x0023 => "be",
        0x0024 => "sl",
        0x0025 => "et",
        0x0026 => "lv",
        0x0027 => "lt",
        0x0028 => "tg",
        0x0029 => "fa",
        0x002A => "vi",
        0x002B => "hy",
        0x002C => "az",
        0x002D => "eu",
        0x002E => "hsb",
        0x002F => "mk",
        0x0030 => "st",
        0x0031 => "ts",
        0x0032 => "tn",
        0x0033 => "ve",
        0x0034 => "xh",
        0x0035 => "zu",
        0x0036 => "af",
        0x0037 => "ka",
        0x0038 => "fo",
        0x0039 => "hi",
        0x003A => "mt",
        0x003B => "se",
        0x003C => "ga",
        0x003D => "yi",
        0x003E => "ms",
        0x003F => "kk",
        0x0040 => "ky",
        0x0041 => "sw",
        0x0042 => "tk",
        0x0043 => "uz",
        0x0044 => "tt",
        0x0045 => "bn",
        0x0046 => "pa",
        0x0047 => "gu",
        0x0048 => "or",
        0x0049 => "ta",
        0x004A => "te",
        0x004B => "kn",
        0x004C => "ml",
        0x004D => "as",
        0x004E => "mr",
        0x004F => "sa",
        0x0050 => "mn",
        0x0051 => "bo",
        0x0052 => "cy",
        0x0053 => "km",
        0x0054 => "lo",
        0x0055 => "my",
        0x0056 => "gl",
        0x0057 => "kok",
        0x0058 => "mni",
        0x0059 => "sd",
        0x005A => "syr",
        0x005B => "si",
        0x005C => "chr",
        0x005D => "iu",
        0x005E => "am",
        0x005F => "tzm",
        0x0060 => "ks",
        0x0061 => "ne",
        0x0062 => "fy",
        0x0063 => "ps",
        0x0064 => "fil",
        0x0065 => "dv",
        0x0066 => "bin",
        0x0067 => "ff",
        0x0068 => "ha",
        0x0069 => "ibb",
        0x006A => "yo",
        0x006B => "quz",
        0x006C => "nso",
        0x006D => "ba",
        0x006E => "lb",
        0x006F => "kl",
        0x0070 => "ig",
        0x0071 => "kr",
        0x0072 => "om",
        0x0073 => "ti",
        0x0074 => "gn",
        0x0075 => "haw",
        0x0076 => "la",
        0x0077 => "so",
        0x0078 => "ii",
        0x0079 => "pap",
        0x007A => "arn",
        0x007C => "moh",
        0x007E => "br",
        0x0080 => "ug",
        0x0081 => "mi",
        0x0082 => "oc",
        0x0083 => "co",
        0x0084 => "gsw",
        0x0085 => "sah",
        0x0086 => "qut",
        0x0087 => "rw",
        0x0088 => "wo",
        0x008C => "prs",
        0x0091 => "gd",
        0x0092 => "ku",
        0x0093 => "quc",
        0x0401 => "ar-SA",
        0x0402 => "bg-BG",
        0x0403 => "ca-ES",
        0x0404 => "zh-TW",
        0x0405 => "cs-CZ",
        0x0406 => "da-DK",
        0x0407 => "de-DE",
        0x0408 => "el-GR",
        0x0409 => "en-US",
        0x040A => "es-ES_tradnl",
        0x040B => "fi-FI",
        0x040C => "fr-FR",
        0x040D => "he-IL",
        0x040E => "hu-HU",
        0x040F => "is-IS",
        0x0410 => "it-IT",
        0x0411 => "ja-JP",
        0x0412 => "ko-KR",
        0x0413 => "nl-NL",
        0x0414 => "nb-NO",
        0x0415 => "pl-PL",
        0x0416 => "pt-BR",
        0x0417 => "rm-CH",
        0x0418 => "ro-RO",
        0x0419 => "ru-RU",
        0x041A => "hr-HR",
        0x041B => "sk-SK",
        0x041C => "sq-AL",
        0x041D => "sv-SE",
        0x041E => "th-TH",
        0x041F => "tr-TR",
        0x0420 => "ur-PK",
        0x0421 => "id-ID",
        0x0422 => "uk-UA",
        0x0423 => "be-BY",
        0x0424 => "sl-SI",
        0x0425 => "et-EE",
        0x0426 => "lv-LV",
        0x0427 => "lt-LT",
        0x0428 => "tg-Cyrl-TJ",
        0x0429 => "fa-IR",
        0x042A => "vi-VN",
        0x042B => "hy-AM",
        0x042C => "az-Latn-AZ",
        0x042D => "eu-ES",
        0x042E => "hsb-DE",
        0x042F => "mk-MK",
        0x0430 => "st-ZA",
        0x0431 => "ts-ZA",
        0x0432 => "tn-ZA",
        0x0433 => "ve-ZA",
        0x0434 => "xh-ZA",
        0x0435 => "zu-ZA",
        0x0436 => "af-ZA",
        0x0437 => "ka-GE",
        0x0438 => "fo-FO",
        0x0439 => "hi-IN",
        0x043A => "mt-MT",
        0x043B => "se-NO",
        0x043D => "yi-Hebr",
        0x043E => "ms-MY",
        0x043F => "kk-KZ",
        0x0440 => "ky-KG",
        0x0441 => "sw-KE",
        0x0442 => "tk-TM",
        0x0443 => "uz-Latn-UZ",
        0x0444 => "tt-RU",
        0x0445 => "bn-IN",
        0x0446 => "pa-IN",
        0x0447 => "gu-IN",
        0x0448 => "or-IN",
        0x0449 => "ta-IN",
        0x044A => "te-IN",
        0x044B => "kn-IN",
        0x044C => "ml-IN",
        0x044D => "as-IN",
        0x044E => "mr-IN",
        0x044F => "sa-IN",
        0x0450 => "mn-MN",
        0x0451 => "bo-CN",
        0x0452 => "cy-GB",
        0x0453 => "km-KH",
        0x0454 => "lo-LA",
        0x0455 => "my-MM",
        0x0456 => "gl-ES",
        0x0457 => "kok-IN",
        0x0458 => "mni-IN",
        0x0459 => "sd-Deva-IN",
        0x045A => "syr-SY",
        0x045B => "si-LK",
        0x045C => "chr-Cher-US",
        0x045D => "iu-Cans-CA",
        0x045E => "am-ET",
        0x045F => "tzm-Arab-MA",
        0x0460 => "ks-Arab",
        0x0461 => "ne-NP",
        0x0462 => "fy-NL",
        0x0463 => "ps-AF",
        0x0464 => "fil-PH",
        0x0465 => "dv-MV",
        0x0466 => "bin-NG",
        0x0467 => "fuv-NG",
        0x0468 => "ha-Latn-NG",
        0x0469 => "ibb-NG",
        0x046A => "yo-NG",
        0x046B => "quz-BO",
        0x046C => "nso-ZA",
        0x046D => "ba-RU",
        0x046E => "lb-LU",
        0x046F => "kl-GL",
        0x0470 => "ig-NG",
        0x0471 => "kr-NG",
        0x0472 => "om-ET",
        0x0473 => "ti-ET",
        0x0474 => "gn-PY",
        0x0475 => "haw-US",
        0x0476 => "la-Latn",
        0x0477 => "so-SO",
        0x0478 => "ii-CN",
        0x0479 => "pap-029",
        0x047A => "arn-CL",
        0x047C => "moh-CA",
        0x047E => "br-FR",
        0x0480 => "ug-CN",
        0x0481 => "mi-NZ",
        0x0482 => "oc-FR",
        0x0483 => "co-FR",
        0x0484 => "gsw-FR",
        0x0485 => "sah-RU",
        0x0486 => "qut-GT",
        0x0487 => "rw-RW",
        0x0488 => "wo-SN",
        0x048C => "prs-AF",
        0x048D => "plt-MG",
        0x048E => "zh-yue-HK",
        0x048F => "tdd-Tale-CN",
        0x0490 => "khb-Talu-CN",
        0x0491 => "gd-GB",
        0x0492 => "ku-Arab-IQ",
        0x0493 => "quc-CO",
        0x0501 => "qps-ploc",
        0x05FE => "qps-ploca",
        0x0801 => "ar-IQ",
        0x0803 => "ca-ES-valencia",
        0x0804 => "zh-CN",
        0x0807 => "de-CH",
        0x0809 => "en-GB",
        0x080A => "es-MX",
        0x080C => "fr-BE",
        0x0810 => "it-CH",
        0x0811 => "ja-Ploc-JP",
        0x0813 => "nl-BE",
        0x0814 => "nn-NO",
        0x0816 => "pt-PT",
        0x0818 => "ro-MD",
        0x0819 => "ru-MD",
        0x081A => "sr-Latn-CS",
        0x081D => "sv-FI",
        0x0820 => "ur-IN",
        0x082C => "az-Cyrl-AZ",
        0x082E => "dsb-DE",
        0x0832 => "tn-BW",
        0x083B => "se-SE",
        0x083C => "ga-IE",
        0x083E => "ms-BN",
        0x0843 => "uz-Cyrl-UZ",
        0x0845 => "bn-BD",
        0x0846 => "pa-Arab-PK",
        0x0849 => "ta-LK",
        0x0850 => "mn-Mong-CN",
        0x0851 => "bo-BT",
        0x0859 => "sd-Arab-PK",
        0x085D => "iu-Latn-CA",
        0x085F => "tzm-Latn-DZ",
        0x0860 => "ks-Deva",
        0x0861 => "ne-IN",
        0x0867 => "ff-Latn-SN",
        0x086B => "quz-EC",
        0x0873 => "ti-ER",
        0x09FF => "qps-plocm",
        0x0C01 => "ar-EG",
        0x0C04 => "zh-HK",
        0x0C07 => "de-AT",
        0x0C09 => "en-AU",
        0x0C0A => "es-ES",
        0x0C0C => "fr-CA",
        0x0C1A => "sr-Cyrl-CS",
        0x0C3B => "se-FI",
        0x0C50 => "mn-Mong-MN",
        0x0C51 => "dz-BT",
        0x0C5F => "tmz-MA",
        0x0C6b => "quz-PE",
        0x1001 => "ar-LY",
        0x1004 => "zh-SG",
        0x1007 => "de-LU",
        0x1009 => "en-CA",
        0x100A => "es-GT",
        0x100C => "fr-CH",
        0x101A => "hr-BA",
        0x103B => "smj-NO",
        0x105F => "tzm-Tfng-MA",
        0x1401 => "ar-DZ",
        0x1404 => "zh-MO",
        0x1407 => "de-LI",
        0x1409 => "en-NZ",
        0x140A => "es-CR",
        0x140C => "fr-LU",
        0x141A => "bs-Latn-BA",
        0x143B => "smj-SE",
        0x1801 => "ar-MA",
        0x1809 => "en-IE",
        0x180A => "es-PA",
        0x180C => "fr-MC",
        0x181A => "sr-Latn-BA",
        0x183B => "sma-NO",
        0x1C01 => "ar-TN",
        0x1C09 => "en-ZA",
        0x1C0A => "es-DO",
        0x1C1A => "sr-Cyrl-BA",
        0x1C3B => "sma-SE",
        0x2001 => "ar-OM",
        0x2009 => "en-JM",
        0x200A => "es-VE",
        0x200C => "fr-RE",
        0x201A => "bs-Cyrl-BA",
        0x203B => "sms-FI",
        0x2401 => "ar-YE",
        0x2409 => "en-029",
        0x240A => "es-CO",
        0x240C => "fr-CD",
        0x241A => "sr-Latn-RS",
        0x243B => "smn-FI",
        0x2801 => "ar-SY",
        0x2809 => "en-BZ",
        0x280A => "es-PE",
        0x280C => "fr-SN",
        0x281A => "sr-Cyrl-RS",
        0x2C01 => "ar-JO",
        0x2C09 => "en-TT",
        0x2C0A => "es-AR",
        0x2C0C => "fr-CM",
        0x2C1A => "sr-Latn-ME",
        0x3001 => "ar-LB",
        0x3009 => "en-ZW",
        0x300A => "es-EC",
        0x300C => "fr-CI",
        0x301A => "sr-Cyrl-ME",
        0x3401 => "ar-KW",
        0x3409 => "en-PH",
        0x340A => "es-CL",
        0x340C => "fr-ML",
        0x3801 => "ar-AE",
        0x3809 => "en-ID",
        0x380A => "es-UY",
        0x380C => "fr-MA",
        0x3c01 => "ar-BH",
        0x3c09 => "en-HK",
        0x3c0A => "es-PY",
        0x3c0C => "fr-HT",
        0x4001 => "ar-QA",
        0x4009 => "en-IN",
        0x400A => "es-BO",
        0x4401 => "ar-Ploc-SA",
        0x4409 => "en-MY",
        0x440A => "es-SV",
        0x4801 => "ar-145",
        0x4809 => "en-SG",
        0x480A => "es-HN",
        0x4C09 => "en-AE",
        0x4C0A => "es-NI",
        0x5009 => "en-BH",
        0x500A => "es-PR",
        0x5409 => "en-EG",
        0x540A => "es-US",
        0x5809 => "en-JO",
        0x580A => "es-419",
        0x5C09 => "en-KW",
        0x5C0A => "es-CU",
        0x6009 => "en-TR",
        0x6409 => "en-YE",
        0x641A => "bs-Cyrl",
        0x681A => "bs-Latn",
        0x6C1A => "sr-Cyrl",
        0x701A => "sr-Latn",
        0x703B => "smn",
        0x742C => "az-Cyrl",
        0x743B => "sms",
        0x7804 => "zh",
        0x7814 => "nn",
        0x781A => "bs",
        0x782C => "az-Latn",
        0x783B => "sma",
        0x7843 => "uz-Cyrl",
        0x7850 => "mn-Cyrl",
        0x785D => "iu-Cans",
        0x785F => "tzm-Tfng",
        0x7C04 => "zh-Hant",
        0x7C14 => "nb",
        0x7C1A => "sr",
        0x7C28 => "tg-Cyrl",
        0x7C2E => "dsb",
        0x7C3B => "smj",
        0x7C43 => "uz-Latn",
        0x7C46 => "pa-Arab",
        0x7C50 => "mn-Mong",
        0x7C59 => "sd-Arab",
        0x7C5C => "chr-Cher",
        0x7C5D => "iu-Latn",
        0x7C5F => "tzm-Latn",
        0x7C67 => "ff-Latn",
        0x7C68 => "ha-Latn",
        0x7C92 => "ku-Arab",
        _ => default,
    };
    String::from(s)
}

/// Windows operating system type (build and suffix/pack)
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct OperatingSystem {
    pub build: Build,
    pub suffix: Suffix,
}

// <https://en.wikipedia.org/wiki/Windows_NT#Releases>
#[derive(Clone, Debug, FromPrimitive, PartialEq, Eq)]
#[allow(non_camel_case_types)]
pub enum Build {
    Other,
    Win31 = 528,
    Win35 = 807,
    Win351 = 1057,
    Win40 = 1381,
    Win2000 = 2195,
    WinXP = 2600,
    Vista_6000 = 6000,
    Vista_6001 = 6001,
    Vista_6002 = 6002,
    Win7_7600 = 7600,
    Win7_7601 = 7601,
    Win8 = 9200,
    Win81 = 9600,
    Win10_10240 = 10240,
    Win10_10586 = 10586,
    Win10_14393 = 14393,
    Win10_15063 = 15063,
    Win10_16299 = 16299,
    Win10_17134 = 17134,
    Win10_17763 = 17763,
    Server2003 = 3790,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Suffix {
    Empty,
    Rtm,
    Sp1,
    Sp2,
    Th1,
    Th2,
    Rs1,
    Rs2,
    Rs3,
    Rs4,
    Rs5,
}

/// convert a build number into an OperatingSystem type
pub fn build_number_to_os(number: u32) -> OperatingSystem {
    let build = match num::FromPrimitive::from_u32(number) {
        Some(x) => x,
        None => Build::Other,
    };
    let suffix = match number {
        6000 => Suffix::Rtm,
        7600 => Suffix::Rtm,
        6001 => Suffix::Sp1,
        6002 => Suffix::Sp2,
        7601 => Suffix::Sp1,
        10240 => Suffix::Th1,
        10586 => Suffix::Th2,
        14393 => Suffix::Rs1,
        15063 => Suffix::Rs2,
        16299 => Suffix::Rs3,
        17134 => Suffix::Rs4,
        17763 => Suffix::Rs5,
        _ => Suffix::Empty,
    };
    OperatingSystem { build, suffix }
}

/// convert an OperatingSystem into a string description
pub fn os_to_string<'a>(os: &OperatingSystem, default: &'a str) -> String {
    let s = match os.build {
        Build::Win31 => "Windows NT 3.1",
        Build::Win35 => "Windows NT 3.5",
        Build::Win351 => "Windows NT 3.51",
        Build::Win40 => "Windows NT 4.0",
        Build::Win2000 => "Windows 2000",
        Build::WinXP => "Windows XP",
        Build::Vista_6000 => "Windows Vista",
        Build::Vista_6001 => "Windows Vista",
        Build::Vista_6002 => "Windows Vista",
        Build::Win7_7600 => "Windows 7",
        Build::Win7_7601 => "Windows 7",
        Build::Win8 => "Windows 8",
        Build::Win81 => "Windows 8.1",
        Build::Win10_10240 => "Windows 10",
        Build::Win10_10586 => "Windows 10",
        Build::Win10_14393 => "Windows 10",
        Build::Win10_15063 => "Windows 10",
        Build::Win10_16299 => "Windows 10",
        Build::Win10_17134 => "Windows 10",
        Build::Win10_17763 => "Windows 10",
        Build::Server2003 => "Windows Server 2003",
        Build::Other => default,
    };
    let mut result = String::from(s);
    match os.suffix {
        Suffix::Rtm => result.push_str(" RTM"),
        Suffix::Sp1 => result.push_str(" SP1"),
        Suffix::Sp2 => result.push_str(" SP2"),
        Suffix::Th1 => result.push_str(" TH1"),
        Suffix::Th2 => result.push_str(" TH2"),
        Suffix::Rs1 => result.push_str(" RS1"),
        Suffix::Rs2 => result.push_str(" RS2"),
        Suffix::Rs3 => result.push_str(" RS3"),
        Suffix::Rs4 => result.push_str(" RS4"),
        Suffix::Rs5 => result.push_str(" RS5"),
        Suffix::Empty => (),
    };
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_lcid_string_en() {
        let default = "default-lcid-name";
        assert_eq!("en-US", lcid_to_string(0x409, default));
    }

    #[test]
    fn test_lcid_string_default() {
        let default = "default-lcid-name";
        assert_eq!(default, lcid_to_string(0xffff, default));
    }

    #[test]
    fn test_build_os_win10() {
        let w10_rs5 = OperatingSystem {
            build: Build::Win10_17763,
            suffix: Suffix::Rs5,
        };
        assert_eq!(w10_rs5, build_number_to_os(17763));
    }

    #[test]
    fn test_build_os_other() {
        let other = OperatingSystem {
            build: Build::Other,
            suffix: Suffix::Empty,
        };
        assert_eq!(other, build_number_to_os(1));
    }

    #[test]
    fn test_os_string_win7_sp1() {
        let w7_sp1 = "Windows 7 SP1";
        let default = "default-os-name";
        let w7_os = OperatingSystem {
            build: Build::Win7_7601,
            suffix: Suffix::Sp1,
        };
        assert_eq!(w7_sp1, os_to_string(&w7_os, default));
    }

    #[test]
    fn test_os_string_win81() {
        let w81 = "Windows 8.1";
        let default = "default-os-name";
        let w81_os = OperatingSystem {
            build: Build::Win81,
            suffix: Suffix::Empty,
        };
        assert_eq!(w81, os_to_string(&w81_os, default));
    }

    #[test]
    fn test_os_string_default() {
        let default = "default-os-name";
        let other_os = OperatingSystem {
            build: Build::Other,
            suffix: Suffix::Empty,
        };
        assert_eq!(default, os_to_string(&other_os, default));
    }
}
