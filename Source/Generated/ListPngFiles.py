import sys
from os import walk
from os import path


def gen(dirPath):
    _, _, filenames = next(walk(dirPath))

    if len(filenames) == 0:
        print("Can't find .png files in directory: " + dirPath)
        return

    with open("BlueNoiseFileNames.h", "w") as hdr:
        with open("BlueNoiseFileNames.cpp", "w") as src:
            hdr.write("// This file was generated by ListPngFiles.py\n\n")
            hdr.write("#pragma once\n\n")
            hdr.write("extern const char* const BlueNoiseFileNames[];\n")
            hdr.write("extern const int BlueNoiseFileNamesCount;\n")

            src.write("// This file was generated by ListPngFiles.py\n\n")
            src.write("#include \"" + path.basename(hdr.name) + "\"\n\n")
            src.write("const char* const BlueNoiseFileNames[] =\n{\n")
            i = 0
            for name in filenames:
                if name.endswith(".png"):
                    src.write("    \"" + name + "\",\n")
                    i += 1
            src.write("};\n\n")
            src.write("const int BlueNoiseFileNamesCount = " + str(i) + ";\n")


if __name__ == '__main__':
    if len(sys.argv) > 1:
        gen(sys.argv[1])
    else:
        print("Arguments: <directory path>")
