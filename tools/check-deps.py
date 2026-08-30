#!/usr/bin/env python3
"""Dependency inventory check for scoreview-engine.

Two jobs, deliberately separate:

  --verify      Offline. Every version in tools/deps.json must match what the
                tree actually builds. Cheap enough for every CI build, and it
                is what keeps the manifest — and docs/dependencies.md with it —
                from quietly going stale.

  --updates     Online. Ask each upstream whether a newer release exists.
                This is the *primary* security signal for the vendored C
                libraries, which sounds wrong until you look at the feeds:
                FreeType 2.14.1 had nine CVEs fixed in 2.14.2/2.14.3, and
                OSV knew none of them for pkg:generic/freetype@2.14.1 while
                NVD matched exactly one against cpe:2.3:a:freetype:freetype
                (most are filed against Oracle Java, which bundles FreeType).
                "Are we on the current release" is the question that has a
                reliable answer for these.

  --advisories  Online. OSV and NVD anyway, as a second pair of eyes. Never
                the only signal. Findings already triaged are listed per
                component in deps.json under advisories_acknowledged, with the
                reason; they are printed but do not count, because a real but
                inapplicable finding that reappears every week trains everyone
                to stop reading the report.

Exit codes: 0 clean, 1 findings a human should read, 2 the check itself broke.
The weekly workflow files an issue on 1 and goes red on 2. stdlib only, so it
runs on a bare runner.
"""

import argparse
import datetime
import glob
import json
import os
import pathlib
import re
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "tools" / "deps.json"

UA = "scoreview-engine-check-deps"
TIMEOUT = 30


# --------------------------------------------------------------------------
# helpers


