// bfbridge_basiclib.c

// If inlining but erroneously still compiling .c, make it empty
#if !(defined(BFBRIDGE_INLINE) && !defined(BFBRIDGE_HEADER))

#include "bfbridge_basiclib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef WIN32
#include <windows.h>
#define BFBRIDGE_JNI_PATH_SEPARATOR_STR ";"
#define BFBRIDGE_PATH_SEPARATOR '\\'
#else
#include <dirent.h>
#define BFBRIDGE_JNI_PATH_SEPARATOR_STR ":"
#define BFBRIDGE_PATH_SEPARATOR '/'
#endif

// Define BFENVA - BF ENV Access
// In the single-header mode of operation, we need to be able to
// call JNI in its both modes
// C++: env->JNIMethodABC..(x,y,z)
// C: (*env)->JNIMethodABC..(env, x,y,z)
// This can also be used for JVM*, such as (*jvm)->DestroyJavaVM(jvm);
// Define a second one (ENV Access Void), BFENVAV, for no args,
// as __VA_ARGS__ requires at least one
#ifdef __cplusplus
#define BFENVA(env_ptr, method_name, ...) \
    ((env_ptr)->method_name(__VA_ARGS__))
#define BFENVAV(env_ptr, method_name) \
    ((env_ptr)->method_name())
#else
#define BFENVA(env_ptr, method_name, ...) \
    ((*(env_ptr))->method_name((env_ptr), __VA_ARGS__))
#define BFENVAV(env_ptr, method_name, ...) \
    ((*(env_ptr))->method_name((env_ptr)))
#endif

void bfbridge_free_error(bfbridge_error_t *error)
{
    free(error->description);
    free(error);
}

typedef struct bfbridge_basiclib_string
{
    char *str;
    int len;
    int alloc_len;
} bfbridge_basiclib_string_t;

static bfbridge_basiclib_string_t *allocate_string(const char *initial)
{
    int initial_len = strlen(initial);
    bfbridge_basiclib_string_t *bbs =
        (bfbridge_basiclib_string_t *)
            malloc(sizeof(bfbridge_basiclib_string_t));
    bbs->alloc_len = 2 * initial_len;
    bbs->str = (char *)malloc(bbs->alloc_len * sizeof(char) + 1);
    bbs->len = initial_len;
    strcpy(bbs->str, initial);
    return bbs;
}

static void free_string(bfbridge_basiclib_string_t *bbs)
{
    free(bbs->str);
    free(bbs);
}

static void append_to_string(bfbridge_basiclib_string_t *bbs, const char *s)
{
    int s_len = strlen(s);
    int required_len = s_len + bbs->len;
    if (bbs->alloc_len < required_len)
    {
        do
        {
            bbs->alloc_len = bbs->alloc_len + bbs->alloc_len / 2;
        } while ((bbs->alloc_len < required_len));
        bbs->str = (char *)realloc(bbs->str, bbs->alloc_len + 1);
    }
    strcat(bbs->str, s);
    bbs->len += s_len;
}

// description: optional string to be appended to end of operation
// Note: make_error copies strings supplied to it, so you should free them
// after calling make_error, before returning it
static bfbridge_error_t *make_error(
    bfbridge_error_code_t code,
    const char *operation,
    const char *description)
{
    bfbridge_error_t *error = (bfbridge_error_t *)malloc(sizeof(bfbridge_error_t));
    error->code = code;
    bfbridge_basiclib_string_t *desc = allocate_string(operation);
    if (description)
    {
        append_to_string(desc, description);
    }
    error->description = desc->str;
    return error;
}

