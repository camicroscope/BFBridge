import os
import threading
from . import gtm, utils
ffi, lib = utils.IMPORT_BFBRIDGE()


# Can be created only once during a Python process lifetime.
# Once it's destroyed it cannot be recreated in the same process
class BFBridgeVM:
    def __init__(self):
        self.bfbridge_vm =  ffi.new("bfbridge_vm_t*")
        cpdir = os.environ.get("BFBRIDGE_CLASSPATH")
        if cpdir is None or cpdir == "":
            raise RuntimeError("Please set BFBRIDGE_CLASSPATH env var to a single dir containing the jar files")
        cpdir_arg = ffi.new("char[]", cpdir.encode())

        cachedir = os.environ.get("BFBRIDGE_CACHEDIR")
        cachedir_arg = ffi.NULL
        if cachedir is not None and cachedir != "":
            cachedir_arg = ffi.new("char[]", cachedir.encode())

        potential_error = lib.bfbridge_make_vm(self.bfbridge_vm, cpdir_arg, cachedir_arg)
        if potential_error != ffi.NULL:
            err = ffi.string(potential_error[0].description)
            lib.bfbridge_free_error(potential_error)
            raise RuntimeError(err)
        self.owner_pid = os.getpid()

    def __del__(self):
        # Additionally we can check if now it's the same pid that created this object
        if hasattr(self, "bfbridge_vm"):
            lib.bfbridge_free_vm(self.bfbridge_vm)

# You may create this object as many
# times as needed even in a single thread, and the thread will
# detach only after all of them are destroyed.
# If you would like to keep the Java thread
# alive, please do:
# thread_holder = threading.local()
# thread_holder.thread = BFBridgeThread(vm)
# ...
# # when no longer needed
# del thread_holder.thread
# You may find that reusing BFBridgeThread
# has a performance benefit
class BFBridgeThread:
    def __init__(self, bfbridge_vm):
        if bfbridge_vm is None:
            raise ValueError("BFBridgeInstance must be initialized with BFBridgeThread")

        self.owner_pid = os.getpid()
        if bfbridge_vm.owner_pid != self.owner_pid:
            raise RuntimeError("JVM was created in a different process")

        self.bfbridge_thread = ffi.new("bfbridge_thread_t*")
        potential_error = lib.bfbridge_make_thread(self.bfbridge_thread, bfbridge_vm.bfbridge_vm)
        if potential_error != ffi.NULL:
            err = ffi.string(potential_error[0].description)
            lib.bfbridge_free_error(potential_error)
            raise RuntimeError(err)
        self.owner_thread = threading.get_native_id()
        gtm.change_ref_count(self.owner_thread, 1)

    def __copy__(self):
        raise RuntimeError("BFBridgeThread cannot be copied")

    def __deepcopy__(self):
        raise RuntimeError("BFBridgeThread cannot be copied")

    # Before Python 3.4: https://stackoverflow.com/a/8025922
    # Now we can define __del__
    def __del__(self):
        if gtm.change_ref_count(self.owner_thread, -1) == 0:
            lib.bfbridge_free_thread(self.bfbridge_thread)

