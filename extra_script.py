Import("env")
from shutil import copyfile

def move_bin(*args, **kwargs):
    print("Copying bin output to project directory...")
    target = str(kwargs['target'][0])
    print(target)
    if target == ".pio/build/esp32dev_rgb/firmware.bin":
        copyfile(target, 'esp32_solvinden_rgb.bin')
    elif target == ".pio/build/esp32dev_rgbw/firmware.bin":
        copyfile(target, 'esp32_solvinden_rgbw.bin')
    print("Done.")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", move_bin)   #post action for .bin