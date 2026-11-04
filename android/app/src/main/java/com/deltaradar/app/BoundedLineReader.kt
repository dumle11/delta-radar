package com.deltaradar.app

import java.io.Closeable
import java.io.IOException
import java.io.Reader

/** Buffered LF/CRLF/CR records without unbounded readLine() allocation. An
 * oversized record terminates this client; it must never become a partial frame. */
class BoundedLineReader(private val reader: Reader, private val maxChars: Int) : Closeable {
    init { require(maxChars > 0) }
    private val buffer = CharArray(8192)
    private var offset = 0
    private var available = 0
    private var skipLf = false

    fun readLine(): String? {
        if (available < 0) return null
        val line = StringBuilder(minOf(maxChars, 256))
        while (true) {
            if (offset == available) {
                available = reader.read(buffer)
                offset = 0
                if (available < 0) return if (line.isEmpty()) null else line.toString()
                if (available == 0) continue
            }
            if (skipLf) {
                skipLf = false
                if (buffer[offset] == '\n') { offset++; continue }
            }
            val start = offset
            while (offset < available && buffer[offset] != '\n' && buffer[offset] != '\r') offset++
            val count = offset - start
            if (line.length > maxChars - count) throw IOException("Entity frame exceeds $maxChars characters")
            line.append(buffer, start, count)
            if (offset < available) {
                skipLf = buffer[offset++] == '\r'
                return line.toString()
            }
        }
    }

    override fun close() = reader.close()
}
