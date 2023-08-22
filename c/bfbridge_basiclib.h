// bfbridge_basiclib.h

// Optionally define: BFBRIDGE_INLINE (makes it a header-only thread)
// Please note: in this case you're encouraged to call bfbridge_make_vm, bfbridge_make_thread
// and bfbridge_make_instance from a single compilation unit only to save from
// final executable size, or wrap it around a non-static caller which
// you can call from multiple files
// Please note: The best way to define BFBRIDGE_INLINE is through the flag
// -DBFBRIDGE_INLINE but it also works speed-wise to only #define BFBRIDGE_INLINE
// before you include it.

// Optionally define: BFBRIDGE_KNOW_BUFFER_LEN
// This will save some memory by not storing buffer length in our structs.

#ifndef BFBRIDGE_BASICLIB_H
#define BFBRIDGE_BASICLIB_H

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BFBRIDGE_INLINE
#define BFBRIDGE_INLINE_ME_EXTRA static
#else
#define BFBRIDGE_INLINE_ME_EXTRA
#endif

// As an example, inlining solves the issue that
// passing struct ptrs requires dereference https://stackoverflow.com/a/552250
// so without inline, we would need to pass instance pointers by value
#ifdef BFBRIDGE_INLINE
#define BFBRIDGE_INLINE_ME static inline
#else
#define BFBRIDGE_INLINE_ME
#endif

// The following marker is for our Python CFFI compiler:
// -----CFFI HEADER BEGIN-----

typedef enum bfbridge_error_code
{
    // Library & Thread initialization:
    BFBRIDGE_INVALID_CLASSPATH,
    BFBRIDGE_CLASS_NOT_FOUND,
    BFBRIDGE_METHOD_NOT_FOUND,
    // https://docs.oracle.com/en/java/javase/20/docs/specs/jni/functions.html#return-codes
    BFBRIDGE_JNI_ERR = -1, // = JNI_ERR,
    BFBRIDGE_JNI_EDETACHED = -2, // = JNI_EDETACHED,
    BFBRIDGE_JNI_EVERSION = -3, // = JNI_EVERSION,
    BFBRIDGE_JNI_ENOMEM = -4, // = JNI_ENOMEM,
    BFBRIDGE_JNI_EEXIST = -5, // = JNI_EEXIST,
    BFBRIDGE_JNI_EINVAL = -6, // = JNI_EINVAL,

    // Instance initialization:
    BFBRIDGE_INVALID_COMMUNICATON_BUFFER,
    BFBRIDGE_OUT_OF_MEMORY_ERROR,
    BFBRIDGE_JVM_LACKS_BYTE_BUFFERS,
    BFBRIDGE_LIBRARY_UNINITIALIZED, // previous step wasn't successfully completed
} bfbridge_error_code_t;

typedef struct bfbridge_error
{
    bfbridge_error_code_t code;
    char *description;
} bfbridge_error_t;

BFBRIDGE_INLINE_ME void bfbridge_free_error(bfbridge_error_t *);

typedef struct bfbridge_vm {
    JavaVM *jvm;
} bfbridge_vm_t;

// A process can call bfbridge_make_vm at most once
// cpdir: a string to a single directory containing jar files (and maybe classes)
// cachedir: NULL or the directory path to store file caches for faster opening
BFBRIDGE_INLINE_ME_EXTRA bfbridge_error_t *bfbridge_make_vm(bfbridge_vm_t *dest,
    char *cpdir,
    char *cachedir);

// Copies while making the freeing of the previous a noop.
// Do not use for moving from one thread/process to another
BFBRIDGE_INLINE_ME void bfbridge_move_vm(bfbridge_vm_t *dest, bfbridge_vm_t *from);

// Due to JVM restrictions, after bfbridge_free_vm, bfbridge_make_vm will fail with error code -1.
// Hence bfbridge_free_vm should not be called until the process will never call/construct JVM again.
BFBRIDGE_INLINE_ME void bfbridge_free_vm(bfbridge_vm_t*);

