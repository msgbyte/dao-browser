package com.msgbyte.dao.browser

import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper

internal class BrowserDatabase(
    context: Context,
    databaseName: String,
) : SQLiteOpenHelper(context.applicationContext, databaseName, null, VERSION) {
    override fun onCreate(database: SQLiteDatabase) {
        database.execSQL(
            """
            CREATE TABLE history_visits (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                url TEXT NOT NULL,
                title TEXT NOT NULL,
                visited_at INTEGER NOT NULL
            )
            """.trimIndent(),
        )
        database.execSQL(
            """
            CREATE INDEX history_visits_visited_at
            ON history_visits(visited_at DESC)
            """.trimIndent(),
        )
        database.execSQL(
            """
            CREATE TABLE bookmark_folders (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                title TEXT NOT NULL,
                created_at INTEGER NOT NULL
            )
            """.trimIndent(),
        )
        database.execSQL(
            """
            CREATE TABLE bookmarks (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                url TEXT NOT NULL,
                title TEXT NOT NULL,
                folder_id INTEGER,
                kind TEXT NOT NULL,
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL,
                UNIQUE(url, kind),
                FOREIGN KEY(folder_id) REFERENCES bookmark_folders(id) ON DELETE SET NULL
            )
            """.trimIndent(),
        )
        database.execSQL(
            """
            CREATE INDEX bookmarks_updated_at
            ON bookmarks(updated_at DESC)
            """.trimIndent(),
        )
    }

    override fun onConfigure(database: SQLiteDatabase) {
        super.onConfigure(database)
        database.setForeignKeyConstraintsEnabled(true)
    }

    override fun onUpgrade(database: SQLiteDatabase, oldVersion: Int, newVersion: Int) = Unit

    private companion object {
        const val VERSION = 1
    }
}