bfbridge_error_t *bfbridge_make_vm(bfbridge_vm_t *dest,
    char *cpdir,
    char *cachedir)
{
    // Ease of freeing
    dest->jvm = NULL;

    if (cpdir == NULL || cpdir[0] == '\0')
    {
        return make_error(BFBRIDGE_INVALID_CLASSPATH, "bfbridge_make_thread: no classpath supplied", NULL);
    }

    // Plus one if needed for the purpose below, and one for nullchar
    int cp_len = strlen(cpdir);
    char cp[cp_len + 1 + 1];
    strcpy(cp, cpdir);
    // To be able to append filenames to the end of path,
    // it needs the optional "/" at the end
    if (cp[cp_len - 1] != BFBRIDGE_PATH_SEPARATOR)
    {
        cp[cp_len++] = BFBRIDGE_PATH_SEPARATOR;
        cp[cp_len] = 0;
    }

    // Should be freed: path_arg
    bfbridge_basiclib_string_t *path_arg = allocate_string("-Djava.class.path=");

    append_to_string(path_arg, cp);

    // Also add .../*, ending with asterisk, just in case
    append_to_string(path_arg, BFBRIDGE_JNI_PATH_SEPARATOR_STR);
    append_to_string(path_arg, cp);
    append_to_string(path_arg, "*");

// But for some reason unlike the -cp arg, .../* does not work
// so we need to list every jar file
// https://en.cppreference.com/w/cpp/filesystem/directory_iterator
#ifdef WIN32
#error "directory file listing not implemented on Windows"
#else
    DIR *cp_dir = opendir(cp);
    if (!cp_dir)
    {
        free_string(path_arg);
        return make_error(BFBRIDGE_INVALID_CLASSPATH, "bfbridge_make_thread: a single classpath folder containing jars was expected but got ", cp);
    }
    struct dirent *cp_dirent;
    // BUG: one of the cp_dirent is "..", the parent folder
    while ((cp_dirent = readdir(cp_dir)) != NULL)
    {
        append_to_string(path_arg, BFBRIDGE_JNI_PATH_SEPARATOR_STR);
        append_to_string(path_arg, cp);
        append_to_string(path_arg, cp_dirent->d_name);
    }
    closedir(cp_dir);
#endif

    JavaVMOption options[3];

    //fprintf(stderr, "Java classpath (BFBRIDGE_CLASSPATH): %s\n", path_arg->str);
    // https://docs.oracle.com/en/java/javase/20/docs/specs/man/java.html#performance-tuning-examples
    char optimize1[] = "-XX:+UseParallelGC";
    // char optimize2[] = "-XX:+UseLargePages"; Not compatible with our linux distro
    options[0].optionString = path_arg->str;
    options[1].optionString = optimize1;
    // Note: please make sure to update the array size above and nOptions below
    // to uncomment these
    // options[2].optionString = "-verbose:jni";
    // options[3].optionString = "-Xcheck:jni";
    JavaVMInitArgs vm_args;
    vm_args.version = JNI_VERSION_20;
    vm_args.nOptions = 2;
    vm_args.options = options;
    vm_args.ignoreUnrecognized = 0;

    // Should be freed: path_arg, cache_arg
    bfbridge_basiclib_string_t *cache_arg = allocate_string("-Dbfbridge.cachedir=");

	__builtin_printf("xyz check cache\n");
    if (cachedir && cachedir[0] != '\0')
    {
	__builtin_printf("xyz check cache yes %s\n", cachedir);
        // Remember that pointers we use must be valid until JNI_CreateJavaVM.
        append_to_string(cache_arg, cachedir);
        options[vm_args.nOptions++].optionString = cache_arg->str;
    }
	__builtin_printf("xyz check cache continue\n");

    // Check if JVM already exists
    /*{
    JavaVM* vmbuf[3];
    jsize x = 0;
    int code = JNI_GetCreatedJavaVMs(vmbuf, 3, &x);
    printf("check code: %d %d\n", code, x);
    }*/

    JavaVM *jvm;
    JNIEnv *env;

    // Should be freed: path_arg, cache_arg, jvm
    int code = JNI_CreateJavaVM(&jvm, (void **)&env, &vm_args);

    free_string(cache_arg);

    if (code < 0)
    {
        free_string(path_arg);
        // Make error code positive:
        char code_string[2] = {(char)('0' + (-code)), 0};
        if (code > 9) {
            // This code supports only 1 digit now.
            code_string[0] = 0;
        }

        // Handle "Other error"
        // No, this does not work on -1
        /*if (BFENVAV(env, ExceptionCheck) == 1)
        {
            BFENVAV(env, ExceptionDescribe);
        }*/

        return make_error((bfbridge_error_code_t)code, "JNI_CreateJavaVM failed, please see https://docs.oracle.com/en/java/javase/20/docs/specs/jni/functions.html#return-codes for error code description: -", code_string);
    }

    // Should be freed: jvm, path_arg

    // Verify this early
    jclass bfbridge_base = BFENVA(env, FindClass, "org/camicroscope/BFBridge");
    if (!bfbridge_base)
    {
        bfbridge_basiclib_string_t *error = allocate_string("FindClass failed because org.camicroscope.BFBridge (or a dependency of it) could not be found. Are the jars in: ");
        append_to_string(error, path_arg->str);

        if (BFENVAV(env, ExceptionCheck) == 1)
        {
            BFENVAV(env, ExceptionDescribe);
            append_to_string(error, " An exception was printed to stderr.");
        }

        free_string(path_arg);
        BFENVAV(jvm, DestroyJavaVM);

        bfbridge_error_t *err = make_error(BFBRIDGE_CLASS_NOT_FOUND, error->str, NULL);
        free_string(error);
        return err;
    }

    free_string(path_arg);
    dest->jvm = jvm;
    return NULL;
}

