from shutil import copyfile

try:
    copyfile("./include/User_Setup.h", ".pio/libdeps/esp32dev/TFT_eSPI/User_Setup.h")
    copyfile("./include/User_Setup_Select.h", ".pio/libdeps/esp32dev/TFT_eSPI/User_Setup_Select.h")
except IOError as e:
    print("Unable to copy file. %s" % e)
    exit(1)