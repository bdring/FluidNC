# The classic esp32 sections.ld files (wifi/, bt/, noradio/) put .eh_frame
# in its own output section instead of folding it into .flash.rodata.
# pioarduino's flash size check adds up only the sections it names, so it
# would miss those ~80 KB - and .flash.appdesc, which it misses anyway - and
# pass an image too big for the app partition.  Count them too.
Import("env")

env.Replace(
    SIZEPROGREGEXP=r"^(?:\.iram0\.text|\.iram0\.vectors|\.dram0\.data|"
    r"\.flash\.text|\.flash\.rodata|\.flash\.appdesc|\.eh_frame)\s+([0-9]+).*"
)
