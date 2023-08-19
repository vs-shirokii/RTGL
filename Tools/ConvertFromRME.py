# Copyright (c) 2023 Sultim Tsyrendashiev
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import os
import sys
import imageio
import math
import numpy as np
import shutil
from PIL import Image

FROM_FOLDER         = "mat_dev_old"
TO_FOLDER           = "mat_dev"
ADDITIONAL_FOLDERS  = ["mat_src", "mat_TEMP"]

ACCEPTED_EXTENSIONS = [".png",".tga"]
RESULT_EXTENSION    = ".png"


def resize(float_img, new_shape):
    # note: swapped width / height
    new_width  = new_shape[1]
    new_height = new_shape[0]
    pilimg = Image.fromarray((float_img * 255.0).astype(np.uint8))
    intimg = np.array(pilimg.resize((new_width, new_height),
                                    resample=Image.LANCZOS) )
    return intimg.astype(np.float32) / 255.0


OVERWRITE_ORM           = False
ROUGHNESS_BIAS          = 0.0
ROUGHNESS_MULT          = 1.0
METALLIC_BIAS           = 0.0
METALLIC_MULT           = 1.0

OVERWRITE_EMIS          = False
EMIS_NORMALIZE_TO_ONE   = False


def calculate_luminance(rgb_array):
    return 0.2126 * rgb_array[..., 0] + 0.7152 * rgb_array[..., 1] + 0.0722 * rgb_array[..., 2]


def convert(path_albedo, path_rme, result_path_orm, result_path_emis):
    orm_is_ok = True
    if os.path.exists(result_path_orm):
        if not OVERWRITE_ORM:
            print(f"    Result file already exists, ignoring: {result_path_orm}")
            orm_is_ok = False
        else:
            os.remove(result_path_orm)
    
    emis_is_ok = True
    if os.path.exists(result_path_emis):
        if not OVERWRITE_EMIS:
            print(f"    Result file already exists, ignoring: {result_path_emis}")
            emis_is_ok = False
        else:
            os.remove(result_path_emis)

    if not os.path.exists(path_albedo):
        print(f"    No albedo file, NO EMISSION FILE will be produced")
        emis_is_ok = False        

    if not orm_is_ok and not emis_is_ok:
        return

    rme    = imageio.v3.imread(path_rme)
    albedo = imageio.v3.imread(path_albedo) if emis_is_ok else np.zeros((*rme.shape[:2], 4), dtype=np.uint8)

    rme    = rme   .astype(np.float32) / 255.0
    albedo = albedo.astype(np.float32) / 255.0

    if orm_is_ok:
        # ( 1.0, rme[0], rme[1] )
        orm = np.zeros((*rme.shape[:2], 4), dtype=np.float32)
        orm[:, :, 0] = 1.0
        orm[:, :, 1] = np.clip(ROUGHNESS_BIAS + ROUGHNESS_MULT * rme[:, :, 0], 0.0, 1.0)
        orm[:, :, 2] = np.clip(METALLIC_BIAS  + METALLIC_MULT  * rme[:, :, 1], 0.0, 1.0)
        orm[:, :, 3] = 1.0
        imageio.imsave(result_path_orm, (orm * 255.0).astype(np.uint8))

    # albedo * rme[2]
    if emis_is_ok:
        if albedo.shape == rme.shape:
            newsize = (albedo.shape[0], albedo.shape[1])
        else:
            area_a = albedo.shape[0] * albedo.shape[1]
            area_e = rme.shape[0] * rme.shape[1]
            if area_a > area_e:
                newsize = (albedo.shape[0], albedo.shape[1])
                rme = resize(rme, newsize)
            else:
                newsize = (rme.shape[0], rme.shape[1])
                albedo = resize(albedo, newsize)
    
        emis = np.zeros((newsize[0], newsize[1], 4), dtype=np.float32)
        emis[:, :, 0] = albedo[:, :, 0] * np.clip(rme[:, :, 2], 0.0, 1.0)
        emis[:, :, 1] = albedo[:, :, 1] * np.clip(rme[:, :, 2], 0.0, 1.0)
        emis[:, :, 2] = albedo[:, :, 2] * np.clip(rme[:, :, 2], 0.0, 1.0)

        if EMIS_NORMALIZE_TO_ONE:
            lum = calculate_luminance(emis)
            if (lum.max() > 0.0):
                newmax = math.sqrt(lum.max() / 0.1)
                emis = np.clip(emis * newmax / lum.max(), 0.0, 1.0)
        
        emis[:, :, 3] = 1.0
        imageio.imsave(result_path_emis, (emis * 255.0).astype(np.uint8))


