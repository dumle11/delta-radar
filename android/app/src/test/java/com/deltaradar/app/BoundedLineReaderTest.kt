package com.deltaradar.app

import java.io.IOException
import java.io.StringReader
import org.junit.Assert.*
import org.junit.Test

class BoundedLineReaderTest {
    @Test(timeout = 1000) fun preservesLineEndingsAndUnterminatedFinalRecord() {
        BoundedLineReader(StringReader("a\r\nb\rc\n\nlast"), 16).use {
            assertEquals(listOf("a", "b", "c", "", "last"), List(5) { _ -> it.readLine() })
            assertNull(it.readLine()); assertNull(it.readLine())
        }
    }

    @Test(timeout = 1000) fun handlesCrLfSplitAcrossBufferRefills() {
        val first = "a".repeat(8191)
        BoundedLineReader(StringReader(first + "\r\nnext\n"), 8191).use {
            assertEquals(first, it.readLine()); assertEquals("next", it.readLine()); assertNull(it.readLine())
        }
    }

    @Test(timeout = 1000) fun exactLimitIsAcceptedAcrossRefills() {
        val full = "a".repeat(9000)
        BoundedLineReader(StringReader(full + "\nend"), full.length).use {
            assertEquals(full, it.readLine()); assertEquals("end", it.readLine())
        }
    }

    @Test(timeout = 1000) fun oversizedRecordsAreRejectedWithOrWithoutNewline() {
        for (suffix in listOf("", "\n", "\r\n")) {
            BoundedLineReader(StringReader("a".repeat(9001) + suffix), 9000).use {
                assertThrows(IOException::class.java) { it.readLine() }
            }
        }
    }

    @Test(timeout = 1000) fun eofIsDifferentFromAnEmptyRecordAndCloseReleasesTheReader() {
        val source = StringReader("\n")
        BoundedLineReader(source, 1).use { assertEquals("", it.readLine()); assertNull(it.readLine()) }
        assertThrows(IOException::class.java) { source.read() }
    }
}
