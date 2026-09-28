package dev.busung.s25uroot

import android.content.Context
import android.content.Intent
import android.net.Uri

internal const val PROJECT_ISSUES_URL =
    "https://github.com/soumarcelino/Root-My-Galaxy-SM-S918B/issues"

internal fun openProjectIssues(context: Context) {
    context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(PROJECT_ISSUES_URL)))
}
