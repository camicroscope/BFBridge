from PIL import Image
import numpy as np
import os
from pathlib import Path

# returns ffi, lib
def IMPORT_BFBRIDGE():
    try:
        # This will fail if compile_bfbridge.py was not run. Needs to compile with the same Python version.
        # Or if os.getcwd does not contain the shared object
        from ._bfbridge import ffi, lib
        return ffi, lib
    except ImportError as e:
        print("Error: BFBridge (BioFormats wrapper) import failed:")
        print(str(e))

        print("Trying changing working directory")
        try:
            previous_cwd = os.getcwd()
            os.chdir(Path(__file__).parent)
            from ._bfbridge import ffi, lib
            print("Successful")
            os.chdir(previous_cwd)
            return ffi, lib
        except ImportError:
            previous_cwd = None
        except:
            pass

        print("Trying to compile. Working directory: " + os.getcwd())
        from . import compile_bfbridge
        try:
            compile_bfbridge.compile_bfbridge()
        except BaseException as e2:
            print("BFBridge could not be compiled. Cannot proceed. Error:")
            print(str(e2))
            raise e
        print("BFBridge compilation seems to be successful. Retrying import.")

        try:
            from ._bfbridge import ffi, lib
            if previous_cwd:
                os.chdir(previous_cwd)
        except BaseException as e2:
            print("Importing after BFBridge compilation failed. Cannot proceed")
            print(str(e2))
            if previous_cwd:
                os.chdir(previous_cwd)
            raise e
        print("BFBridge loaded successfully after recompilation.")
        return ffi, lib

# channels = 3 or 4 supported currently
# interleaved: Boolean
# pixel_type: Integer https://github.com/ome/bioformats/blob/9cb6cfa/components/formats-api/src/loci/formats/FormatTools.java#L98
# interleaved: Boolean
# little_endian: Boolean
def make_image( \
        byte_arr, width, height, channels, interleaved, bioformats_pixel_type, little_endian):
    if bioformats_pixel_type > 8 or bioformats_pixel_type < 0:
        raise ValueError("make_pil_image: pixel_type out of range")
    # https://github.com/ome/bioformats/blob/9cb6cfaaa5361bc/components/formats-api/src/loci/formats/FormatTools.java#L98
    if bioformats_pixel_type < 2:
        bytes_per_pixel_per_channel = 1
    elif bioformats_pixel_type < 4:
        bytes_per_pixel_per_channel = 2
    elif bioformats_pixel_type < 7:
        bytes_per_pixel_per_channel = 4
    elif bioformats_pixel_type < 8:
        bytes_per_pixel_per_channel = 8
    elif bioformats_pixel_type < 9:
        # a bit type has 1 bit per byte and handle this early
        if channels != 1:
            raise ValueError("make_pil_image: bit type (8) supported only for Black&White")
        arr = np.array(byte_arr)
        arr = np.reshape(arr, (height, width))
        return Image.fromarray(arr, mode="1")

    if channels != 3 and channels != 4:
        raise ValueError("make_pil_image: only 3 or 4 channels supported currently")
    
    if len(byte_arr) != width * height * channels * bytes_per_pixel_per_channel:
        raise ValueError(
            "make_pil_image: Expected " + width + " * " + \
            height + " * " + channels + " * " + \
            bytes_per_pixel_per_channel + " got " + len(byte_arr) + " bytes"
            )

    if bioformats_pixel_type == 0:
        np_dtype = np.int8
    elif bioformats_pixel_type == 1:
        np_dtype = np.uint8
    elif bioformats_pixel_type == 2:
        np_dtype = np.int16
    elif bioformats_pixel_type == 3:
        np_dtype = np.uint16
    elif bioformats_pixel_type == 4:
        np_dtype = np.int32
    elif bioformats_pixel_type == 5:
        np_dtype = np.uint32
    elif bioformats_pixel_type == 6:
        np_dtype = np.float32
    elif bioformats_pixel_type == 7:
        np_dtype = np.float64
    
    #arr = np.array(byte_arr, dtype=np_dtype)

    dt = np.dtype(np_dtype.__name__)
    dt = dt.newbyteorder("little" if little_endian else "big")
    arr = np.frombuffer(byte_arr, dtype=dt)

    # float type
    if bioformats_pixel_type == 6 or bioformats_pixel_type == 7:
        arr_min = np.min(arr)
        arr_max = np.max(arr)
        # check if not already normalized
        if arr_min < 0 or arr_max > 1:
            arr = (arr - arr_min) / (arr_max - arr_min)
        arr_min = 0
        arr_max = 1

    # https://pillow.readthedocs.io/en/latest/handbook/concepts.html#concept-modes
    # pillow doesn't support float64, trim to float32
    if bioformats_pixel_type == 7:
        arr = np.float32(arr)
        bioformats_pixel_type = 6
        bytes_per_pixel_per_channel = 4
    
    # interleave
    if not interleaved:
        # subarrays of length (len(arr)/channels)
        arr = np.reshape(arr, (-1, len(arr) // channels))
        arr = np.transpose(arr)
        # flatten. -1 means infer
        arr = np.reshape(arr, (-1))

    # now handle types 0 to 6

    # for signed int types, make them unsigned:
    if bioformats_pixel_type < 6 and ((bioformats_pixel_type & 1) == 0):
        # 128 for int8 etc.
        offset = -np.iinfo(arr.dtype).min
        arr = arr + offset
        if bioformats_pixel_type == 0:
            arr = np.uint8(arr)
        elif bioformats_pixel_type == 2:
            arr = np.uint16(arr)
        elif bioformats_pixel_type == 4:
            arr = np.uint32(arr)
        bioformats_pixel_type += 1

    # now handle types 1, 3, 5, 6

    # PIL supports only 8 bit
    # 32 bit -> 8 bit:
    if bioformats_pixel_type == 5:
        arr = np.float64(arr) / 65536 / 256
        arr = np.rint(arr)
        arr = np.uint8(arr)
        bioformats_pixel_type = 1
        bytes_per_pixel_per_channel = 1
    # 16 bit -> 8 bit:
    if bioformats_pixel_type == 3:
        arr = np.float32(arr) / 256
        arr = np.rint(arr)
        arr = np.uint8(arr)
        bioformats_pixel_type = 1
        bytes_per_pixel_per_channel = 1
    # float -> 8 bit
    if bioformats_pixel_type == 6:
        # numpy rint rounds to nearest even integer when ends in 0.5
        arr *= 255.4999
        arr = np.rint(arr)
        arr = np.uint8(arr)
        bioformats_pixel_type = 1
        bytes_per_pixel_per_channel = 1

    arr = np.reshape(arr, (height,width,channels))
    #https://pillow.readthedocs.io/en/latest/reference/Image.html#PIL.Image.fromarray
    return Image.fromarray(arr, mode="RGB")
