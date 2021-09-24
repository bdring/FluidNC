import subprocess
import filecmp, tempfile, shutil, os

# Thank you https://docs.platformio.org/en/latest/projectconf/section_env_build.html !

try:
    tag = (
        subprocess.check_output(["git", "describe", "--tags", "--abbrev=0"], stderr=subprocess.DEVNULL)
        .strip()
        .decode("utf-8")
    )
except:
    tag = "v3.0.0"

grbl_version = tag.replace('v','').rpartition('.')[0]

# Check to see if the head commit exactly matches a tag.
# If so, the revision is "release", otherwise it is BRANCH-COMMIT
try:
    subprocess.check_call(["git", "describe", "--tags", "--exact"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    rev = ''
except:
    branchname = (
        subprocess.check_output(["git", "rev-parse", "--abbrev-ref", "HEAD"])
        .strip()
        .decode("utf-8")
    )
    revision = (
        subprocess.check_output(["git", "rev-parse", "--short", "HEAD"])
        .strip()
        .decode("utf-8")
    )
    modified = (
        subprocess.check_output(["git", "status", "-uno", "-s"])
        .strip()
        .decode("utf-8")
    )
    if modified:
        dirty = "-dirty"
    else:
        dirty = ""

    rev = " (%s-%s%s)" % (branchname, revision, dirty)

git_info = '%s%s' % (tag, rev)

provisional = "FluidNC/src/version.cxx"
final = "FluidNC/src/version.cpp"
with open(provisional, "w") as fp:
    fp.write('const char* grbl_version = \"' + grbl_version + '\";\n')
    fp.write('const char* git_info     = \"' + git_info + '\";\n')

if not (os.path.exists(final) and filecmp.cmp(provisional, final)):
    os.remove(final)
    os.rename(provisional, final)
else:
    os.remove(provisional)