void bfbridge_move_vm(bfbridge_vm_t *dest, bfbridge_vm_t *from)
{
    if (from->jvm) {
        *dest = *from;
        from->jvm = NULL;
    } else {
        dest->jvm = NULL;
    }
}

void bfbridge_free_vm(bfbridge_vm_t *dest)
{
    if (dest->jvm) {
        BFENVAV(dest->jvm, DestroyJavaVM);
        dest->jvm = NULL;
    }
}

bfbridge_error_t *bfbridge_make_thread(
    bfbridge_thread_t *dest, bfbridge_vm_t *vm)
{
    // Ease of freeing
    dest->env = NULL;

    if (!vm->jvm) {
        return make_error(BFBRIDGE_LIBRARY_UNINITIALIZED, "bfbridge_make_thread requires successful bfbridge_make_vm", NULL);
    }

    dest->vm = vm;
    JNIEnv *env;
    jint code = BFENVA(vm->jvm, AttachCurrentThread, (void **)&env, NULL);

    if (code < 0) {
        char code_string[2] = {(char)('0' + (-code)), 0};
        if (code > 9) {
            // This code supports only 1 digit now.
            code_string[0] = 0;
        }
        return make_error((bfbridge_error_code_t)code, "AttachCurrentThread failed, please see https://docs.oracle.com/en/java/javase/20/docs/specs/jni/functions.html#return-codes for error code description: -", code_string);
    }

    // Should be freed: current thread (to be detached)

    jclass bfbridge_base = BFENVA(env, FindClass, "org/camicroscope/BFBridge");
    if (!bfbridge_base)
    {
        const char *note = NULL;
        if (BFENVAV(env, ExceptionCheck) == 1)
        {
            BFENVAV(env, ExceptionDescribe);
            note = " An exception was printed to stderr.";
        }

        BFENVAV(vm->jvm, DetachCurrentThread);

        return make_error(BFBRIDGE_CLASS_NOT_FOUND, "FindClass failed because org.camicroscope.BFBridge (or a dependency of it) could not be found.", note);
    }

    dest->bfbridge_base = bfbridge_base;

    dest->constructor = BFENVA(env, GetMethodID, bfbridge_base, "<init>", "()V");
    if (!dest->constructor)
    {
        BFENVAV(vm->jvm, DetachCurrentThread);
        return make_error(BFBRIDGE_METHOD_NOT_FOUND, "Could not find BFBridge constructor", NULL);
    }

    const char *method_cannot_be_found = NULL;

    // Now do the same for methods but in shorthand form
#define prepare_method_id(name, descriptor)                                 \
    dest->name =                                                            \
        BFENVA(env, GetMethodID, bfbridge_base, #name, (char *)descriptor); \
    if (!dest->name)                                                        \
    {                                                                       \
        method_cannot_be_found = #name;                                     \
        goto prepare_method_error;                                          \
    }

    if (0) {
    prepare_method_error:
        BFENVAV(vm->jvm, DetachCurrentThread);
        return make_error(
            BFBRIDGE_METHOD_NOT_FOUND,                              
            "Please check and update the method and/or the descriptor, as currently it cannot be found, for the method: ",                      
            method_cannot_be_found);
    }

    // To print descriptors (encoded function types) to screen
    // ensure that curent dir's org/camicroscope folder has BFBridge.class
    // otherwise, compile in the parent folder of "org" with:
    // "javac -cp ".:jar_files/*" org/camicroscope/BFBridge.java".
    // Run: javap -s (-p) org.camicroscope.BFBridge

    prepare_method_id(BFSetCommunicationBuffer, "(Ljava/nio/ByteBuffer;)V");
    prepare_method_id(BFGetErrorLength, "()I");
    prepare_method_id(BFIsCompatible, "(I)I");
    prepare_method_id(BFIsAnyFileOpen, "()I");
    prepare_method_id(BFOpen, "(I)I");
    prepare_method_id(BFGetFormat, "()I");
    prepare_method_id(BFIsSingleFile, "(I)I");
    prepare_method_id(BFGetCurrentFile, "()I");
    prepare_method_id(BFGetUsedFiles, "()I");
    prepare_method_id(BFClose, "()I");
    prepare_method_id(BFGetSeriesCount, "()I");
    prepare_method_id(BFSetCurrentSeries, "(I)I");
    prepare_method_id(BFGetResolutionCount, "()I");
    prepare_method_id(BFSetCurrentResolution, "(I)I");
    prepare_method_id(BFGetSizeX, "()I");
    prepare_method_id(BFGetSizeY, "()I");
    prepare_method_id(BFGetSizeC, "()I");
    prepare_method_id(BFGetSizeZ, "()I");
    prepare_method_id(BFGetSizeT, "()I");
    prepare_method_id(BFGetEffectiveSizeC, "()I");
    prepare_method_id(BFGetImageCount, "()I");
    prepare_method_id(BFGetDimensionOrder, "()I");
    prepare_method_id(BFIsOrderCertain, "()I");
    prepare_method_id(BFGetOptimalTileWidth, "()I");
    prepare_method_id(BFGetOptimalTileHeight, "()I");
    prepare_method_id(BFGetPixelType, "()I");
    prepare_method_id(BFGetBitsPerPixel, "()I");
    prepare_method_id(BFGetBytesPerPixel, "()I");
    prepare_method_id(BFGetRGBChannelCount, "()I");
    prepare_method_id(BFIsRGB, "()I");
    prepare_method_id(BFIsInterleaved, "()I");
    prepare_method_id(BFIsLittleEndian, "()I");
    prepare_method_id(BFIsIndexedColor, "()I");
    prepare_method_id(BFIsFalseColor, "()I");
    prepare_method_id(BFGet8BitLookupTable, "()I");
    prepare_method_id(BFGet16BitLookupTable, "()I");
    prepare_method_id(BFOpenBytes, "(IIIII)I");
    prepare_method_id(BFOpenThumbBytes, "(III)I");
    prepare_method_id(BFGetMPPX, "(I)D");
    prepare_method_id(BFGetMPPY, "(I)D");
    prepare_method_id(BFGetMPPZ, "(I)D");
    prepare_method_id(BFDumpOMEXMLMetadata, "()I");

    // Ease of freeing: keep null until we can return without error
    dest->env = env;

    return NULL;
}

void bfbridge_move_thread(bfbridge_thread_t *dest, bfbridge_thread_t *thread)
{
    // Ease of freeing
    if (thread->env)
    {
        *dest = *thread;
        thread->env = NULL;
    }
    else
    {
        dest->env = NULL;
    }
}

void bfbridge_free_thread(bfbridge_thread_t *dest)
{
    // Ease of freeing
    if (dest->env && dest->vm->jvm)
    {
        /*int code =*/BFENVAV(dest->vm->jvm, DetachCurrentThread);
        // printf("DetachCurrentThread: return code should be zero %d\n\n", code);
        dest->env = NULL;
    }
}

bfbridge_error_t *bfbridge_make_instance(
    bfbridge_instance_t *dest,
    bfbridge_thread_t *thread,
    char *communication_buffer,
    int communication_buffer_len)
{
    // Ease of freeing
    dest->bfbridge = NULL;
    dest->communication_buffer = communication_buffer;
#ifndef BFBRIDGE_KNOW_BUFFER_LEN
    dest->communication_buffer_len = communication_buffer_len;
#endif
    if (!thread->env)
    {
        return make_error(
            BFBRIDGE_LIBRARY_UNINITIALIZED,
            "a bfbridge_thread_t must have been initialized before bfbridge_make_instance",
            NULL);
    }

    if (communication_buffer == NULL || communication_buffer_len < 0)
    {
        return make_error(
            BFBRIDGE_INVALID_COMMUNICATON_BUFFER,
            "communication_buffer NULL or has negative length",
            NULL);
    }

    /* Check if its out thread:
    printf("env pointer for this thread oming\n");

    // verify attached
    BFENVA(thread->vm->jvm, GetEnv, (void**)&env2, 20);*/

    JNIEnv *env = thread->env;
    jobject bfbridge_local =
        BFENVA(env, NewObject, thread->bfbridge_base, thread->constructor);
    // Should be freed: bfbridge
    jobject bfbridge = (jobject)BFENVA(env, NewGlobalRef, bfbridge_local);
    BFENVA(env, DeleteLocalRef, bfbridge_local);

    // Should be freed: bfbridge, buffer (the jobject only)
    jobject buffer =
        BFENVA(env,
               NewDirectByteBuffer,
               communication_buffer,
               communication_buffer_len);
    if (!buffer)
    {
        if (BFENVAV(env, ExceptionCheck) == 1)
        {
            // As of JDK 20, NewDirectByteBuffer only raises OutOfMemoryError
            BFENVAV(env, ExceptionDescribe);
            BFENVA(env, DeleteGlobalRef, bfbridge);

            return make_error(
                BFBRIDGE_OUT_OF_MEMORY_ERROR,
                "NewDirectByteBuffer failed, printing debug info to stderr",
                NULL);
        }
        else
        {
            BFENVA(env, DeleteGlobalRef, bfbridge);
            return make_error(
                BFBRIDGE_JVM_LACKS_BYTE_BUFFERS,
                "Used JVM implementation does not support direct byte buffers"
                "which means that communication between Java and our thread"
                "would need to copy data inefficiently, but only"
                "the direct byte buffer mode is supported by our thread",
                // To implement this, one can save, in a boolean flag
                // in this thread, that a copy required, then
                // make a function in Java that makes new java.nio.ByteBuffer
                // and sets the buffer variable to it,
                // then use getByteArrayRegion and setByteArrayRegion from
                // this thread to copy when needed to interact with buffer.
                NULL);
        }
    }

    /*
    How we would do if we hadn't been caching methods beforehand: This way:
    jmethodID BFSetCommunicationBuffer =
      BFENVA(env, GetMethodID,
      thread->bfbridge_base, "BFSetCommunicationBuffer",
      "(Ljava/nio/ByteBuffer;)V");
    );
    BFENVA(env, CallVoidMethod, bfbridge, thread->BFSetCommunicationBuffer, buffer);
    */
    BFENVA(env, CallVoidMethod, bfbridge, thread->BFSetCommunicationBuffer, buffer);
    BFENVA(env, DeleteLocalRef, buffer);

    // Ease of freeing: keep null until we can return without error
    dest->bfbridge = bfbridge;
    return NULL;
}

void bfbridge_move_instance(bfbridge_instance_t *dest, bfbridge_instance_t *thread)
{
    // Ease of freeing
    if (thread->bfbridge)
    {
        *dest = *thread;
        thread->bfbridge = NULL;
        thread->communication_buffer = NULL;
    }
    else
    {
        dest->bfbridge = NULL;
        dest->communication_buffer = NULL;
    }
}

void bfbridge_free_instance(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    // Ease of freeing
    if (instance->bfbridge && thread->env && thread->vm->jvm)
    {
        BFENVA(thread->env, DeleteGlobalRef, instance->bfbridge);
        instance->bfbridge = NULL;
    }
}

char *bfbridge_instance_get_communication_buffer(
    bfbridge_instance_t *instance, int *len)
{
#ifndef BFBRIDGE_KNOW_BUFFER_LEN
    if (len)
    {
        *len = instance->communication_buffer_len;
    }
#endif
    return instance->communication_buffer;
}

// Shorthand for JavaENV:
#define BFENV (thread->env)
// Instance class:
#define BFINSTC (instance->bfbridge)

// Goal: e.g. BFENVA(BFENV, thread->BFGetErrorLength, BFINSTC, arg1, arg2, ...)
// Call easily
// #define BFFUNC(method_name, ...) BFENVA(BFENV, method_name, BFINSTC, __VA_ARGS__)
// Even more easily:
//      #define BFFUNC(caller, method, ...)
//BFENVA(BFENV, caller, BFINSTC, thread->method, __VA_ARGS__)
// Super easily:
#define BFFUNC(method, type, ...) \
    BFENVA(BFENV, Call##type##Method, BFINSTC, thread->method, __VA_ARGS__)
// Use the second one, void one, for no args as __VA_ARGS__ requires at least one
#define BFFUNCV(method, type) \
    BFENVA(BFENV, Call##type##Method, BFINSTC, thread->method)

// Methods
// Please keep in order with bfbridge_thread_t members

// BFSetCommunicationBuffer is used internally

char *bf_get_error_convenience(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    int len = BFFUNCV(BFGetErrorLength, Int);
    // The case of overflow is handled on Java side
    instance->communication_buffer[len] = '\0';
    return instance->communication_buffer;
}

int bf_get_error_length(bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetErrorLength, Int);
}

int bf_is_compatible(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    char *filepath, int filepath_len)
{
    memcpy(instance->communication_buffer, filepath, filepath_len);
    return BFFUNC(BFIsCompatible, Int, filepath_len);
}

int bf_is_any_file_open(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFIsAnyFileOpen, Int);
}

int bf_open(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    char *filepath, int filepath_len)
{
    memcpy(instance->communication_buffer, filepath, filepath_len);
    return BFFUNC(BFOpen, Int, filepath_len);
}

int bf_get_format(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetFormat, Int);
}

