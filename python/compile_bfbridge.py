from cffi import FFI
from pathlib import Path
import sys
import os

def compile_bfbridge():
    # API mode out-of-line
    # https://cffi.readthedocs.io/en/latest/overview.html#purely-for-performance-api-level-out-of-line
    ffibuilder = FFI()

    try:
        bfbridge_source = Path('bfbridge_basiclib.c').read_text()
        bfbridge_header = Path('bfbridge_basiclib.h').read_text()
        bfbridge_cffi_prefix = Path('bfbridge_cffi_prefix.h').read_text()
    except:
        raise RuntimeError("bfbridge_basiclib.c and/or bfbridge_basiclib.h and/or bfbridge_cffi_prefix.h could not be found")

    header_begin = "CFFI HEADER BEGIN"
    header_end = "CFFI HEADER END"

    try:
        header_begin_index = bfbridge_header.index(header_begin)
        header_end_index = bfbridge_header.index(header_end)
    except:
        raise RuntimeError("bfbridge_basiclib.h CFFI markers could not be found")

    # Fix header_begin_index: beginning from the middle of a "//"
    # comment produces an invalid header.
    header_begin_index = bfbridge_header.index("\n", header_begin_index)

    bfbridge_header = bfbridge_header[header_begin_index:header_end_index]
    bfbridge_header = bfbridge_cffi_prefix + "\n" + bfbridge_header


    # https://stackoverflow.com/q/31795394
    # ffibuilder.cdef, unlike set_source, does not
    # call the compiler, but tries to parse it.
    # Hence we can't use the standard header.

    bfbridge_header = bfbridge_header.replace("BFBRIDGE_INLINE_ME_EXTRA", "").replace("BFBRIDGE_INLINE_ME", "").replace("#ifndef BFBRIDGE_KNOW_BUFFER_LEN", "").replace("#endif", "")

    #print("Header: " + bfbridge_header)

    ffibuilder.cdef(bfbridge_header)

    if "JAVA_HOME" not in os.environ:
        raise RuntimeError("Please set JAVA_HOME environment variable to a JDK Home directory")

    java_home = os.path.join(os.environ['JAVA_HOME'])
    java_include = os.path.join(java_home, "include")
    java_link = os.path.join(java_home, "lib/server")

    print("JAVA_HOME: " + java_home)

    # jni-md.h should also be included hence the platform paths, see link in https://stackoverflow.com/a/37029528
    # https://github.com/openjdk/jdk/blob/6e3cc131daa9f3b883164333bdaad7aa3a6ca018/src/jdk.hotspot.agent/share/classes/sun/jvm/hotspot/utilities/PlatformInfo.java#L32
    java_include = [java_include, os.path.join(java_include, "linux"), os.path.join(java_include, "darwin"), os.path.join(java_include, "win32"), os.path.join(java_include, "bsd")]

    extra_link_args = []

    if os.name != "nt":
        extra_link_args.append("-Wl,-rpath," + java_link) # libjvm.so
        if sys.platform == "darwin":
            extra_link_args.append("-Wl,-install_name,@rpath/libjvm.dylib")

        # build fails without these when calling compiler manually
        # but somehow fails silently in this script.
        extra_link_args.append("-ljvm")
        extra_link_args.append("-L" + java_link)

    ffibuilder.set_source("_bfbridge", bfbridge_source, extra_link_args=extra_link_args, include_dirs=java_include, libraries=["jvm"])

    ffibuilder.compile(verbose=True)

if __name__ == "__main__":
    compile_bfbridge()
