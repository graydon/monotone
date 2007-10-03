// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "base.hh"
#include "sanity.hh"

#include <windows.h>

struct table_entry
{
  unsigned long key;
  char *val;
};

void key_to_string(unsigned long key, 
                   table_entry *table,
                   std::string & str, 
                   std::string const & def)
{
  while (table->val != 0)
    {
      if (table->key == key) {
        str = std::string(table->val);
        return;
      }
      ++table;
    }
  str = def;
}

static table_entry processor_types[] = {
#ifdef PROCESSOR_ARCHITECTURE_386
  { PROCESSOR_INTEL_386, "i386" },
#endif
#ifdef PROCESSOR_ARCHITECTURE_486
  { PROCESSOR_INTEL_486, "i486" },
#endif
#ifdef PROCESSOR_ARCHITECTURE_PENTIUM
  { PROCESSOR_INTEL_PENTIUM, "pentium" },
#endif
#ifdef PROCESSOR_ARCHITECTURE_PENTIUMII
  { PROCESSOR_INTEL_PENTIUMII, "pentiumII" },
#endif
#ifdef PROCESSOR_ARCHITECTURE_R4000
  { PROCESSOR_INTEL_R4000, "r4000" },
#endif
#ifdef PROCESSOR_ARCHITECTURE_R5000
  { PROCESSOR_INTEL_R5000, "r5000" },
#endif
#ifdef PROCESSOR_HITACHI_SH3
  { PROCESSOR_HITACHI_SH3, "sh3" },
#endif
#ifdef PROCESSOR_HITACHI_SH4
  { PROCESSOR_HITACHI_SH4, "sh4" },
#endif
#ifdef PROCESSOR_STRONGARM
  { PROCESSOR_STRONGARM, "strongarm" },
#endif
#ifdef PROCESSOR_ARM720
  { PROCESSOR_ARM720, "arm720" },
#endif
#ifdef PROCESSOR_SHx_SH3DSP
  { PROCESSOR_SHx_SH3DSP, "sh3dsp" },
#endif
#ifdef PROCESSOR_AMD_X8664
  { PROCESSOR_AMD_X8664, "amd64" },
#endif
  { 0, 0 }
};


static table_entry processors[] = {
#ifdef PROCESSOR_ARCHITECTURE_INTEL
  { PROCESSOR_ARCHITECTURE_INTEL, "ia32" },  
#endif
#ifdef PROCESSOR_ARCHITECTURE_IA64
  { PROCESSOR_ARCHITECTURE_IA64, "ia64" },
#endif
#ifdef PROCESSOR_ARCHITECTURE_AMD64
  { PROCESSOR_ARCHITECTURE_AMD64, "amd64" },
#endif
#ifdef PROCESSOR_ARCHITECTURE_ALPHA
  { PROCESSOR_ARCHITECTURE_ALPHA, "alpha" },
#endif
#ifdef PROCESSOR_ARCHITECTURE_MIPS
  { PROCESSOR_ARCHITECTURE_MIPS, "mips" },
#endif
#ifdef PROCESSOR_ARCHITECTURE_PPC
  { PROCESSOR_ARCHITECTURE_PPC, "ppc" },
#endif
#ifdef PROCESSOR_ARCHITECTURE_SHX
  { PROCESSOR_ARCHITECTURE_SHX, "sh" },
#endif
#ifdef PROCESSOR_ARCHITECTURE_ARM
  { PROCESSOR_ARCHITECTURE_ARM, "arm" },
#endif
#ifdef PROCESSOR_ARCHITECTURE_IA32_ON_WIN64
  { PROCESSOR_ARCHITECTURE_IA32_ON_WIN64, "wow64" },
#endif
  { 0, 0 }
};


static table_entry families[] = {
#ifdef VER_PLATFORM_WIN32s
  { VER_PLATFORM_WIN32s, "32s/3.1" },
#endif
#ifdef VER_PLATFORM_WIN32_WINDOWS
  { VER_PLATFORM_WIN32_WINDOWS, "95/98/SE/ME" },
#endif
#ifdef VER_PLATFORM_WIN32_NT
  { VER_PLATFORM_WIN32_NT, "NT/2000/XP/2003" },
#endif
#ifdef VER_PLATFORM_WIN32_CE
  { VER_PLATFORM_WIN32_CE, "CE" },
#endif
  { 0, 0 }
};

void get_system_flavour(std::string & ident)
{

  SYSTEM_INFO si;
  OSVERSIONINFO vi;

  vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

  GetSystemInfo(&si);
  I(GetVersionEx(&vi));
  
  std::string family, processor;

  key_to_string(vi.dwPlatformId, families, family, "unknown");

  processor = "unknown";

  bool old_skool_cpu_identification = true;

#ifdef VER_PLATFORM_WIN32_NT
  if (vi.dwPlatformId == VER_PLATFORM_WIN32_NT)
    old_skool_cpu_identification = false;
#endif

#ifdef VER_PLATFORM_WIN32_CE
  if (vi.dwPlatformId == VER_PLATFORM_WIN32_CE)
    old_skool_cpu_identification = false;
#endif

  if (old_skool_cpu_identification)
    key_to_string(si.dwProcessorType, processor_types, processor, "unknown");
  else
    {
      key_to_string(si.wProcessorArchitecture, processors, processor, "unknown");
      processor = (F("%s (level %d, rev %d)")
                   % processor
                   % si.wProcessorLevel
                   % si.wProcessorRevision).str();
    }

  ident = (F("Windows %s (%d.%d, build %d, %s) on %s")
           % family 
           % vi.dwMajorVersion
           % vi.dwMinorVersion
           % vi.dwBuildNumber
           % (vi.szCSDVersion[0] == '\0' ? "none" : vi.szCSDVersion)
           % processor).str();
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
