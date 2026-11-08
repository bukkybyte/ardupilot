#!/usr/bin/env python3
"""Hook AP_PackAuth into an ArduPilot tree (anchors validated on Plane-4.6.3).

  apply_patch.py <ardupilot-dir> [--board CubeOrange ...] [--unsigned] [--release] [--dry-run]

Patches, each only if not already done (also recognises the same edits made by hand):
  Tools/ardupilotwaf/ardupilotwaf.py   register the library so it is compiled and linked
  libraries/AP_Arming/AP_Arming.h      the module member + include
  libraries/AP_Arming/AP_Arming.cpp    parameters, thread start, arming gate, mandatory count
  libraries/AP_HAL_ChibiOS/hwdef/<board>/hwdef.dat   build switches
If an anchor is not found exactly once the edit is NOT made; the snippet is printed so you can
add it by hand, and the exit code is 2.
"""
import argparse, os, re, sys

TAG = "[AP_PackAuth]"
manual = []
changed = []

def rd(p):
    with open(p) as f:
        return f.read()

def wr(p, s, dry):
    if not dry:
        with open(p, "w") as f:
            f.write(s)

def apply_edits(root, rel, edits, dry):
    """edits: (name, old_anchor, new_text, present_regex). old_anchor must occur exactly once.
    An edit counts as done if its tag comment or present_regex is already in the file."""
    path = os.path.join(root, rel)
    if not os.path.exists(path):
        manual.append((rel, "file not found", ""))
        return
    s = rd(path)
    for name, old, new, present in edits:
        if ("%s %s" % (TAG, name)) in s or re.search(present, s):
            print("  skip  %-34s %s (already done)" % (rel, name))
            continue
        if s.count(old) != 1:
            print("  MANUAL %-33s %s (anchor found %d times)" % (rel, name, s.count(old)))
            manual.append((rel, name, new))
            continue
        s = s.replace(old, new)
        changed.append((rel, name))
        print("  edit  %-34s %s" % (rel, name))
    wr(path, s, dry)

def patch_waf(root, dry):
    rel = "Tools/ardupilotwaf/ardupilotwaf.py"
    s = rd(os.path.join(root, rel))
    if "'AP_PackAuth'" in s:
        print("  skip  %-34s library list (already done)" % rel)
        return
    m = re.search(r"^(\s*)'AP_BattMonitor',[^\n]*\n", s, re.M)
    if not m:
        manual.append((rel, "library list", "    'AP_PackAuth',   # add to COMMON_VEHICLE_DEPENDENT_LIBRARIES"))
        print("  MANUAL %-33s library list (AP_BattMonitor entry not found)" % rel)
        return
    s = s[:m.end()] + "%s'AP_PackAuth',\n" % m.group(1) + s[m.end():]
    wr(os.path.join(root, rel), s, dry)
    changed.append((rel, "library list"))
    print("  edit  %-34s library list" % rel)

def patch_arming_h(root, dry):
    rel = "libraries/AP_Arming/AP_Arming.h"
    path = os.path.join(root, rel)
    s = rd(path)
    if TAG in s or re.search(r"AP_PackAuth\s+_packauth", s):
        print("  skip  %-34s (already done)" % rel)
        return
    inc_anchor = '#include "AP_Arming_config.h"\n'
    if s.count(inc_anchor) != 1 or s.count("\nprotected:\n") < 1:
        manual.append((rel, "include + members",
                       "#include <AP_PackAuth/AP_PackAuth.h>\n(in the first protected: section)\n"
                       "#if AP_PACKAUTH_ENABLED\n    AP_PackAuth _packauth;\n    bool packauth_checks(bool report);\n#endif"))
        print("  MANUAL %-33s include + members" % rel)
        return
    s = s.replace(inc_anchor, inc_anchor + "#include <AP_PackAuth/AP_PackAuth.h>   // %s include\n" % TAG)
    i = s.index("\nprotected:\n") + len("\nprotected:\n")
    s = s[:i] + ("#if AP_PACKAUTH_ENABLED   // %s members\n    AP_PackAuth _packauth;\n"
                 "    bool packauth_checks(bool report);\n#endif\n\n") % TAG + s[i:]
    wr(path, s, dry)
    changed.append((rel, "include + members"))
    print("  edit  %-34s include + members" % rel)