typedef struct bfbridge_thread
{
    bfbridge_vm_t *vm;
    JNIEnv *env;

    // Calling FindClass after this is set invalidates this pointer,
    // so please use this jclass variable instead of FindClass.
    jclass bfbridge_base;

    jmethodID constructor;

    // Please keep this list in order with javap output
    // See the comment "To print descriptors (encoded function types) ..."
    // for the javap command.
    // To add a new method:
    // 1) Modify bfbridge_thread_t
    // 2) Modify bfbridge_basiclib.h to add prototype
    // 3) Modify bfbridge_basiclib.c to add descriptor
    // 4) Modify bfbridge_basiclib.c to add function body
    jmethodID BFSetCommunicationBuffer;
    jmethodID BFGetErrorLength;
    jmethodID BFIsCompatible;
    jmethodID BFIsAnyFileOpen;
    jmethodID BFOpen;
    jmethodID BFGetFormat;
    jmethodID BFIsSingleFile;
    jmethodID BFGetCurrentFile;
    jmethodID BFGetUsedFiles;
    jmethodID BFClose;
    jmethodID BFGetSeriesCount;
    jmethodID BFSetCurrentSeries;
    jmethodID BFGetResolutionCount;
    jmethodID BFSetCurrentResolution;
    jmethodID BFGetSizeX;
    jmethodID BFGetSizeY;
    jmethodID BFGetSizeC;
    jmethodID BFGetSizeZ;
    jmethodID BFGetSizeT;
    jmethodID BFGetEffectiveSizeC;
    jmethodID BFGetImageCount;
    jmethodID BFGetDimensionOrder;
    jmethodID BFIsOrderCertain;
    jmethodID BFGetOptimalTileWidth;
    jmethodID BFGetOptimalTileHeight;
    jmethodID BFGetPixelType;
    jmethodID BFGetBitsPerPixel;
    jmethodID BFGetBytesPerPixel;
    jmethodID BFGetRGBChannelCount;
    jmethodID BFIsRGB;
    jmethodID BFIsInterleaved;
    jmethodID BFIsLittleEndian;
    jmethodID BFIsIndexedColor;
    jmethodID BFIsFalseColor;
    jmethodID BFGet8BitLookupTable;
    jmethodID BFGet16BitLookupTable;
    jmethodID BFOpenBytes;
    jmethodID BFOpenThumbBytes;
    jmethodID BFGetMPPX;
    jmethodID BFGetMPPY;
    jmethodID BFGetMPPZ;
    jmethodID BFDumpOMEXMLMetadata;
} bfbridge_thread_t;

// bfbridge_make_thread attaches the current thread to the JVM
// and fills *dest. Calling when already attached is a noop
// and it fills *dest if you lost it.
// On success, returns NULL and fills *dest
// On failure, returns error, and it may have modified *dest
BFBRIDGE_INLINE_ME_EXTRA bfbridge_error_t *bfbridge_make_thread(
    bfbridge_thread_t *dest,
    bfbridge_vm_t *vm);

// Copies the data while making the freeing of the previous a noop.
// Do not use for moving from one thread/process to another
// Dest: doesn't need to be initialized but allocated
// This function would benefit from restrict but if we inline, not necessary
BFBRIDGE_INLINE_ME void bfbridge_move_thread(
    bfbridge_thread_t *dest, bfbridge_thread_t *lib);

// Does not free the thread struct but its contents
// To move from one thread to another, free the current thread and call
// bfbridge_make_thread on the new thread (or in the opposite order)
// Important: bfbridge_make_thread can be called as many times in a single thread
// but calling bfbridge_free_thread once will free for the whole thread.
BFBRIDGE_INLINE_ME void bfbridge_free_thread(bfbridge_thread_t *);

// Almost all functions that need a bfbridge_instance_t must be passed
// the instance's related bfbridge_thread_t and not any other bfbridge_thread_t

typedef struct bfbridge_instance
{
    jobject bfbridge;
    char *communication_buffer;
#ifndef BFBRIDGE_KNOW_BUFFER_LEN
    int communication_buffer_len;
#endif
} bfbridge_instance_t;

// On success, returns NULL and fills *dest
// On failure, returns error, and it may have modified *dest
// (But on failure, it will still have set the communication_buffer
// and the communication_buffer_len, to help you free it)
// communication_buffer: Please allocate a char* from heap and pass it and its length.
// You should keep the pointer (or call bfbridge_instance_get_communication_buffer)
// and free it about the same time as bfbridge_free_instance or reuse it.
// communication_buffer:
// This will be used for two-way communication.
// Suggested length is 33 MB (33554432) to allow
// 2048 by 2048 four channels of 16 bit
// communication_buffer_len: You should pass this correctly
// even if you define BFBRIDGE_KNOW_BUFFER_LEN
BFBRIDGE_INLINE_ME_EXTRA bfbridge_error_t *bfbridge_make_instance(
    bfbridge_instance_t *dest,
    bfbridge_thread_t *thread,
    char *communication_buffer,
    int communication_buffer_len);

// Copies while making the freeing of the previous a noop
// Do not use for moving from one thread/process to another
// Dest: doesn't need to be initialized but allocated
// This function would benefit from restrict but if we inline, not necessary
BFBRIDGE_INLINE_ME void bfbridge_move_instance(
    bfbridge_instance_t *dest, bfbridge_instance_t *lib);

// bfbridge_free_thread removes the need to call bfbridge_free_instance.
// This function does not free communication_buffer, you should free it
// It also does not free the instance struct but its contents only.
BFBRIDGE_INLINE_ME void bfbridge_free_instance(
    bfbridge_instance_t *, bfbridge_thread_t *thread);