int bf_is_single_file(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    char *filepath, int filepath_len)
{
    memcpy(instance->communication_buffer, filepath, filepath_len);
    return BFFUNC(BFIsSingleFile, Int, filepath_len);
}

int bf_get_current_file(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetCurrentFile, Int);
}

// Lists null-separated filenames/filepaths for the currently open file.
// Returns bytes written including the last null
int bf_get_used_files(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetUsedFiles, Int);
}

int bf_close(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFClose, Int);
}

int bf_get_series_count(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetSeriesCount, Int);
}

int bf_set_current_series(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int ser)
{
    return BFFUNC(BFSetCurrentSeries, Int, ser);
}

int bf_get_resolution_count(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetResolutionCount, Int);
}

int bf_set_current_resolution(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int res)
{
    return BFFUNC(BFSetCurrentResolution, Int, res);
}

int bf_get_size_x(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetSizeX, Int);
}

int bf_get_size_y(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetSizeY, Int);
}

int bf_get_size_c(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetSizeC, Int);
}

int bf_get_size_z(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetSizeZ, Int);
}

int bf_get_size_t(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetSizeT, Int);
}

int bf_get_effective_size_c(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetEffectiveSizeC, Int);
}

int bf_get_image_count(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetImageCount, Int);
}

int bf_get_dimension_order(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetDimensionOrder, Int);
}

