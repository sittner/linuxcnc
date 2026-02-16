# RTAI and Xenomai Removal Plan

## Overview

This document tracks the multi-phase removal of RTAI and Xenomai support from LinuxCNC, simplifying the codebase to support only RT_PREEMPT (uspace).

## Phase 1: Initial Audit and Safe File Removal ✓ COMPLETE

### Files Removed (7 files, ~3,031 lines)

#### Core RTAPI Source Files (5 files)
- ✅ `src/rtapi/rtai_rtapi.c` (1,749 lines) - RTAI kernel module implementation
- ✅ `src/rtapi/rtai_ulapi.c` (863 lines) - RTAI userland API  
- ✅ `src/rtapi/uspace_rtai.cc` (188 lines) - uspace RTAI wrapper
- ✅ `src/rtapi/uspace_xenomai.cc` (193 lines) - uspace Xenomai wrapper
- ✅ `src/rtapi/rtapi_rtai_shm_wrap.h` (21 lines) - RTAI shared memory header

#### Debian Package Files (2 files)
- ✅ `debian/control.uspace-rtai.in` - RTAI package definition
- ✅ `debian/control.uspace-xenomai.in` - Xenomai package definition

### Current State
- Source files removed
- Build system still has conditional compilation blocks
- Conditionals ensure uspace-only builds work without RTAI/Xenomai
- Documentation still references RTAI/Xenomai (to be updated in Phase 3)

---

## Phase 2: Build System Cleanup ✓ COMPLETE

### Files Modified

#### 1. `src/configure.ac` - Remove RTAI/Xenomai Detection
**Lines removed/modified:**
- ✅ Lines 118-127: Section 2 header and RTAI variable initialization
- ✅ Lines 163-174: `--with-realtime` help string mentions RTAI
- ✅ Lines 204-223: rtai-config detection and search
- ✅ Lines 225-226: xeno-config detection
- ✅ Lines 229-263: USPACE_RTAI and USPACE_XENOMAI configuration
- ✅ Lines 318-330: rtai-config case in RTS switch
- ✅ Lines 342-355: kernel headers check for non-uspace
- ✅ Lines 357-371: RTAI variable substitution and AC_DEFINE blocks
- ✅ Lines 395-400: RTAI CC detection

**Kept:**
- ✅ Lines 331-340: uspace case (the RT_PREEMPT path)
- ✅ Basic RTS variable and uspace support

**Changes made:**
```bash
✅ Removed:
- RTAI variable and initialization
- rtai-config detection
- xeno-config detection  
- CONFIG_USPACE_RTAI logic
- CONFIG_USPACE_XENOMAI logic
- RTAI-specific AC_SUBST and AC_DEFINE
- RTAI case in RTS switch statement

✅ Simplified:
- Made uspace the only supported RTS value
- Removed "or RTAI path" from help strings
- Simplified RTS case statement to only handle uspace
```

#### 2. `src/rtapi/Submakefile` - Remove Library Build Rules
**Lines removed:**
- ✅ Lines 26-35: CONFIG_USPACE_RTAI conditional block
- ✅ Lines 37-46: CONFIG_USPACE_XENOMAI conditional block

These blocks tried to build the deleted source files when CONFIG_USPACE_RTAI=y or CONFIG_USPACE_XENOMAI=y.

#### 3. `src/Makefile.inc.in` - Remove Config Variables
**Lines removed:**
- ✅ Line 237: `CONFIG_USPACE_RTAI=@CONFIG_USPACE_RTAI@`
- ✅ Line 241: `CONFIG_USPACE_XENOMAI=@CONFIG_USPACE_XENOMAI@`

#### 4. `src/rtapi/uspace_common.h` - Remove Conditional Blocks
**Lines removed:**
- ✅ Lines 376-385: `#ifdef USPACE_RTAI` block and detect_rtai()
- ✅ Lines 387-396: `#ifdef USPACE_XENOMAI` block and detect_xenomai()

**Note:** Kept the `#else` fallbacks that return 0, now as unconditional stubs.

---

## Phase 3: Script Cleanup ✓ COMPLETE

### 1. `scripts/rtapi.conf.in`
**Lines removed:**
- ✅ Lines 22-54: RTAI module loading configuration (adeos, rtai_hal, rtai_sched, etc.)

**Kept:**
- ✅ Lines 1-21: Header and basic configuration
- ✅ Simplified to empty MODULES for uspace-only

### 2. `scripts/realtime.in`  
**Changes made:**
- ✅ Removed RTAI-specific module loading logic (lines 86-114)
- ✅ Simplified CheckStatus() to only check rtapi_app for uspace
- ✅ Simplified CheckMem() to return immediately (no kernel modules to check)
- ✅ Simplified Unload() to only handle uspace rtapi_app cleanup
- ✅ Simplified CheckUnloaded() to return immediately (no kernel modules)
- ✅ Updated header description to remove RTAI references

**Strategy:** Script now simplified to minimal RTAPI/HAL loading for uspace only.

