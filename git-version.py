import subprocess
import filecmp, tempfile, shutil, os

# Thank you https://docs.platformio.org/en/latest/projectconf/section_env_build.html !

gitFail = False
try:
    subprocess.check_call(["git", "status"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
except:
    gitFail = True

if gitFail:
    tag = "v3.0.x"
    rev = " (noGit)"
else:
    try:
        
        tag = (
            subprocess.check_output(["git", "describe", "--tags", "--abbrev=0"], stderr=subprocess.DEVNULL)
            .strip()
            .decode("utf-8")
        )

    except:
        tag = "v3.0.x"
        

    # Check to see if the head commit exactly matches a tag.
    # If so, the revision is "release", otherwise it is BRANCH-COMMIT
    try:
        subprocess.check_call(["git", "describe", "--tags", "--exact"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        rev = ''
        branchname = ''
        revision = ''
        repo = ''

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
        repo = (
            subprocess.check_output(["git", "config", "--get", "remote.origin.url"])
            .strip()
            .decode("utf-8")
        )
        if modified:
            dirty = "-avataar120-Beta"
        else:
            dirty = "-avataar120-Release"

        rev = " (%s-%s%s)" % (branchname, revision, dirty)

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
        repo = (
            subprocess.check_output(["git", "config", "--get", "remote.origin.url"])
            .strip()
            .decode("utf-8")
        )
        if modified:
            dirty = "-avataar120-Beta"
        else:
            dirty = "-avataar120-Release"

        rev = " (%s-%s%s)" % (branchname, revision, dirty)

grbl_version = tag.rpartition('.')[0]
git_info = '%s%s' % (tag, rev)

provisional = "FluidNC/src/version.cxx"
final = "FluidNC/src/version.cpp"
with open(provisional, "w") as fp:
    fp.write('const char* grbl_version = \"' + grbl_version + '\";\n')
    fp.write('const char* git_info     = \"' + git_info + '\";\n')
    fp.write('const char* tag = \"' + tag + '\";\n')
    fp.write('const char* branchname = \"' + branchname + '\";\n')
    fp.write('const char* revision = \"' + revision + '\";\n')
    #fp.write('const char* modified = \"' + modified + '\";\n')
    fp.write('const char* repo = \"' + repo + '\";\n')

if not os.path.exists(final):
    # No version.cpp so rename version.cxx to version.cpp
    os.rename(provisional, final)
elif not filecmp.cmp(provisional, final):
    # version.cxx differs from version.cpp so get rid of the
    # old .cpp and rename .cxx to .cpp
    os.remove(final)
    os.rename(provisional, final)
else:
    # The existing version.cpp is the same as the new version.cxx
    # so we can just leave the old version.cpp in place and get
    # rid of version.cxx
    os.remove(provisional)
