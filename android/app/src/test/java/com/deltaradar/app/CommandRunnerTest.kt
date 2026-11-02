package com.deltaradar.app

import java.io.File
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.*
import org.junit.Test
import kotlin.concurrent.thread

class CommandRunnerTest {
    @Test fun outputAndExitCodeAreReturnedWithoutRetry() {
        val calls = AtomicInteger()
        val runner = CommandRunner { calls.incrementAndGet(); ProcessBuilder(it).redirectErrorStream(true).start() }
        val result = runner.run(listOf("sh", "-c", "echo expected; exit 7"), 1000)
        assertEquals("expected", result.output)
        assertEquals(7, result.exitCode)
        assertFalse(result.success)
        assertEquals(1, calls.get())
    }
    @Test fun timeoutIsBoundedEvenWhenDescendantKeepsStdoutOpen() {
        val before = System.nanoTime()
        val result = CommandRunner().run(listOf("sh", "-c", "sleep 2 & wait"), 100)
        assertTrue(result.timedOut)
        assertTrue(TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - before) < 1000)
    }
    @Test fun cancellationBeforeDispatchPreventsAllLaunches() {
        val calls = AtomicInteger()
        val runner = CommandRunner { calls.incrementAndGet(); ProcessBuilder(it).start() }
        val cancellation = CommandRunner.Cancellation()
        cancellation.cancel()
        assertTrue(runner.run(listOf("sh", "-c", "echo should-not-run"), 1000, cancellation).cancelled)
        assertEquals(0, calls.get())
    }
    @Test fun inFlightCancellationReturnsWithoutRetrying() {
        val entered = CountDownLatch(1)
        val calls = AtomicInteger()
        val runner = CommandRunner { calls.incrementAndGet(); ProcessBuilder(it).start().also { entered.countDown() } }
        val cancellation = CommandRunner.Cancellation()
        var result: CommandRunner.Result? = null
        val worker = thread { result = runner.run(listOf("sh", "-c", "sleep 2"), 5000, cancellation) }
        assertTrue(entered.await(1, TimeUnit.SECONDS))
        cancellation.cancel()
        worker.join(1000)
        assertFalse(worker.isAlive)
        assertTrue(result!!.cancelled)
        assertEquals(1, calls.get())
    }
    @Test fun uncertainSideEffectIsNotExecutedTwice() {
        val marker = Files.createTempFile("command-once", ".txt").toFile()
        try {
            val result = CommandRunner().run(listOf("sh", "-c", "echo one >> '${marker.absolutePath}'; sleep 2"), 100)
            assertTrue(result.timedOut)
            assertEquals(listOf("one"), marker.readLines())
        } finally { marker.delete() }
    }
    @Test fun cancelledSessionRevokesNativeLeaseAndCleanupIsClaimedOnlyOnce() {
        val dir = Files.createTempDirectory("scanner-session").toFile()
        try {
            val session = ScannerSession(dir)
            assertEquals("active\n", session.lease.readText())
            session.cancel()
            assertFalse(session.lease.exists())
            assertTrue(session.cancellation.isCancelled())
            assertTrue(session.claimCleanup())
            assertFalse(session.claimCleanup())
        } finally { dir.deleteRecursively() }
    }
}
