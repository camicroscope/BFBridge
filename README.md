# BFBridge

BioFormats wrapper in C using Java JNI, plus a Python wrapper using CFFI.

## C

```c
bfbridge_vm_t vm;
bfbridge_thread_t thread;
bfbridge_instance_t instance;
bfbridge_error_t *err = bfbridge_make_vm(&vm);
if (err) {
    printf("%s\n", err->description);
    bfbridge_free_error(err);
    return;
}
err = bfbridge_make_thread(&thread, &vm);
if (err) {
    bfbridge_free_vm(&vm);
    printf("%s\n", err->description);
    bfbridge_free_error(err);
    return;
}
int buffer_len = 10000000;
char communication_buffer[buffer_len];
err = bfbridge_make_instance(&instance, &thread, &vm, &communication_buffer, buffer_len);
if (err) {
    bfbridge_free_vm(&vm);
    printf("%s\n", err->description);
    bfbridge_free_error(err);
    return;
}
const char *file = "/path/to/file.svs";
int code = bf_open(&instance, &thread, file, strlen(file));
if (code < 0) {
    printf("%s\n", bf_get_error_convenience(&instance, &thread));
}
// ...
// Read from communication_buffer if methods write to it
// ...
bfbridge_free_vm(&vm);
```

You may also define `BFBRIDGE_INLINE` from including the header to make it a header-only library for performance.

## Python

```py
import BFBridge.python as bfbridge

vm = bfbridge.BFBridgeVM()
thread = bfbridge.BFBridgeThread(vm)
instance = bfbridge.BFBridgeInstance(thread)

# if you would like to keep the Java thread alive as long as the Python thread is alive:
import threading
thread_holder = threading.local()
thread_holder.thread = thread
# ^^^this should be in a separate file to avoid Python optimize this

instance.open("/path/to/file.svs")

```

### Ease of use

For example, if bfbridge_make_thread fails, calling bfbridge_make_instance, bfbridge_free_instance, bfbridge_free_thread will not cause any segmentation fault. This is important because it means that the C++ or Python destructor won't fail if it tries to free any structures that haven't been allocated yet. This means that the library make functions, on failure, set a failure marker so that free functions won't cause nullpointer dereference. However the user of the library must ensure allocation and deallocation of the types.

## Limitations

- Python code does not work well with, for example, matplotlib. This could be due to TCL. This can be overcome by making all BioFormats calls from a separate thread.

https://docs.oracle.com/en/java/javase/20/docs/specs/jni/invocation.html#creating-the-vm

> Note: Depending on the operating system, the primordial process thread may be subject to special handling that impacts its ability to function properly as a normal Java thread (such as having a limited stack size and being able to throw StackOverflowError). It is strongly recommended that the primordial thread is not used to load the Java VM, but that a new thread is created just for that purpose.

- Supports reader caching function of BioFormats but the user of this library should ensure that after updating Java, the previous cache folder needs to be deleted. Hopefully updating BioFormats does not require this as BioFormats attempts to detect it.

- Python code assumes that 1 Python thread corresponds to 1 system thread. This is always true at least for CPython.

- Python error handling far from perfect as for for functions that return an integer/float, the Python library passes on the negative values signifying error.

## Debugging

On Unix SIGSEGV can mean many things. It can be any C segmentation fault (null pointer dereference, etc.) or it could also mean that the thread that tries to make a BioFormats call wasn't attached, i.e. it reused a bioformats thread created by another thread. To debug this, add:

```c
    JNIEnv *env2;
    BFENVA(thread->vm->jvm, GetEnv, (void**)&env2, 20);
    printf("env for this thread should be nonnull: %p", env2);
```

inside the method where the crash happens.

Besides, uncommenting "-Xcheck:jni" will likely help.