# An instance can be used with only the thread object it was constructed with
class BFBridgeInstance:
    def __init__(self, bfbridge_thread):
        if bfbridge_thread is None:
            raise ValueError("BFBridgeInstance must be initialized with BFBridgeThread")

        self.owner_thread = threading.get_native_id()
        if bfbridge_thread.owner_thread != self.owner_thread:
            raise RuntimeError("BFBridgeInstance was supplied a bfbridge_thread from a different thread")

        self.bfbridge_thread = bfbridge_thread.bfbridge_thread
        self.bfbridge_instance = ffi.new("bfbridge_instance_t*")
        self.communication_buffer = ffi.new("char[34000000]")
        self.communication_buffer_len = 34000000
        potential_error = lib.bfbridge_make_instance(
            self.bfbridge_instance,
            self.bfbridge_thread,
            self.communication_buffer,
            self.communication_buffer_len)
        if potential_error != ffi.NULL:
            err = ffi.string(potential_error[0].description)
            lib.bfbridge_free_error(potential_error)
            raise RuntimeError(err)

    def __copy__(self):
        raise RuntimeError("Copying a BFBridgeInstance might not work")

    def __deepcopy__(self):
        raise RuntimeError("Copying a BFBridgeInstance might not work")

    def __del__(self):
        if hasattr(self, "bfbridge_instance"):
            lib.bfbridge_free_instance(self.bfbridge_instance, self.bfbridge_thread)

    def __return_from_buffer(self, length, isString):
        if length < 0:
            print(self.get_error_string())
            raise ValueError(self.get_error_string())
        if isString:
            return ffi.unpack(self.communication_buffer, length).decode("utf-8")
            # or ffi.string after if we set the null byte here
        else:
            return ffi.buffer(self.communication_buffer, length)

    def __boolean(self, integer):
        if integer == 1:
            return True
        elif integer == 0:
            return False
        else:
            print(self.get_error_string())
            raise RuntimeError(self.get_error_string())

    # Should be called only just after the last method call returned an error code
    def get_error_string(self):
        length = lib.bf_get_error_length(self.bfbridge_instance, self.bfbridge_thread)
        return self.__return_from_buffer(length, True)
    
    def is_compatible(self, filepath):
        filepath = filepath.encode()
        file = ffi.new("char[]", filepath)
        filepathlen = len(file) - 1
        res = lib.bf_is_compatible(self.bfbridge_instance, self.bfbridge_thread, filepath, filepathlen)
        return self.__boolean(res)
    
    def open(self, filepath):
        filepath = filepath.encode()
        file = ffi.new("char[]", filepath)
        filepathlen = len(file) - 1
        res = lib.bf_open(self.bfbridge_instance, self.bfbridge_thread, filepath, filepathlen)
        print(self.get_error_string(), flush=True)
        return res
    
    def get_format(self):
        length = lib.bf_get_format(self.bfbridge_instance, self.bfbridge_thread)
        return self.__return_from_buffer(length, True)

    def close(self):
        return lib.bf_close(self.bfbridge_instance, self.bfbridge_thread)

    def get_series_count(self):
        return lib.bf_get_series_count(self.bfbridge_instance, self.bfbridge_thread)

    def set_current_series(self, ser):
        return lib.bf_set_current_series(self.bfbridge_instance, self.bfbridge_thread, ser)
    
    def get_resolution_count(self):
        return lib.bf_get_resolution_count(self.bfbridge_instance, self.bfbridge_thread)

    def set_current_resolution(self, res):
        return lib.bf_set_current_resolution(self.bfbridge_instance, self.bfbridge_thread, res)
    
    def get_size_x(self):
        return lib.bf_get_size_x(self.bfbridge_instance, self.bfbridge_thread)

    def get_size_y(self):
        return lib.bf_get_size_y(self.bfbridge_instance, self.bfbridge_thread)

    def get_size_z(self):
        return lib.bf_get_size_z(self.bfbridge_instance, self.bfbridge_thread)

    def get_size_c(self):
        return lib.bf_get_size_c(self.bfbridge_instance, self.bfbridge_thread)

    def get_size_t(self):
        return lib.bf_get_size_t(self.bfbridge_instance, self.bfbridge_thread)

    def get_effective_size_c(self):
        return lib.bf_get_effective_size_c(self.bfbridge_instance, self.bfbridge_thread)

    def get_image_count(self):
        return lib.bf_get_image_count(self.bfbridge_instance, self.bfbridge_thread)

    def get_dimension_order(self):
        length = lib.bf_get_dimension_order(self.bfbridge_instance, self.bfbridge_thread)
        return self.__return_from_buffer(length, True)

    def is_order_certain(self):
        return self.__boolean(lib.bf_is_order_certain(self.bfbridge_instance, self.bfbridge_thread))

    def get_optimal_tile_width(self):
        return lib.bf_get_optimal_tile_width(self.bfbridge_instance, self.bfbridge_thread)
    
    def get_optimal_tile_height(self):
        return lib.bf_get_optimal_tile_height(self.bfbridge_instance, self.bfbridge_thread)
    
    def get_pixel_type(self):
        return lib.bf_get_optimal_tile_height(self.bfbridge_instance, self.bfbridge_thread)
    
    def get_pixel_type(self):
        return lib.bf_get_pixel_type(self.bfbridge_instance, self.bfbridge_thread)
    
    def get_bits_per_pixel(self):
        return lib.bf_get_bits_per_pixel(self.bfbridge_instance, self.bfbridge_thread)

    def get_bytes_per_pixel(self):
        return lib.bf_get_bytes_per_pixel(self.bfbridge_instance, self.bfbridge_thread)

    def get_rgb_channel_count(self):
        return lib.bf_get_rgb_channel_count(self.bfbridge_instance, self.bfbridge_thread)

    def is_rgb(self):
        return self.__boolean(lib.bf_is_rgb(self.bfbridge_instance, self.bfbridge_thread))

    def is_interleaved(self):
        return self.__boolean(lib.bf_is_interleaved(self.bfbridge_instance, self.bfbridge_thread))
    
    def is_little_endian(self):
        return self.__boolean(lib.bf_is_little_endian(self.bfbridge_instance, self.bfbridge_thread))

    def is_indexed_color(self):
        return self.__boolean(lib.bf_is_indexed_color(self.bfbridge_instance, self.bfbridge_thread))
    
    def is_false_color(self):
        return self.__boolean(lib.bf_is_false_color(self.bfbridge_instance, self.bfbridge_thread))
    
    # TODO return a 2D array, handle sign depending on pixel type
    def get_8_bit_lookup_table(self):
        return self.__return_from_buffer(lib.bf_get_8_bit_lookup_table(self.bfbridge_instance, self.bfbridge_thread), False)
    
    # TODO return a 2D array, handle little endianness, handle sign depending on pixel type
    def get_16_bit_lookup_table(self):
        return self.__return_from_buffer(lib.bf_get_8_bit_lookup_table(self.bfbridge_instance, self.bfbridge_thread), False)
    
    # plane = 0 in general
    def open_bytes(self, plane, x, y, w, h):
        return self.__return_from_buffer(lib.bf_open_bytes(self.bfbridge_instance, self.bfbridge_thread, plane, x, y, w, h), False)

    def open_bytes_pil_image(self, plane, x, y, w, h):
        byte_arr = self.open_bytes(plane, x, y, w, h)
        return utils.make_pil_image( \
            byte_arr, w, h, self.get_rgb_channel_count(), \
            self.is_interleaved(), self.get_pixel_type(), \
            self.is_little_endian())
    
    def open_thumb_bytes(self, plane, w, h):
        return self.__return_from_buffer( \
            lib.bf_open_thumb_bytes( \
            self.bfbridge_instance, self.bfbridge_thread, plane, w, h), False)
    
    def open_thumb_bytes_pil_image(self, plane, max_w, max_h):
        img_h = self.get_size_y()
        img_w = self.get_size_x()
        y_over_x = img_h / img_w;
        x_over_y = 1 / y_over_x;
        w = min(max_w, round(max_h * x_over_y));
        h = min(max_h, round(max_w * y_over_x));
        if h > img_h or w > img_w:
            h = img_h
            w = img_w
        byte_arr = self.open_thumb_bytes(plane, w, h)

        # Thumbnails can't be signed int in BioFormats
        # https://github.com/ome/bioformats/blob/19d7fb9cbfdc1/components/formats-api/src/loci/formats/FormatTools.java#L1322
        pixel_type = self.get_pixel_type()
        if pixel_type == 0 or pixel_type == 2 or pixel_type == 4:
            pixel_type += 1

        return utils.make_pil_image( \
            byte_arr, w, h, self.get_rgb_channel_count(), \
            self.is_interleaved(), pixel_type, \
            self.is_little_endian())

    def get_mpp_x(self, no):
        return lib.bf_get_mpp_x(self.bfbridge_instance, self.bfbridge_thread, no)
    
    def get_mpp_y(self, no):
        return lib.bf_get_mpp_y(self.bfbridge_instance, self.bfbridge_thread, no)

    def get_mpp_z(self, no):
        return lib.bf_get_mpp_z(self.bfbridge_instance, self.bfbridge_thread, no)

    def dump_ome_xml_metadata(self):
        length = lib.bf_dump_ome_xml_metadata(self.bfbridge_instance, self.bfbridge_thread)
        return self.__return_from_buffer(length, True)
