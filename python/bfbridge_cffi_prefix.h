// CFFI header parser doesn't support ifdef
// which means that we can #include only specially designed headers
// but CFFI it allows us to define opaque types:
// https://cffi.readthedocs.io/en/latest/cdef.html#letting-the-c-compiler-fill-the-gaps

// As the docs linked above says, typedef ... foo_t; is an alternative
// but full opaqueness is not fine for us.
// An alternative is: typedef struct { ... } * foo_t; which is true in C
// but not in C++

// Here, CFFI infers their sizes (they're pointers)
typedef... *JavaVM;
typedef... *JNIEnv;
typedef... *jclass;
typedef... *jmethodID;
typedef... *jobject;
