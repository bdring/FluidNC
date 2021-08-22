import subprocess

# Thank you https://docs.platformio.org/en/latest/projectconf/section_env_build.html !

try:
    tag = (
        subprocess.check_output(["git", "describe", "--tags", "--abbrev=0"])
        .strip()
        .decode("utf-8")
    )
except:
    tag = "2.0"

print("-DGIT_TAG='\"%s\"'" % (tag))

# Check to see if the head commit exactly matches a tag.
# If so, the revision is "release", otherwise it is BRANCH-COMMIT
try:
    subprocess.check_call(["git", "describe", "--tags", "--exact"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("-DGIT_REV='\"%s\"'" % "release")
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
        dirty = ""
    else:
        dirty = "-dirty"

    print("-DGIT_REV='\"%s-%s%s\"'" % (branchname, revision, dirty))
