package com.msgbyte.dao.ui

import android.graphics.Bitmap
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

internal class TabThumbnailCaptureCoordinator(
    private val scope: CoroutineScope,
    private val captureThumbnail: suspend () -> Bitmap?,
    private val saveThumbnail: suspend (tabId: String, private: Boolean, bitmap: Bitmap) -> Unit,
    private val deleteThumbnail: suspend (tabId: String) -> Unit,
    private val onError: (Throwable) -> Unit = {},
) {
    private val activeGenerations = mutableMapOf<String, Long>()
    private val captureJobs = mutableMapOf<String, Job>()
    private var nextGeneration = 0L

    fun capture(tabId: String, private: Boolean) {
        val previous = captureJobs.remove(tabId)
        previous?.cancel()
        val generation = ++nextGeneration
        activeGenerations[tabId] = generation

        lateinit var job: Job
        job = scope.launch(start = CoroutineStart.UNDISPATCHED) {
            try {
                previous?.join()
                val bitmap = captureThumbnail()
                if (bitmap != null && isActive && activeGenerations[tabId] == generation) {
                    saveThumbnail(tabId, private, bitmap)
                }
            } catch (error: Throwable) {
                report(error)
            }
        }
        captureJobs[tabId] = job
        job.invokeOnCompletion {
            if (captureJobs[tabId] === job) {
                captureJobs.remove(tabId)
                if (activeGenerations[tabId] == generation) activeGenerations.remove(tabId)
            }
        }
    }

    suspend fun close(tabId: String) {
        activeGenerations.remove(tabId)
        try {
            captureJobs.remove(tabId)?.cancelAndJoin()
            deleteThumbnail(tabId)
        } catch (error: Throwable) {
            report(error)
        }
    }

    private fun report(error: Throwable) {
        if (error is CancellationException) throw error
        onError(error)
    }
}