def arming_cpp_edits():
    e = []
    e.append(("param group",
        "\n    AP_GROUPEND\n};",
        "\n#if AP_PACKAUTH_ENABLED && AP_PACKAUTH_PARAMS_ENABLED   // %s param group\n"
        "    // @Group: PKA_\n    // @Path: ../AP_PackAuth/AP_PackAuth.cpp\n"
        "    AP_SUBGROUPINFO(_packauth, \"PKA_\", 13, AP_Arming, AP_PackAuth),\n#endif\n\n"
        "    AP_GROUPEND\n};" % TAG,
        r"AP_SUBGROUPINFO\(\s*_packauth"))
    e.append(("thread start",
        "void AP_Arming::update(void)\n{\n",
        "void AP_Arming::update(void)\n{\n#if AP_PACKAUTH_ENABLED   // %s thread start\n"
        "    _packauth.init();    // starts the authenticator thread on the first call\n#endif\n\n" % TAG,
        r"_packauth\.init\(\)"))
    e.append(("arm gate",
        "    if (armed) { //already armed\n        return false;\n    }\n",
        "    if (armed) { //already armed\n        return false;\n    }\n\n"
        "#if AP_PACKAUTH_ENABLED   // %s arm gate: not skippable by ARMING_CHECK or force-arm\n"
        "    {\n        char msg[MAVLINK_MSG_STATUSTEXT_FIELD_TEXT_LEN+1] {};\n"
        "        if (!_packauth.gate_check(msg, sizeof(msg))) {\n"
        "            check_failed(true, \"%%s\", msg);\n"
        "#if HAL_LOGGING_ENABLED\n            AP::logger().arming_failure();\n#endif\n"
        "            return false;\n        }\n    }\n#endif\n" % TAG,
        r"_packauth\.gate_check\(msg, sizeof\(msg\)\)\)\s*\{\s*check_failed\(true"))
    e.append(("pre-arm chain",
        "        &  serial_protocol_checks(report)\n        &  estop_checks(report);",
        "        &  serial_protocol_checks(report)\n#if AP_PACKAUTH_ENABLED   // %s pre-arm chain\n"
        "        &  packauth_checks(report)\n#endif\n        &  estop_checks(report);" % TAG,
        r"&\s*packauth_checks\(report\)"))
    e.append(("pre-arm check function",
        "bool AP_Arming::pre_arm_checks(bool report)\n{",
        "#if AP_PACKAUTH_ENABLED   // %s pre-arm check function\n"
        "bool AP_Arming::packauth_checks(bool report)\n{\n"
        "    char msg[MAVLINK_MSG_STATUSTEXT_FIELD_TEXT_LEN+1] {};\n"
        "    if (!_packauth.gate_check(msg, sizeof(msg))) {\n"
        "        check_failed(report, \"%%s\", msg);\n        return false;\n    }\n    return true;\n}\n#endif\n\n"
        "bool AP_Arming::pre_arm_checks(bool report)\n{" % TAG,
        r"bool\s+AP_Arming::packauth_checks"))
    e.append(("arming cannot be switched off",
        "AP_Arming::Required AP_Arming::arming_required() const\n{\n",
        "AP_Arming::Required AP_Arming::arming_required() const\n{\n"
        "#if AP_PACKAUTH_ENABLED   // %s arming cannot be switched off\n"
        "    if (require != Required::YES_MIN_PWM && require != Required::YES_ZERO_PWM) {\n"
        "        return Required::YES_MIN_PWM;\n    }\n#endif\n" % TAG,
        r"AP_PACKAUTH_ENABLED[^\n]*\n(?:\s*//[^\n]*\n)?\s*if \(require != Required::YES_MIN_PWM"))
    return e

