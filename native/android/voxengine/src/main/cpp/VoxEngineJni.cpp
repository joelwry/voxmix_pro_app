// JNI bridge: the whole engine API is two functions (call + status), see vox/Engine.h.
// No audio data ever crosses this boundary.

#include "OboeDevice.h"
#include "vox/Engine.h"
#include <jni.h>
#include <memory>
#include <mutex>

namespace
{
std::mutex gLock;
std::unique_ptr<vox::Engine> gEngine;

std::string toString (JNIEnv* env, jstring s)
{
    if (! s) return {};
    const char* c = env->GetStringUTFChars (s, nullptr);
    std::string r (c ? c : "");
    if (c) env->ReleaseStringUTFChars (s, c);
    return r;
}

jstring toJ (JNIEnv* env, const std::string& s) { return env->NewStringUTF (s.c_str()); }
} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_voxmixpro_engine_VoxEngineNative_nativeCreate (JNIEnv* env, jclass, jstring dataDir)
{
    std::lock_guard<std::mutex> l (gLock);
    if (! gEngine)
        gEngine = std::make_unique<vox::Engine> (toString (env, dataDir), std::make_unique<vox::OboeDevice>());
    return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_voxmixpro_engine_VoxEngineNative_nativeCall (JNIEnv* env, jclass, jstring method, jstring args)
{
    vox::Engine* e;
    {
        std::lock_guard<std::mutex> l (gLock);
        e = gEngine.get();
    }
    if (! e) return toJ (env, "{\"ok\":false,\"error\":\"Engine not initialised\"}");
    return toJ (env, e->call (toString (env, method), toString (env, args)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_voxmixpro_engine_VoxEngineNative_nativeStatus (JNIEnv* env, jclass)
{
    vox::Engine* e;
    {
        std::lock_guard<std::mutex> l (gLock);
        e = gEngine.get();
    }
    return toJ (env, e ? e->status() : std::string ("{}"));
}

extern "C" JNIEXPORT void JNICALL
Java_com_voxmixpro_engine_VoxEngineNative_nativeDestroy (JNIEnv*, jclass)
{
    std::lock_guard<std::mutex> l (gLock);
    gEngine.reset();
}
