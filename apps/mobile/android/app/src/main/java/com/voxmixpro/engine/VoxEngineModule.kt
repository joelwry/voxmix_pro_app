package com.voxmixpro.engine

import android.app.Activity
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.net.Uri
import android.os.Build
import android.provider.OpenableColumns
import androidx.core.content.FileProvider
import com.facebook.react.bridge.ActivityEventListener
import com.facebook.react.bridge.LifecycleEventListener
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

/**
 * TurboModule exposing the native engine to JavaScript.
 *
 * - Engine commands run on one background thread, in order, so the JS thread never blocks.
 * - Status polling (getStatus) is synchronous and reads only atomics in the engine.
 * - Android integration lives here: file picker, "save as", sharing, audio focus, headphone
 *   unplug (becoming noisy) and app background/foreground.
 */
class VoxEngineModule(private val context: ReactApplicationContext) :
    NativeVoxEngineSpec(context), ActivityEventListener, LifecycleEventListener {

    companion object {
        const val NAME = "VoxEngine"
        private const val REQ_PICK = 4101
        private const val REQ_SAVE = 4102
    }

    private val executor = Executors.newSingleThreadExecutor { r -> Thread(r, "VoxEngineCommands") }
    private val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private var pickPromise: Promise? = null
    private var savePromise: Promise? = null
    private var saveSource: File? = null
    private var focusRequest: AudioFocusRequest? = null

    private val dataDir: File = File(context.filesDir, "voxmix").apply { mkdirs() }

    private val noisyReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context?, intent: Intent?) {
            // headphones unplugged: pause instead of blasting the song through the speaker
            if (intent?.action == AudioManager.ACTION_AUDIO_BECOMING_NOISY) runCommand("pause", "{}")
        }
    }

    private val focusListener = AudioManager.OnAudioFocusChangeListener { change ->
        when (change) {
            AudioManager.AUDIOFOCUS_LOSS, AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> runCommand("pause", "{}")
        }
    }

    init {
        VoxEngineNative.nativeCreate(dataDir.absolutePath)
        context.addActivityEventListener(this)
        context.addLifecycleEventListener(this)
        val filter = IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY)
        if (Build.VERSION.SDK_INT >= 33) context.registerReceiver(noisyReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        else context.registerReceiver(noisyReceiver, filter)
    }

    override fun getName() = NAME

    override fun invalidate() {
        try { context.unregisterReceiver(noisyReceiver) } catch (_: Exception) {}
        executor.submit { VoxEngineNative.nativeCall("onAppBackground", "{}") }
        super.invalidate()
    }

    private fun runCommand(method: String, args: String) {
        executor.submit { VoxEngineNative.nativeCall(method, args) }
    }

    private fun requestFocus() {
        if (Build.VERSION.SDK_INT >= 26) {
            val req = focusRequest ?: AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build()
                )
                .setOnAudioFocusChangeListener(focusListener)
                .build().also { focusRequest = it }
            audioManager.requestAudioFocus(req)
        } else {
            @Suppress("DEPRECATION")
            audioManager.requestAudioFocus(focusListener, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN)
        }
    }

    // ------------------------------------------------------------------ engine

    override fun call(method: String, argsJson: String, promise: Promise) {
        if (method == "play" || method == "startRecording" || method == "startLatencyTest") requestFocus()
        executor.submit {
            try {
                promise.resolve(VoxEngineNative.nativeCall(method, argsJson))
            } catch (t: Throwable) {
                promise.reject("E_ENGINE", t.message ?: "Engine error", t)
            }
        }
    }

    override fun getStatus(): String = VoxEngineNative.nativeStatus()

    // ------------------------------------------------------------------ files

    override fun pickAudioFile(promise: Promise) {
        val activity = getCurrentActivity()
        if (activity == null) { promise.reject("E_NO_ACTIVITY", "The app is not in the foreground"); return }
        pickPromise?.resolve("")
        pickPromise = promise
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "audio/*"
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("audio/*", "application/octet-stream"))
        }
        try {
            activity.startActivityForResult(intent, REQ_PICK)
        } catch (e: Exception) {
            pickPromise = null
            promise.reject("E_PICKER", "No file picker is available on this device", e)
        }
    }

    override fun saveFileAs(path: String, suggestedName: String, mimeType: String, promise: Promise) {
        val activity = getCurrentActivity()
        val src = File(path)
        if (activity == null || !src.exists()) { promise.reject("E_SAVE", "The file is not available"); return }
        savePromise?.resolve("")
        savePromise = promise
        saveSource = src
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = mimeType
            putExtra(Intent.EXTRA_TITLE, suggestedName)
        }
        try {
            activity.startActivityForResult(intent, REQ_SAVE)
        } catch (e: Exception) {
            savePromise = null
            promise.reject("E_SAVE", "No file manager is available on this device", e)
        }
    }

    override fun shareFile(path: String, mimeType: String, promise: Promise) {
        val activity = getCurrentActivity()
        val file = File(path)
        if (activity == null || !file.exists()) { promise.reject("E_SHARE", "The file is not available"); return }
        try {
            val uri = FileProvider.getUriForFile(context, context.packageName + ".fileprovider", file)
            val send = Intent(Intent.ACTION_SEND).apply {
                type = mimeType
                putExtra(Intent.EXTRA_STREAM, uri)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            activity.startActivity(Intent.createChooser(send, "Share song"))
            promise.resolve(true)
        } catch (e: Exception) {
            promise.reject("E_SHARE", e.message ?: "Sharing failed", e)
        }
    }

    override fun getAudioDeviceInfo(promise: Promise) {
        val pm = context.packageManager
        val info = JSONObject()
        info.put("nativeSampleRate", audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull() ?: 48000)
        info.put("framesPerBuffer", audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER)?.toIntOrNull() ?: 0)
        info.put("lowLatency", pm.hasSystemFeature(PackageManager.FEATURE_AUDIO_LOW_LATENCY))
        info.put("proAudio", pm.hasSystemFeature(PackageManager.FEATURE_AUDIO_PRO))
        info.put("sdk", Build.VERSION.SDK_INT)
        info.put("model", Build.MANUFACTURER + " " + Build.MODEL)
        val outputs = JSONArray()
        var bluetooth = false
        var wired = false
        var usb = false
        for (d in audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
            when (d.type) {
                AudioDeviceInfo.TYPE_BLUETOOTH_A2DP, AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> bluetooth = true
                AudioDeviceInfo.TYPE_WIRED_HEADPHONES, AudioDeviceInfo.TYPE_WIRED_HEADSET -> wired = true
                AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_USB_HEADSET -> usb = true
            }
            outputs.put(JSONObject().put("id", d.id).put("type", d.type).put("name", d.productName?.toString() ?: ""))
        }
        if (Build.VERSION.SDK_INT >= 31 && audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
                .any { it.type == AudioDeviceInfo.TYPE_BLE_HEADSET || it.type == AudioDeviceInfo.TYPE_BLE_SPEAKER }) bluetooth = true
        info.put("outputs", outputs)
        info.put("bluetooth", bluetooth)
        info.put("wiredHeadphones", wired)
        info.put("usbAudio", usb)
        promise.resolve(info.toString())
    }

    private fun displayName(uri: Uri): String {
        context.contentResolver.query(uri, null, null, null, null)?.use { c ->
            val i = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (i >= 0 && c.moveToFirst()) return c.getString(i) ?: "audio"
        }
        return uri.lastPathSegment ?: "audio"
    }

    override fun onActivityResult(activity: Activity, requestCode: Int, resultCode: Int, data: Intent?) {
        if (requestCode == REQ_PICK) {
            val p = pickPromise ?: return
            pickPromise = null
            val uri = data?.data
            if (resultCode != Activity.RESULT_OK || uri == null) { p.resolve(""); return }
            // copy the document into the app cache so the engine can read a plain file path
            executor.submit {
                try {
                    val name = displayName(uri)
                    val dir = File(context.cacheDir, "imports").apply { mkdirs() }
                    dir.listFiles()?.forEach { it.delete() }
                    val safe = name.replace(Regex("[^A-Za-z0-9._ ()-]"), "_")
                    val dst = File(dir, safe)
                    context.contentResolver.openInputStream(uri).use { input ->
                        if (input == null) throw IllegalStateException("The file could not be read")
                        dst.outputStream().use { input.copyTo(it, 1 shl 16) }
                    }
                    p.resolve(JSONObject().put("path", dst.absolutePath).put("name", name).put("size", dst.length()).toString())
                } catch (e: Exception) {
                    p.reject("E_IMPORT", e.message ?: "The file could not be read", e)
                }
            }
        } else if (requestCode == REQ_SAVE) {
            val p = savePromise ?: return
            val src = saveSource
            savePromise = null
            saveSource = null
            val uri = data?.data
            if (resultCode != Activity.RESULT_OK || uri == null || src == null) { p.resolve(""); return }
            executor.submit {
                try {
                    context.contentResolver.openOutputStream(uri, "w").use { out ->
                        if (out == null) throw IllegalStateException("The destination could not be opened")
                        src.inputStream().use { it.copyTo(out, 1 shl 16) }
                    }
                    p.resolve(displayName(uri))
                } catch (e: Exception) {
                    p.reject("E_SAVE", e.message ?: "Saving failed (storage full?)", e)
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {}

    override fun onHostResume() { runCommand("onAppForeground", "{}") }

    override fun onHostPause() {
        // a picker or permission dialog also pauses the activity; only real backgrounding
        // (onHostPause without a pending picker) stops audio
        if (pickPromise == null && savePromise == null) runCommand("onAppBackground", "{}")
    }

    override fun onHostDestroy() { runCommand("onAppBackground", "{}") }
}