def find_albedo_path(from_folder_abs, relative_dirpath, basename):
    tried_paths = []
    folders_to_check = [from_folder_abs] + [os.path.abspath(d) for d in ADDITIONAL_FOLDERS]
    for folder in folders_to_check:
        for ext in ACCEPTED_EXTENSIONS:
            p = os.path.join(folder, relative_dirpath, f"{basename}{ext}")
            if os.path.exists(p):
                return (p, tried_paths)
            tried_paths.append(p)
    return ("", tried_paths)


def main():
    if "--help" in sys.argv or "--h" in sys.argv or "-help" in sys.argv or "-h" in sys.argv:
        print(f"Usage: python.exe ConvertFromRME.py")
        print(f"    It should be launched in the \'rt\' folder that contains \'{FROM_FOLDER}\' and \'{TO_FOLDER}\'")
        print(f"    The script reads '<name>.<extension>\' (albedo) + \'<name>_rme.<extension>\'")
        print(f"    files from {FROM_FOLDER} folder and creates new files in {TO_FOLDER} folder:")
        print(f"    \'<name>_orm.<extension>\' + \'<name>_e.<extension>\',")
        print(f"    converting legacy Roughness-Metallic-Emission (RGB) textures into")
        print(f"    Occlusion-Roughness-Metallic (RGB) + Emission textures (RGB).")
        print(f"    Such file structure is motivated by glTF2 standard.")
        print(f"    This file also copies _n (Normal) and albedo files (if were under \'{FROM_FOLDER}\').")
        return
    
    if not os.path.exists(FROM_FOLDER):
        print(f"Can't find the input folder \'{FROM_FOLDER}\'")
        return
    
    if not os.path.exists(TO_FOLDER):
        os.mkdir(TO_FOLDER)

    from_folder_abs = os.path.abspath(FROM_FOLDER)
    to_folder_abs = os.path.abspath(TO_FOLDER)
    
    # copy normal, albedo directly to TO_FOLDER
    for dirpath, _, filenames in os.walk(from_folder_abs):
        relative_dirpath = os.path.relpath(dirpath, from_folder_abs)
        for f in filenames:
            name, ext = os.path.splitext(f)
            if ext not in ACCEPTED_EXTENSIONS:
                continue
            print(f"{os.path.join(dirpath, f)}:")
            src_f = os.path.join(from_folder_abs, relative_dirpath, f)
            dst_f = os.path.join(to_folder_abs,   relative_dirpath, f)
            if os.path.exists(dst_f):
                print(f"    Destination file already exists: {dst_f}")
                continue
            if not name.endswith("_rme"):
                os.makedirs(os.path.dirname(dst_f), exist_ok=True)
                shutil.copy(src_f, dst_f)
                
    # process A+RME -> ORM+E
    for dirpath, _, filenames in os.walk(from_folder_abs):
        relative_dirpath = os.path.relpath(dirpath, from_folder_abs)
        for f in filenames:
            name, ext = os.path.splitext(f)
            if not name.endswith("_rme"):
                continue
            basename = name.removesuffix("_rme")
            print(f"{os.path.join(dirpath, f)}:")
            if ext not in ACCEPTED_EXTENSIONS:
                print(f"    Ignoring file because of its extension (\'{ext}\')")
                continue
            path_albedo, tried = find_albedo_path(from_folder_abs, relative_dirpath, basename)
            path_rme           = os.path.join(from_folder_abs, relative_dirpath, f"{basename}_rme{ext}")
            result_path_orm    = os.path.join(to_folder_abs  , relative_dirpath, f"{basename}_orm{RESULT_EXTENSION}")
            result_path_emis   = os.path.join(to_folder_abs  , relative_dirpath, f"{basename}_e{RESULT_EXTENSION}")
            if not os.path.exists(path_rme):
                print(f"    Unexpectedly, rme file doesn't exist: {path_rme}")
                continue
            if not path_albedo:
                print(f"    WARNING: Can't find albedo file for: {path_rme}")
                for t in tried:
                    print(f"        Tried: {t}")
            try:
                convert(path_albedo, path_rme, result_path_orm, result_path_emis)
            except Exception as e:
                print(f"    Convert error: {e}, on files:")
                print(f"        {path_albedo}")
                print(f"        {path_rme}")


if __name__ == '__main__':
    main()
