/*
 * JNI shim over embed/tacky.h for the Android backend service. Thin on purpose:
 * it moves bytes and owns nothing else. All routing, framing and notification
 * logic lives in Java, which is where the Android APIs that consume it are.
 *
 * Frames cross as byte[], not String. NewStringUTF/GetStringUTFChars speak
 * modified UTF-8, which encodes anything outside the BMP as a surrogate pair in
 * six bytes rather than the four real UTF-8 uses - so every emoji in a message
 * would be mangled in both directions. Java does the decoding instead.
 */
#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ringbroker.h"
#include "tacky.h"

typedef struct {
    JavaVM *vm;
    jobject sink;      /* global ref to the FrameSink */
    jmethodID on_frame; /* void onFrame(byte[]) */
    tacky *client;
} ctx;

/* tacky's backend thread is long-lived, so it is attached once and detached by
 * this key's destructor when it exits. Detaching per frame would be correct too
 * but costs an attach on every message in a history page. */
static pthread_key_t detach_key;
static pthread_once_t detach_key_once = PTHREAD_ONCE_INIT;

static void detach_current_thread(void *vm) {
    if (vm)
        (*(JavaVM *)vm)->DetachCurrentThread((JavaVM *)vm);
}

static void make_detach_key(void) {
    pthread_key_create(&detach_key, detach_current_thread);
}

static JNIEnv *env_for_current_thread(JavaVM *vm) {
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK)
        return env;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
        return NULL;
    pthread_once(&detach_key_once, make_detach_key);
    pthread_setspecific(detach_key, vm);
    return env;
}

/* Runs on tacky's backend thread. Must not block or re-enter tacky, so the Java
 * side only enqueues. */
static void emit_cb(void *ud, const char *json, size_t len) {
    ctx *c = (ctx *)ud;
    JNIEnv *env = env_for_current_thread(c->vm);
    if (!env)
        return;
    jbyteArray arr = (*env)->NewByteArray(env, (jsize)len);
    if (!arr)
        return; /* OOM: the frame is dropped, which beats aborting the service */
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)len, (const jbyte *)json);
    (*env)->CallVoidMethod(env, c->sink, c->on_frame, arr);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env); /* never unwind into tacky */
    (*env)->DeleteLocalRef(env, arr);
}

JNIEXPORT jlong JNICALL
Java_org_qtproject_example_quackchat_TackyNative_nativeCreate(
    JNIEnv *env, jclass cls, jobjectArray args, jobject sink) {
    (void)cls;
    ctx *c = calloc(1, sizeof(ctx));
    if (!c)
        return 0;
    if ((*env)->GetJavaVM(env, &c->vm) != JNI_OK) {
        free(c);
        return 0;
    }
    jclass sink_cls = (*env)->GetObjectClass(env, sink);
    c->on_frame = (*env)->GetMethodID(env, sink_cls, "onFrame", "([B)V");
    c->sink = (*env)->NewGlobalRef(env, sink);
    if (!c->on_frame || !c->sink) {
        free(c);
        return 0;
    }

    /* taco args are option flags and app-private paths, so plain UTF-8 chars
     * are enough here; nothing outside the BMP reaches this array. */
    const jsize n = args ? (*env)->GetArrayLength(env, args) : 0;
    const char **argv = calloc((size_t)n + 1, sizeof(char *));
    jstring *held = calloc((size_t)n ? (size_t)n : 1, sizeof(jstring));
    for (jsize i = 0; i < n; ++i) {
        held[i] = (jstring)(*env)->GetObjectArrayElement(env, args, i);
        argv[i] = (*env)->GetStringUTFChars(env, held[i], NULL);
    }

    /* The UI process maps video frame rings through this. */
    if (ringbroker_start(RINGBROKER_ANDROID_SOCKET) != 0)
        __android_log_print(ANDROID_LOG_ERROR, "quack.jni", "cannot serve frame rings on %s",
                            RINGBROKER_ANDROID_SOCKET);

    /* Callbacks can fire before this returns, so ctx is complete beforehand. */
    c->client = tacky_create(argv, emit_cb, c);

    for (jsize i = 0; i < n; ++i) {
        (*env)->ReleaseStringUTFChars(env, held[i], argv[i]);
        (*env)->DeleteLocalRef(env, held[i]);
    }
    free(argv);
    free(held);

    if (!c->client) {
        (*env)->DeleteGlobalRef(env, c->sink);
        free(c);
        return 0;
    }
    return (jlong)(intptr_t)c;
}

JNIEXPORT void JNICALL
Java_org_qtproject_example_quackchat_TackyNative_nativeSend(JNIEnv *env, jclass cls,
                                                            jlong handle,
                                                            jbyteArray json) {
    (void)cls;
    ctx *c = (ctx *)(intptr_t)handle;
    if (!c || !c->client || !json)
        return;
    const jsize len = (*env)->GetArrayLength(env, json);
    jbyte *bytes = (*env)->GetByteArrayElements(env, json, NULL);
    if (!bytes)
        return;
    tacky_send(c->client, (const char *)bytes, (size_t)len);
    (*env)->ReleaseByteArrayElements(env, json, bytes, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_org_qtproject_example_quackchat_TackyNative_nativeDestroy(JNIEnv *env,
                                                               jclass cls,
                                                               jlong handle) {
    (void)cls;
    ctx *c = (ctx *)(intptr_t)handle;
    if (!c)
        return;
    if (c->client)
        tacky_destroy(c->client); /* joins the thread; no callbacks after this */
    (*env)->DeleteGlobalRef(env, c->sink);
    free(c);
}