def patch_hwdef(root, boards, signed, release, dry):
    for b in boards:
        rel = "libraries/AP_HAL_ChibiOS/hwdef/%s/hwdef.dat" % b
        path = os.path.join(root, rel)
        if not os.path.exists(path):
            manual.append((rel, "hwdef", "add the defines below to the hwdef of your board"))
            print("  MANUAL %-33s hwdef (file not found)" % rel)
            continue
        s = rd(path)
        if "AP_PACKAUTH_ENABLED" in s:
            print("  skip  %-34s (already has AP_PACKAUTH_ENABLED)" % rel)
            continue
        block = ("\n# --- SmartPack pack authentication (AP_PackAuth) ---\n"
                 "define AP_PACKAUTH_ENABLED 1\n"
                 "define AP_PACKAUTH_CFG_SIGNED %d\n"
                 "define AP_PACKAUTH_DEBUG %d\n") % (1 if signed else 0, 0 if release else 1)
        if not s.endswith("\n"):
            s += "\n"
        wr(path, s + block, dry)
        changed.append((rel, "defines"))
        print("  edit  %-34s defines (signed=%d debug=%d)" % (rel, signed, not release))

def warn_leftovers(root):
    """the earlier BATT_MONITOR-based version must not stay in the tree"""
    for rel in ("libraries/AP_Arming/AP_Arming.cpp", "libraries/AP_BattMonitor/AP_BattMonitor.h",
                "libraries/AP_BattMonitor/AP_BattMonitor_config.h"):
        p = os.path.join(root, rel)
        if os.path.exists(p) and re.search(r"AP_BATTERY_AUTH_GATE|SMBus_Auth", rd(p)):
            print("  WARNING %s still has code of the old BATT_MONITOR-based version "
                  "(AP_BATTERY_AUTH_GATE / SMBus_Auth). Remove it." % rel)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root")
    ap.add_argument("--board", action="append", default=None)
    ap.add_argument("--unsigned", action="store_true", help="plain ARMING_PKA_NUM, no signature needed")
    ap.add_argument("--release", action="store_true", help="debug text off")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    for need in ("libraries/AP_Arming/AP_Arming.cpp", "Tools/ardupilotwaf/ardupilotwaf.py"):
        if not os.path.exists(os.path.join(root, need)):
            sys.exit("%s is not an ArduPilot checkout (missing %s)" % (root, need))
    if not os.path.exists(os.path.join(root, "libraries/AP_PackAuth/AP_PackAuth.cpp")):
        sys.exit("libraries/AP_PackAuth is missing in %s (put this folder there first)" % root)
    ver = os.path.join(root, "ArduPlane/version.h")
    if os.path.exists(ver):
        m = re.search(r'THISFIRMWARE\s+"([^"]+)"', rd(ver))
        print("ArduPilot tree: %s%s" % (m.group(1) if m else "?", "" if m and "4.6" in m.group(1) else
              "   (anchors were validated on 4.6.3; check the result)"))
    print("dry run, nothing is written" if a.dry_run else "patching:")
    patch_waf(root, a.dry_run)
    patch_arming_h(root, a.dry_run)
    apply_edits(root, "libraries/AP_Arming/AP_Arming.cpp", arming_cpp_edits(), a.dry_run)
    patch_hwdef(root, a.board or ["CubeOrange"], not a.unsigned, a.release, a.dry_run)
    warn_leftovers(root)
    print("\n%d edit(s) %s." % (len(changed), "would be made" if a.dry_run else "made"))
    if manual:
        print("\nMANUAL EDITS NEEDED:")
        for rel, name, snippet in manual:
            print("\n--- %s : %s\n%s" % (rel, name, snippet))
        sys.exit(2)

if __name__ == "__main__":
    main()