int bf_is_order_certain(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFIsOrderCertain, Int);
}

int bf_get_optimal_tile_width(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetOptimalTileWidth, Int);
}

int bf_get_optimal_tile_height(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetOptimalTileHeight, Int);
}

int bf_get_pixel_type(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetPixelType, Int);
}

int bf_get_bits_per_pixel(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetBitsPerPixel, Int);
}

int bf_get_bytes_per_pixel(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetBytesPerPixel, Int);
}

int bf_get_rgb_channel_count(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGetRGBChannelCount, Int);
}

int bf_is_rgb(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFIsRGB, Int);
}

int bf_is_interleaved(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFIsInterleaved, Int);
}

int bf_is_little_endian(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFIsLittleEndian, Int);
}

int bf_is_indexed_color(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFIsIndexedColor, Int);
}

int bf_is_false_color(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFIsFalseColor, Int);
}

int bf_get_8_bit_lookup_table(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGet8BitLookupTable, Int);
}

int bf_get_16_bit_lookup_table(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFGet16BitLookupTable, Int);
}

int bf_open_bytes(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int plane, int x, int y, int w, int h)
{
    return BFFUNC(BFOpenBytes, Int, plane, x, y, w, h);
}

int bf_open_thumb_bytes(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int plane, int w, int h)
{
    return BFFUNC(BFOpenThumbBytes, Int, plane, w, h);
}

double bf_get_mpp_x(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int series)
{
    return BFFUNC(BFGetMPPX, Double, series);
}

double bf_get_mpp_y(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int series)
{
    return BFFUNC(BFGetMPPY, Double, series);
}

double bf_get_mpp_z(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread,
    int series)
{
    return BFFUNC(BFGetMPPZ, Double, series);
}

int bf_dump_ome_xml_metadata(
    bfbridge_instance_t *instance, bfbridge_thread_t *thread)
{
    return BFFUNCV(BFDumpOMEXMLMetadata, Int);
}

#undef BFENVA
#undef BFENV
#undef BFINSTC

#endif // !(defined(BFBRIDGE_INLINE) && !defined(BFBRIDGE_HEADER))