// returns the communication buffer
// sets *len, if the len ptr is not NULL, to the buffer length
// Usecases:
// - call next to bfbridge_free_instance to free the buffer
// - some of our thread functions would like to return a byte array
// so they return the number bytes to be read, an integer, and
// expect that the user will read from there.
BFBRIDGE_INLINE_ME char *bfbridge_instance_get_communication_buffer(
    bfbridge_instance_t *, int *len);

// Return a C string with the last error
// This should only be called when the last bf_* method returned an error code
// May otherwise return undisplayable characters
BFBRIDGE_INLINE_ME char *bf_get_error_convenience(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

// bf_get_error_length: fills the communication buffer with an error message
// returns: the number of bytes to read
BFBRIDGE_INLINE_ME int bf_get_error_length(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

// 1 if the filepath can be read by BioFormats otherwise 0
BFBRIDGE_INLINE_ME int bf_is_compatible(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    char *filepath, int filepath_len);

BFBRIDGE_INLINE_ME int bf_is_any_file_open(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_open(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    char *filepath, int filepath_len);

BFBRIDGE_INLINE_ME int bf_get_format(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

// You may use bf_get_used_files for already opened files instead
BFBRIDGE_INLINE_ME int bf_is_single_file(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    char *filepath, int filepath_len);

BFBRIDGE_INLINE_ME int bf_get_current_file(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_get_used_files(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_close(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_get_series_count(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_set_current_series(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int ser);

BFBRIDGE_INLINE_ME int bf_get_resolution_count(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_set_current_resolution(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int res);

BFBRIDGE_INLINE_ME int bf_get_size_x(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_get_size_y(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_get_size_c(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_get_size_z(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_get_size_t(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_get_effective_size_c(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

// https://downloads.openmicroscopy.org/bio-formats/latest/api/loci/formats/ImageReader.html#openBytes(int)
// getEffectiveSizeC() * getSizeZ() * getSizeT() == getImageCount()
BFBRIDGE_INLINE_ME int bf_get_image_count(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_get_dimension_order(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_is_order_certain(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_get_optimal_tile_width(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_get_optimal_tile_height(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

/*
https://github.com/ome/bioformats/blob/9cb6cfaaa5361bcc4ed9f9841f2a4caa29aad6c7/components/formats-api/src/loci/formats/FormatTools.java#L98
You may use this to determine number of bytes in a pixel or if float type
*/
BFBRIDGE_INLINE_ME int bf_get_pixel_type(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_get_bits_per_pixel(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_get_bytes_per_pixel(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_get_rgb_channel_count(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_is_rgb(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_is_interleaved(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_is_little_endian(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

/*
    isindexed false, isfalsecolor false -> no table
    isindexed true, isfalsecolor false -> table must be read
    isindexed true, isfalsecolor true -> table can be read for precision, not obligatorily
*/
BFBRIDGE_INLINE_ME int bf_is_indexed_color(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_is_false_color(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

// https://downloads.openmicroscopy.org/bio-formats/latest/api/loci/formats/ImageReader.html#get8BitLookupTable--
BFBRIDGE_INLINE_ME int bf_get_8_bit_lookup_table(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

// Little endian
BFBRIDGE_INLINE_ME int bf_get_16_bit_lookup_table(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_open_bytes(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int plane, int x, int y, int w, int h);

BFBRIDGE_INLINE_ME int bf_open_thumb_bytes(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int plane, int w, int h);

BFBRIDGE_INLINE_ME double bf_get_mpp_x(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int series);

BFBRIDGE_INLINE_ME double bf_get_mpp_y(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int series);

BFBRIDGE_INLINE_ME double bf_get_mpp_z(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int series);

BFBRIDGE_INLINE_ME int bf_dump_ome_xml_metadata(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

BFBRIDGE_INLINE_ME int bf_tools_should_generate(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread);

// -----CFFI HEADER END-----
// The marker above is for our Python CFFI compiler

/*
How is the communication buffer used internally?
Some functions receive from BioFormats through the communication buffer
Some functions communicate to BioFormats (such as passing a filename) through it
Some functions don't use the buffer at all
bf_open_bytes, for example, that reads a region to bytes,
receives bytes to the communication buffer. Returns an int,
bytes to be read, which then the user of the library should read from the buffer.
 */

#ifdef BFBRIDGE_INLINE
#define BFBRIDGE_HEADER
#include "bfbridge_basiclib.c"
#undef BFBRIDGE_HEADER
#define BFBRIDGE_PROVIDED_SOURCE
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#else // BFBRIDGE_BASICLIB_H
// Display error if this header was included earlier but only now header-only mode
// is requested
#if defined(BFBRIDGE_INLINE) && !defined(BFBRIDGE_PROVIDED_SOURCE)
#error "bfbridge_basiclib.h was already "#include"d in non-header-only mode"
#endif // BFBRIDGE_PROVIDED_SOURCE

#endif // BFBRIDGE_BASICLIB_H