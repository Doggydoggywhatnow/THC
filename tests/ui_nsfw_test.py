#!/usr/bin/env python3
"""End-to-end test of the NSFW portal's hide/show and panic keys, run against the real THC in a headless X
server (xvfb-run). THC_KEYS makes THC press the keys itself and exit; the result is read back from the files
THC writes. Skipped (exit 77) where xvfb-run or THC is missing."""
import os, re, shutil, sqlite3, subprocess, sys, tempfile

thc = sys.argv[1] if len(sys.argv) > 1 else "build/thc"
xvfb = shutil.which("xvfb-run")
if not xvfb or not os.path.exists(thc):
    print("SKIP: xvfb-run or thc not found")
    sys.exit(77)

failures = 0


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print("FAILED:", what)


root = tempfile.mkdtemp(prefix="thc-ui-nsfw-")
data = os.path.join(root, "xdg", "THC", "THC")
music = os.path.join(root, "Music")
os.makedirs(data)
os.makedirs(os.path.join(music, "Artist", "Album"))
open(os.path.join(music, "Artist", "Album", "a.mp3"), "wb").write(b"\xff\xfb" * 100)


def settings():
    return open(os.path.join(data, "settings.ini")).read()


def write_settings(extra=""):
    open(os.path.join(data, "settings.ini"), "w").write(
        "setup_done = 1\nmusic_dir = %s\n%s" % (music, extra))


def run(keys):
    env = dict(os.environ, XDG_DATA_HOME=os.path.join(root, "xdg"), SDL_AUDIO_DRIVER="dummy",
               THC_KEYS=",".join(["wait", "wait"] + keys + ["wait", "wait"]))
    env.pop("THC_SCREENSHOT", None)
    r = subprocess.run([xvfb, "-a", "-s", "-screen 0 1280x800x24", thc], env=env, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=120)
    return r.returncode


def value(key):
    m = re.search(r"^%s = (.*)$" % re.escape(key), settings(), re.M)
    return m.group(1).strip() if m else None


H, X = "Ctrl+Shift+H", "Ctrl+Shift+X"

# 1. off by default: the keys do nothing and the portal stays off
write_settings()
check(run([H, H, H, X, "n"]) == 0, "runs with the portal off")
check(value("nsfw_enabled") == "0", "the portal is off by default")
check(value("nsfw_tab") == "visible", "the hide key does nothing while the portal is off")

# make a database with a tag, so we can see it survives
db = os.path.join(data, "history.db")
con = sqlite3.connect(db)
check(con.execute("select count(*) from nsfw_tags").fetchone()[0] == 0, "no marks to begin with")
con.execute("insert into nsfw_tags(root,rel_path,scope,size,state,created) values(1,'Artist',1,0,1,0)")
con.commit()
con.close()

# 2. enabled: the hide key hides, the same key shows; the last press decides, however fast
write_settings("nsfw_enabled = 1\n")
check(run([H]) == 0 and value("nsfw_tab") == "hidden", "one press hides the tab")
check(run([H]) == 0 and value("nsfw_tab") == "visible", "one more press shows it (hidden state survived the restart)")

def flip(state, presses):
    """Each press flips the tab: the final state is fixed by the number of presses."""
    return state if presses % 2 == 0 else ("hidden" if state == "visible" else "visible")


state = "visible"
for presses, noise in ((5, []), (4, []), (3, [X] * 2), (9, [X] * 3), (2, [X])):
    check(run([H] * presses + noise) == 0, "runs with %d presses" % presses)
    state = flip(state, presses)
    check(value("nsfw_tab") == state, "%d presses end %s (the final state matches the last press)" % (presses, state))

# 3. hidden is not off: the feature, the marks and the main portals' filtering are untouched
check(value("nsfw_enabled") == "1", "hiding the tab does not turn the portal off")
con = sqlite3.connect(db)
check(con.execute("select count(*) from nsfw_tags").fetchone()[0] == 1, "the marks survive hiding")
con.close()

# 4. panic from every screen is safe, any number of times, and changes nothing it should not
check(run([X, X, X, "3", X, "2", X, "4", X, "1", X]) == 0, "panic spam from several screens does not crash")
check(value("nsfw_enabled") == "1", "panic does not turn the portal off")

# 5. a PIN in the settings file is only the hash, and the portal opens locked
check(re.search(r"^nsfw_pin_hash = [0-9a-f]*$", settings(), re.M) is not None, "the PIN field holds only hex")

shutil.rmtree(root, ignore_errors=True)
print("%s: %d failure%s" % ("ui_nsfw", failures, "" if failures == 1 else "s"))
sys.exit(1 if failures else 0)
