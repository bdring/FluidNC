import subprocess

# Thank you https://docs.platformio.org/en/latest/projectconf/section_env_build.html !

revision = (
    subprocess.check_output(["git", "rev-parse", "--short", "HEAD"])
    .strip()
    .decode("utf-8")
)

branchname = (
    subprocess.check_output(["git", "rev-parse", "--abbrev-ref", "HEAD"])
    .strip()
    .decode("utf-8")
)

print("-DGIT_REV='\"%s-%s\"'" % (branchname, revision))