### 3. `scripts/platform-is-supported`
**Changes made:**
- ✅ Line 34: Removed `supported_kernel_flavors` list entirely (no longer needed)
- ✅ Lines 41-55: Removed entire `detect_kernel_flavor()` function
- ✅ Line 78: Removed `kernel_flavor = detect_kernel_flavor(uname)` call
- ✅ Line 91: Removed kernel flavor from uname print statement
- ✅ Lines 111-113: Removed kernel flavor validation check
- ✅ Fixed regex escape sequence warning (changed `\.` to `r'\.'`)
- **Result:** Script now only checks OS, CPU, and distribution version (no kernel flavor detection)

### 4. `scripts/latency-histogram`  
**Lines removed:**
- ✅ Lines 33-37: RTAI detection in tcl_platform and realtime module loading
- ✅ Lines 300-304: RTAI conditional startup code

---

## Phase 4: Documentation Updates (TODO)

### Primary Documentation Files (94 RTAI references, 3 Xenomai references)

#### Critical Documentation
1. **`docs/src/code/building-linuxcnc.adoc`** (23 references)
   - Lines 120-122: RTAI description
   - Lines 181-187: RTAI realtime platform build instructions
   - Lines 301-322: RTAI configure options and kernel configuration
   - Line 461: RTAI memory lock privilege note
   - **Action:** Remove RTAI as an option, update to show only uspace/RT_PREEMPT

2. **`docs/src/getting-started/system-requirements.adoc`** (10 references)
   - Lines 57, 69, 84-100: RTAI kernel sections
   - **Action:** Remove RTAI sections, keep only RT_PREEMPT information

3. **`docs/src/getting-started/getting-linuxcnc.adoc`**
   - Remove RTAI installation instructions
   - Update to show only RT_PREEMPT kernel installation

4. **`docs/INSTALL.adoc` and `docs/INSTALL_es.adoc`**
   - Remove RTAI installation steps
   - Simplify to RT_PREEMPT only

#### Man Pages
5. **`docs/man/man3/intro.3rtapi`**
   - Update RTAPI introduction to remove RTAI platform mentions

6. **`docs/man/man3/rtapi_get_time.3rtapi`**
   - Update platform-specific notes

7. **`docs/man/man3/rtapi_is.3rtapi`**  
   - Update platform detection documentation

#### Configuration and Integration Docs
8. Other documentation files with RTAI references:
   - `docs/src/config/pncconf.adoc`
   - `docs/src/config/ini-config.adoc`
   - `docs/src/integrator/steppers.adoc`
   - `docs/src/config/stepper-diagnostics.adoc`
   - Various GUI documentation files

### Translation Files (Phase 4b)

#### Documentation Translations (15+ files)
- `docs/po/de.po`, `docs/po/es.po`, `docs/po/fr.po`, etc.
- Update translated strings that mention RTAI/Xenomai
- Mark untranslated after changes

#### Source Code Translations (30+ files)  
- `src/po/*.po` - Main application translations
- `src/po/gmoccapy/*.po` - Gmoccapy GUI translations
- Update any UI strings mentioning RTAI/Xenomai

---

## Phase 5: Final Verification (TODO)

### Build Verification
- [x] Configure with `--with-realtime=uspace` succeeds (Phase 2 complete)
- [ ] Build completes without errors
- [ ] All tests pass
- [ ] No broken references to removed files

### Functional Verification  
- [ ] rtapi_app starts correctly
- [ ] HAL components load
- [ ] Sample configurations run
- [ ] No RTAI/Xenomai detection code executes

### Documentation Verification
- [ ] Documentation builds without errors
- [ ] No broken links to RTAI content
- [ ] Installation guides are clear and accurate

---

## Statistics

### Phase 1 Complete
- **Files removed:** 7
- **Lines removed:** ~3,031
- **Build system:** Still compatible (conditionals prevent issues)

### Phase 2 Complete
- **Files modified:** 4
- **Lines removed/modified:** ~180
- **Build system:** Simplified to uspace-only

### Remaining Work
- **Scripts to modify:** 4 files (~100 lines to simplify)
- **Documentation files:** ~15 primary files (94 RTAI refs, 3 Xenomai refs)
- **Translation files:** ~45 .po files

### Total Estimated Impact
- **Lines of code removed/modified:** ~3,200+ (Phases 1-2 complete)
- **Documentation updates:** ~100+ references
- **Translation updates:** ~50 files

---

## Notes

### Why This Approach?
1. **Phase 1:** Safe removals first - files clearly not needed
2. **Phase 2:** Build system - enables clean builds  
3. **Phase 3:** Scripts - runtime behavior
4. **Phase 4:** Documentation - user-facing content
5. **Phase 5:** Verification - ensure everything works

### Conservative Approach
- Removed only standalone RTAI/Xenomai-specific files
- Left build system conditionals intact for safety
- Build still works for uspace (conditionals skip deleted files)
- Can proceed with phases incrementally

### Key Insights
- Only 2 conditional compilation blocks in source code (`uspace_common.h`)
- Most complexity is in build system configuration (`configure.ac`)
- Extensive documentation and translation overhead
- Clean separation allows incremental removal
