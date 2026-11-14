package com.dme.launcher

import android.content.Context
import java.io.BufferedReader
import java.io.File
import java.io.InputStreamReader

object Injector {

    private const val ASSET_NAME = "andkittyinjector"
    private const val DEPLOY_DIR = "/data/local/tmp/.prf"
    private const val BINARY_PATH = "$DEPLOY_DIR/injector"

    fun isDeployed(): Boolean {
        val result = RootShell.exec("[ -x '$BINARY_PATH' ] && echo YES || echo NO")
        return result.trim() == "YES"
    }

    fun deploy(context: Context): Boolean {
        val local = File(context.noBackupFilesDir, ASSET_NAME)
        try {
            context.assets.open(ASSET_NAME).use { input ->
                local.outputStream().use { output -> input.copyTo(output) }
            }
        } catch (_: Exception) {
            return false
        }

        val result = RootShell.exec(
            "mkdir -p '$DEPLOY_DIR' && " +
                "cp '${local.absolutePath}' '$BINARY_PATH.tmp' && " +
                "chmod 755 '$BINARY_PATH.tmp' && " +
                "mv '$BINARY_PATH.tmp' '$BINARY_PATH' && " +
                "echo DEPLOYED"
        )
        local.delete()
        return result.contains("DEPLOYED")
    }

    fun getSELinuxStatus(): String {
        return RootShell.exec("getenforce").trim()
    }

    fun setSELinuxEnforce(enforce: Boolean): String {
        val mode = if (enforce) "1" else "0"
        RootShell.exec("setenforce $mode")
        return getSELinuxStatus()
    }

    fun inject(
        pid: Int?,
        packageName: String?,
        libPath: String,
        useMemfd: Boolean,
        hideLib: Boolean,
        watchMode: Boolean,
        onOutput: (String) -> Unit
    ): Boolean {
        val target = when {
            watchMode && packageName != null -> "--package '$packageName' --watch"
            packageName != null -> "--package '$packageName'"
            pid != null -> "--pid $pid"
            else -> return false
        }

        val flags = buildString {
            if (useMemfd) append(" --memfd")
            if (hideLib) append(" --hide")
        }

        val injectorCmd = "$BINARY_PATH $target --libs '$libPath'$flags"
        onOutput("[>] $injectorCmd")

        val cmd = "setenforce 0; $injectorCmd; RET=\$?; setenforce 1; exit \$RET"
        onOutput("[*] Running with SELinux permissive")

        var success = false
        try {
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", cmd))
            val stdout = BufferedReader(InputStreamReader(process.inputStream))
            val stderr = BufferedReader(InputStreamReader(process.errorStream))

            val stderrThread = Thread {
                try {
                    stderr.forEachLine { line ->
                        if (line.isNotBlank()) onOutput(line)
                        if (isSuccessLine(line)) success = true
                    }
                } catch (_: Exception) {}
            }.also { it.isDaemon = true; it.start() }

            stdout.forEachLine { line ->
                if (line.isNotBlank()) onOutput(line)
                if (isSuccessLine(line)) success = true
            }

            stderrThread.join(5000)
            val exitCode = process.waitFor()
            onOutput("[*] Injector exited with code $exitCode")
            if (exitCode == 0) success = true
        } catch (e: Exception) {
            onOutput("[!] Error: ${e.message}")
        }

        return success
    }

    private fun isSuccessLine(line: String): Boolean {
        val lower = line.lowercase()
        return lower.contains("injected") || lower.contains("success") ||
            lower.contains("dlopen") || lower.contains("jni_onload")
    }

    fun pushLibrary(localPath: String): String? {
        val filename = File(localPath).name
        val remotePath = "$DEPLOY_DIR/$filename"
        val result = RootShell.exec(
            "mkdir -p '$DEPLOY_DIR' && " +
                "cp '$localPath' '$remotePath.tmp' && " +
                "chmod 644 '$remotePath.tmp' && " +
                "mv '$remotePath.tmp' '$remotePath' && " +
                "echo PUSHED"
        )
        return if (result.contains("PUSHED")) remotePath else null
    }
}
