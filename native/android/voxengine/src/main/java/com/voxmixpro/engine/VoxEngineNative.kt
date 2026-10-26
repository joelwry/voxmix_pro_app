package com.voxmixpro.engine

/**
 * Thin JNI binding to the C++ engine (native/audio-engine). Every command is
 * `call(method, jsonArgs) -> jsonResult`; `status()` returns a cheap real-time snapshot.
 * call() can block (imports, exports, analysis): never invoke it on the UI thread.
 */
object VoxEngineNative {
    init {
        System.loadLibrary("voxengine")
    }

    @JvmStatic external fun nativeCreate(dataDir: String): Boolean
    @JvmStatic external fun nativeCall(method: String, args: String): String
    @JvmStatic external fun nativeStatus(): String
    @JvmStatic external fun nativeDestroy()
}
