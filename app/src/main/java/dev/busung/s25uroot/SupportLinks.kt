package dev.busung.s25uroot

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.withStyle

internal const val PROJECT_ISSUES_URL =
    "https://github.com/soumarcelino/Root-My-Galaxy-SM-S918B/issues"

internal fun openProjectIssues(context: Context) {
    context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(PROJECT_ISSUES_URL)))
}

internal fun emphasizeFirmware(message: String, firmware: String?): AnnotatedString =
    buildAnnotatedString {
        if (firmware.isNullOrBlank() || firmware !in message) {
            append(message)
            return@buildAnnotatedString
        }
        val start = message.indexOf(firmware)
        append(message.substring(0, start))
        withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { append(firmware) }
        append(message.substring(start + firmware.length))
    }
