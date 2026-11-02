package com.deltaradar.app

import java.io.ByteArrayOutputStream
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

/** One process per command. No retries: timeout means the result is uncertain. */
class CommandRunner(private val spawn: (List<String>) -> Process = {
    ProcessBuilder(it).redirectErrorStream(true).start()
}) {
    class Cancellation {
        private var cancelled = false
        private val processes = mutableSetOf<Process>()
        @Synchronized fun isCancelled() = cancelled
        @Synchronized fun start(factory: () -> Process): Process? {
            if (cancelled) return null
            return factory().also { processes.add(it) }
        }
        @Synchronized fun release(process: Process) { processes.remove(process) }
        @Synchronized fun cancel() {
            cancelled = true
            processes.forEach { it.destroyForcibly() }
        }
    }
    data class Result(val output: String, val exitCode: Int?, val timedOut: Boolean, val cancelled: Boolean) {
        val success: Boolean get() = exitCode == 0 && !timedOut && !cancelled
    }
    fun run(command: List<String>, timeoutMs: Long, cancellation: Cancellation = Cancellation()): Result {
        require(timeoutMs > 0)
        val process = try { cancellation.start { spawn(command) } }
            catch (e: Exception) { return Result(e.message.orEmpty(), null, false, cancellation.isCancelled()) }
            ?: return Result("", null, false, true)
        try { process.outputStream.close() } catch (_: Exception) {}
        val output = ByteArrayOutputStream()
        val reading = AtomicBoolean(true)
        val reader = thread(isDaemon = true, name = "command-output") {
            try {
                process.inputStream.use { stream ->
                    val buffer = ByteArray(4096)
                    while (reading.get() || stream.available() > 0) {
                        val available = stream.available()
                        if (available <= 0) { Thread.sleep(5); continue }
                        val count = stream.read(buffer, 0, minOf(buffer.size, available))
                        if (count < 0) break
                        synchronized(output) {
                            if (output.size() < 1024 * 1024) output.write(buffer, 0, minOf(count, 1024 * 1024 - output.size()))
                        }
                    }
                }
            } catch (_: Exception) {}
        }
        var complete = false
        try {
            complete = process.waitFor(timeoutMs, TimeUnit.MILLISECONDS)
            if (!complete || cancellation.isCancelled()) process.destroyForcibly()
            // Never wait indefinitely on a descendant that inherited stdout.
            reading.set(false)
            reader.join(100)
            return Result(synchronized(output) { output.toString("UTF-8").trim() },
                if (complete) process.exitValue() else null, !complete && !cancellation.isCancelled(), cancellation.isCancelled())
        } catch (_: InterruptedException) {
            process.destroyForcibly()
            Thread.currentThread().interrupt()
            return Result("", null, false, true)
        } finally {
            reading.set(false)
            cancellation.release(process)
        }
    }
}