def fetch(url, data=None, headers=None, timeout=TIMEOUT):
    hdrs = {"User-Agent": UA, "Accept": "application/json"}
    if headers:
        hdrs.update(headers)
    body = json.dumps(data).encode() if data is not None else None
    if body:
        hdrs["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=body, headers=hdrs)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read().decode("utf-8", "replace")


def fetch_json(url, data=None, headers=None, timeout=TIMEOUT):
    return json.loads(fetch(url, data=data, headers=headers, timeout=timeout))


def gh_headers():
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    h = {"Accept": "application/vnd.github+json"}
    if token:
        h["Authorization"] = f"Bearer {token}"
    return h


def parse_version(v):
    """'2.14.3' -> (2, 14, 3). Non-numeric tails sort last but compare stably."""
    parts = re.split(r"[.\-_]", str(v))
    out = []
    for p in parts:
        if p.isdigit():
            out.append((0, int(p), ""))
        elif p:
            out.append((1, 0, p))
    return tuple(out)


def newer(candidate, current):
    return parse_version(candidate) > parse_version(current)


def run(cmd, cwd=None):
    """subprocess.run, but a missing executable is an outcome, not a traceback.

    Minimal build images do not always carry git, and a dependency checker that
    dies on that is worse than one that says so.
    """
    try:
        return subprocess.run(
            cmd, cwd=cwd or ROOT, capture_output=True, text=True, check=False
        )
    except (FileNotFoundError, NotADirectoryError) as e:
        return subprocess.CompletedProcess(cmd, returncode=127, stdout="", stderr=str(e))


def in_absent_submodule(relpath):
    """True when relpath is missing only because its submodule is not checked out.

    The weekly job has no reason to pull 800 MB of MuseScore to read a version
    out of one .cmake file, and a plain `git clone` has not pulled it either.
    Distinguishing that from a genuinely missing file keeps both quiet.
    """
    first = pathlib.PurePosixPath(str(relpath).replace("\\", "/")).parts[0]
    sub = ROOT / first
    return sub.is_dir() and not (sub / ".git").exists() and not (ROOT / relpath).exists()


# --------------------------------------------------------------------------
# verify rules — what the tree actually says


def verify_component(c, online):
    """Return (status, found, detail). status in ok / mismatch / skip / error."""
    rule = c.get("verify", {})
    kind = rule.get("rule")
    declared = c["version"]

    if kind == "dirname":
        matches = sorted(glob.glob(str(ROOT / rule["glob"])))
        dirs = [pathlib.Path(m).name for m in matches if pathlib.Path(m).is_dir()]
        if len(dirs) != 1:
            return "error", None, f"expected exactly one dir for {rule['glob']}, found {dirs}"
        found = dirs[0][len(rule["prefix"]):]
        return ("ok" if found == declared else "mismatch"), found, matches[0]

    if kind == "regex":
        path = ROOT / rule["file"]
        if not path.exists():
            if in_absent_submodule(rule["file"]):
                return "skip", None, f"{rule['file']} not checked out"
            return "error", None, f"missing file {rule['file']}"
        m = re.search(rule["pattern"], path.read_text(encoding="utf-8", errors="replace"))
        if not m:
            return "error", None, f"pattern {rule['pattern']!r} not found in {rule['file']}"
        return ("ok" if m.group(1) == declared else "mismatch"), m.group(1), rule["file"]

    if kind == "regex-all":
        found = set()
        for f in rule["files"]:
            path = ROOT / f
            if not path.exists():
                return "error", None, f"missing file {f}"
            hits = re.findall(rule["pattern"], path.read_text(encoding="utf-8", errors="replace"))
            if not hits:
                return "error", None, f"pattern not found in {f}"
            found.update(hits)
        if len(found) != 1:
            return "mismatch", "/".join(sorted(found)), f"disagreeing pins across {rule['files']}"
        got = found.pop()
        return ("ok" if got == declared else "mismatch"), got, ", ".join(rule["files"])

    if kind == "submodule-pin":
        # Not `git describe`: actions/checkout clones the submodule shallow
        # (.gitmodules says so), which leaves it without tags, and describe
        # then answers with a commit hash. The version the build actually
        # compiles is the one written in the submodule's own version file, and
        # that is readable however the checkout was made.
        sub = ROOT / rule["path"]
        vfile = sub / rule["version_file"]
        if not vfile.exists():
            return "skip", None, f"{rule['path']} not checked out"
        txt = vfile.read_text(encoding="utf-8", errors="replace")
        parts = []
        for pattern in rule["patterns"]:
            m = re.search(pattern, txt)
            if not m:
                return "error", None, f"{pattern!r} not found in {rule['version_file']}"
            parts.append(m.group(1))
        found = rule.get("join", ".").join(parts)
        if found != declared:
            return "mismatch", found, rule["version_file"]

        # The version string alone would not notice the submodule being moved
        # to a different commit that happens to carry the same version, so the
        # pin is checked too where git can answer.
        want = rule.get("commit")
        if want:
            r = run(["git", "ls-tree", "HEAD", rule["path"]])
            if r.returncode == 0 and r.stdout.strip():
                got = r.stdout.split()[2]
                if got != want:
                    return "mismatch", f"{found} @ {got[:8]}", f"pin expected {want[:8]}"
                return "ok", f"{found} @ {got[:8]}", rule["version_file"]
        return "ok", found, rule["version_file"]

    if kind == "npm-lock":
        lock = json.loads((ROOT / rule["lock"]).read_text(encoding="utf-8"))
        entry = lock.get("packages", {}).get(f"node_modules/{rule['package']}")
        if not entry:
            return "error", None, f"{rule['package']} not in {rule['lock']}"
        return ("ok" if entry["version"] == declared else "mismatch"), entry["version"], rule["lock"]

    if kind == "emscripten-port":
        # zlib's version is whatever the pinned emsdk ships — resolvable only
        # against that emsdk tag, so it needs the network.
        if not online:
            return "skip", None, "needs --online (emsdk port file)"
        emsdk = next((x["version"] for x in COMPONENTS if x["name"] == rule["follows"]), None)
        if not emsdk:
            return "error", None, f"no component {rule['follows']}"
        url = f"https://raw.githubusercontent.com/emscripten-core/emscripten/{emsdk}/tools/ports/{rule['port']}.py"
        try:
            txt = fetch(url, headers={"Accept": "text/plain"})
        except urllib.error.HTTPError as e:
            return "error", None, f"{url}: HTTP {e.code}"
        m = re.search(r"^VERSION\s*=\s*['\"]([^'\"]+)['\"]", txt, re.M)
        if not m:
            return "error", None, f"no VERSION in {url}"
        return ("ok" if m.group(1) == declared else "mismatch"), m.group(1), f"emsdk {emsdk} port"

    return "skip", None, f"no verify rule ({kind})"


# --------------------------------------------------------------------------
# watch rules — what upstream has


def latest_upstream(watch):
    """What upstream has, as one of three answers.

      {"kind": "version", "latest": ..., "source": ...}
      {"kind": "eol",     "eol": "YYYY-MM-DD", "days": int, "source": ...}
      {"kind": "none",    "reason": ...}

    The eol shape exists because "is there a newer one" is the wrong question
    for a runtime or a runner image: there is always a newer Node, and Node 22
    is supported into 2027. What deserves a weekly answer there is how much
    support is left.
    """
    rule = watch.get("rule")

    if rule == "github-release":
        j = fetch_json(
            f"https://api.github.com/repos/{watch['repo']}/releases/latest",
            headers=gh_headers(),
        )
        tag = j.get("tag_name")
        if not tag:
            return {"kind": "none", "reason": "no release"}
        strip = watch.get("strip", "")
        if strip and tag.startswith(strip):
            tag = tag[len(strip):]
        return {"kind": "version", "latest": tag, "source": watch["repo"]}

    if rule == "github-tag":
        j = fetch_json(
            f"https://api.github.com/repos/{watch['repo']}/tags?per_page=100",
            headers=gh_headers(),
        )
        pat = re.compile(watch["pattern"])
        versions = [".".join(m.groups()) for m in (pat.match(t["name"]) for t in j) if m]
        if not versions:
            return {"kind": "none", "reason": "no tag matched"}
        return {"kind": "version", "latest": max(versions, key=parse_version),
                "source": watch["repo"]}

    if rule == "muse-deps-dir":
        # What the channel offers, not what upstream has released. HarfBuzz
        # upstream is far ahead, but anything outside this directory means
        # leaving MuseScore's dependency channel and re-measuring the corpus
        # baseline it decides.
        j = fetch_json(
            f"https://api.github.com/repos/{watch['repo']}/contents/{watch['path']}"
            f"?ref={watch['ref']}",
            headers=gh_headers(),
        )
        versions = [e["name"] for e in j if e["type"] == "dir"]
        if not versions:
            return {"kind": "none", "reason": "channel empty"}
        return {"kind": "version", "latest": max(versions, key=parse_version),
                "source": f"{watch['repo']}@{watch['ref']}"}

    if rule == "emscripten-releases":
        j = fetch_json(
            "https://raw.githubusercontent.com/emscripten-core/emsdk/main/"
            "emscripten-releases-tags.json"
        )
        return {"kind": "version", "latest": j["aliases"]["latest"],
                "source": "emsdk releases"}

    if rule == "nodejs-lts":
        j = fetch_json(
            "https://raw.githubusercontent.com/nodejs/Release/main/schedule.json"
        )
        entry = j.get(f"v{watch['major']}")
        if not entry or "end" not in entry:
            return {"kind": "none", "reason": f"no schedule for v{watch['major']}"}
        return eol_answer(entry["end"], f"Node {watch['major']} ({entry.get('codename', '')})".strip())

    if rule == "eol-date":
        # A date a human read off a deprecation notice and wrote down. No feed
        # publishes GitHub runner-image retirement in machine-readable form.
        return eol_answer(watch["end"], watch.get("source", "recorded in deps.json"))

    return {"kind": "none", "reason": f"not watched ({rule})"}


def eol_answer(end, source):
    end_date = datetime.date.fromisoformat(end)
    days = (end_date - datetime.date.today()).days
    return {"kind": "eol", "eol": end, "days": days, "source": source}


# --------------------------------------------------------------------------
# advisories — second opinion only


def osv_query(purl, version):
    try:
        j = fetch_json(
            "https://api.osv.dev/v1/query", data={"package": {"purl": f"{purl}@{version}"}}
        )
    except Exception as e:  # noqa: BLE001 - advisory path must never break the run
        return None, str(e)
    return [v["id"] for v in j.get("vulns", [])], None


def nvd_query(cpe, version):
    match = f"{cpe}:{version}:*:*:*:*:*:*:*"
    url = (
        "https://services.nvd.nist.gov/rest/json/cves/2.0?virtualMatchString="
        + urllib.parse.quote(match)
        + "&resultsPerPage=50"
    )
    headers = {}
    if os.environ.get("NVD_API_KEY"):
        headers["apiKey"] = os.environ["NVD_API_KEY"]
    try:
        j = fetch_json(url, headers=headers, timeout=60)
    except Exception as e:  # noqa: BLE001
        return None, str(e)
    return [v["cve"]["id"] for v in j.get("vulnerabilities", [])], None


# --------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--verify", action="store_true", help="check the manifest against the tree (offline)")
    ap.add_argument("--updates", action="store_true", help="ask upstreams for newer releases")
    ap.add_argument("--advisories", action="store_true", help="query OSV and NVD")
    ap.add_argument("--online", action="store_true", help="allow network during --verify")
    ap.add_argument("--markdown", action="store_true", help="emit a markdown report")
    args = ap.parse_args()

    if not (args.verify or args.updates or args.advisories):
        args.verify = True

    global COMPONENTS
    COMPONENTS = json.loads(MANIFEST.read_text(encoding="utf-8"))["components"]

    problems = []
    lines = []

    def out(s=""):
        lines.append(s)

    online = args.online or args.updates or args.advisories

    if args.verify:
        out("## Manifest vs. tree" if args.markdown else "== manifest vs. tree ==")
        if args.markdown:
            out()
            out("| dependency | declared | found | |")
            out("|---|---|---|---|")
        for c in COMPONENTS:
            status, found, detail = verify_component(c, online)
            if status == "ok":
                mark = "ok"
            elif status == "skip":
                mark = "skip"
            else:
                mark = status.upper()
                problems.append(f"{c['name']}: manifest says {c['version']}, tree says {found} ({detail})")
            if args.markdown:
                out(f"| `{c['name']}` | {c['version']} | {found or '-'} | {mark} |")
            else:
                out(f"  {mark:8} {c['name']:16} declared {c['version']:10} found {found or '-'}"
                    + (f"   [{detail}]" if status != "ok" else ""))
        out()

    if args.updates:
        out("## Upstream" if args.markdown else "== upstream ==")
        if args.markdown:
            out()
            out("| dependency | in use | upstream | |")
            out("|---|---|---|---|")
        for c in COMPONENTS:
            try:
                ans = latest_upstream(c.get("watch", {}))
            except Exception as e:  # noqa: BLE001
                ans = {"kind": "none", "reason": f"query failed: {e}"}

            if ans["kind"] == "version":
                latest = ans["latest"]
                behind = newer(latest, c["version"])
                mark = "BEHIND" if behind else "current"
                if behind:
                    sec = " [security-critical]" if c.get("security_critical") else ""
                    problems.append(f"{c['name']}: {c['version']} -> {latest} available{sec}")
                right = latest

            elif ans["kind"] == "eol":
                days = ans["days"]
                # Six months is enough warning for a runner image or a Node
                # major and short enough that the warning still means something.
                if days < 0:
                    mark, right = "EOL", f"unsupported since {ans['eol']}"
                    problems.append(f"{c['name']}: unsupported since {ans['eol']} ({ans['source']})")
                elif days < 180:
                    mark, right = "EOL SOON", f"{ans['eol']} ({days} d)"
                    problems.append(f"{c['name']}: support ends {ans['eol']}, in {days} days ({ans['source']})")
                else:
                    mark, right = "current", f"{ans['eol']} ({days} d)"

            else:
                mark, right = "-", ans["reason"]

            if args.markdown:
                out(f"| `{c['name']}` | {c['version']} | {right} | {mark} |")
            else:
                out(f"  {mark:9} {c['name']:16} {c['version']:10} {right}")
        out()

    if args.advisories:
        out("## Advisory feeds (advisory only)" if args.markdown else "== advisory feeds ==")
        out()
        if args.markdown:
            out("Coverage for vendored C libraries is partial — treat a clean result as")
            out("no evidence, not evidence of absence. Release currency above is the")
            out("signal that actually holds.")
            out()
        for c in COMPONENTS:
            hits = []
            if c.get("purl"):
                ids, err = osv_query(c["purl"], c["version"])
                hits.append(("OSV", ids, err))
            if c.get("cpe"):
                ids, err = nvd_query(c["cpe"], c["version"])
                hits.append(("NVD", ids, err))
                time.sleep(7 if not os.environ.get("NVD_API_KEY") else 1)

            # Advisory IDs already triaged, with the reason recorded in
            # deps.json. Without this a permanent finding — an OSS-Fuzz entry
            # against a version we have accepted, say — files the same issue
            # every Monday until nobody reads it any more.
            known = set(c.get("advisories_acknowledged", {}))

            for feed, ids, err in hits:
                if err:
                    detail = f"query failed ({err[:60]})"
                elif ids:
                    fresh = [i for i in ids if i not in known]
                    seen = [i for i in ids if i in known]
                    detail = ", ".join(ids)
                    if seen:
                        detail += f"  (acknowledged: {', '.join(seen)})"
                    if fresh:
                        problems.append(f"{c['name']}: {feed} reports {', '.join(fresh)}")
                else:
                    detail = "none"
                if args.markdown:
                    out(f"* `{c['name']}` - {feed}: {detail}")
                else:
                    out(f"  {c['name']:16} {feed}: {detail}")
        out()

    print("\n".join(lines))

    if problems:
        print("## Needs a look" if args.markdown else "== needs a look ==")
        print()
        for p in problems:
            print(f"* {p}" if args.markdown else f"  ! {p}")
        return 1

    print("all dependencies current and the manifest matches the tree.")
    return 0


if __name__ == "__main__":
    # 0 clean, 1 findings a human should read, 2 the check itself broke.
    # The weekly workflow files an issue on 1 and goes red on 2 — a drifted
    # dependency and a broken checker deserve different reactions.
    try:
        sys.exit(main())
    except Exception:  # noqa: BLE001
        import traceback

        traceback.print_exc()
        sys.exit(2)
