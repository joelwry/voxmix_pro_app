package com.voxmixpro.engine

import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Converts formats the native engine does not decode itself (M4A/AAC, OGG/Opus/Vorbis, FLAC,
 * AMR, 3GP...) to a 16-bit WAV with Android's own MediaCodec decoders, so imports are not
 * limited to WAV and MP3. WAV and MP3 are passed through untouched (decoded natively).
 */
object AudioDecoder {
    fun needsConversion(file: File): Boolean {
        val head = ByteArray(12)
        file.inputStream().use { it.read(head) }
        val riff = String(head, 0, 4, Charsets.US_ASCII) == "RIFF" && String(head, 8, 4, Charsets.US_ASCII) == "WAVE"
        val id3 = String(head, 0, 3, Charsets.US_ASCII) == "ID3"
        val mpegSync = (head[0].toInt() and 0xFF) == 0xFF && (head[1].toInt() and 0xE0) == 0xE0
        if (riff || id3 || mpegSync) return false
        return true
    }

    /** Decodes [src] into [dst] (16-bit PCM WAV). Throws with a user-facing message on failure. */
    fun decodeToWav(src: File, dst: File) {
        val extractor = MediaExtractor()
        try {
            extractor.setDataSource(src.absolutePath)
            var track = -1
            for (i in 0 until extractor.trackCount) {
                val mime = extractor.getTrackFormat(i).getString(MediaFormat.KEY_MIME) ?: ""
                if (mime.startsWith("audio/")) { track = i; break }
            }
            if (track < 0) throw IllegalStateException("This file has no audio track")
            extractor.selectTrack(track)
            val format = extractor.getTrackFormat(track)
            val mime = format.getString(MediaFormat.KEY_MIME)!!
            val codec = try { MediaCodec.createDecoderByType(mime) } catch (e: Exception) {
                throw IllegalStateException("This phone cannot decode $mime audio")
            }
            var sampleRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE)
            var channels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
            var pcmFloat = false
            codec.configure(format, null, null, 0)
            codec.start()
            RandomAccessFile(dst, "rw").use { out ->
                out.setLength(0)
                out.write(ByteArray(44))   // header written at the end
                var dataBytes = 0L
                val info = MediaCodec.BufferInfo()
                var inputDone = false
                var outputDone = false
                val conv = ByteBuffer.allocate(1 shl 16).order(ByteOrder.LITTLE_ENDIAN)
                while (!outputDone) {
                    if (!inputDone) {
                        val inIndex = codec.dequeueInputBuffer(10_000)
                        if (inIndex >= 0) {
                            val buf = codec.getInputBuffer(inIndex)!!
                            val size = extractor.readSampleData(buf, 0)
                            if (size < 0) {
                                codec.queueInputBuffer(inIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                                inputDone = true
                            } else {
                                codec.queueInputBuffer(inIndex, 0, size, extractor.sampleTime, 0)
                                extractor.advance()
                            }
                        }
                    }
                    val outIndex = codec.dequeueOutputBuffer(info, 10_000)
                    when {
                        outIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                            val of = codec.outputFormat
                            sampleRate = of.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                            channels = of.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                            pcmFloat = of.containsKey(MediaFormat.KEY_PCM_ENCODING) &&
                                of.getInteger(MediaFormat.KEY_PCM_ENCODING) == AudioFormat.ENCODING_PCM_FLOAT
                        }
                        outIndex >= 0 -> {
                            val buf = codec.getOutputBuffer(outIndex)!!
                            buf.position(info.offset)
                            buf.limit(info.offset + info.size)
                            if (pcmFloat) {
                                buf.order(ByteOrder.nativeOrder())
                                val fb = buf.asFloatBuffer()
                                while (fb.hasRemaining()) {
                                    conv.clear()
                                    val count = minOf(fb.remaining(), conv.capacity() / 2)
                                    for (k in 0 until count) conv.putShort((fb.get().coerceIn(-1f, 1f) * 32767f).toInt().toShort())
                                    out.write(conv.array(), 0, conv.position())
                                    dataBytes += conv.position()
                                }
                            } else {
                                val bytes = ByteArray(info.size)
                                buf.get(bytes)
                                out.write(bytes)
                                dataBytes += bytes.size
                            }
                            codec.releaseOutputBuffer(outIndex, false)
                            if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) outputDone = true
                        }
                    }
                }
                codec.stop()
                codec.release()
                if (dataBytes == 0L) throw IllegalStateException("No audio could be decoded from this file")
                out.seek(0)
                out.write(wavHeader(sampleRate, channels, dataBytes))
            }
        } finally {
            extractor.release()
        }
    }

    private fun wavHeader(rate: Int, channels: Int, dataBytes: Long): ByteArray {
        val b = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN)
        b.put("RIFF".toByteArray(Charsets.US_ASCII)); b.putInt((36 + dataBytes).toInt())
        b.put("WAVEfmt ".toByteArray(Charsets.US_ASCII)); b.putInt(16)
        b.putShort(1); b.putShort(channels.toShort()); b.putInt(rate)
        b.putInt(rate * channels * 2); b.putShort((channels * 2).toShort()); b.putShort(16)
        b.put("data".toByteArray(Charsets.US_ASCII)); b.putInt(dataBytes.toInt())
        return b.array()
    }
}
